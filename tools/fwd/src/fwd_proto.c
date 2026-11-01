/*
 * fwd_proto.c - addresses, command encoding/decoding, response parsing and
 * decoding into readable text.
 *
 * Every command has a schema string, one character per argument:
 *   u  unsigned 32 bit decimal      p  port (0..65535)
 *   P  protocol (1 tcp, 2 udp)      T  tunnel type (1 tun, 2 tap)
 *   a  IP address (numeric on wire) s  short word, no ':'
 *   M  tunnel mode (0 connect, 1 listen)
 * The same table drives encode-from-struct, encode-from-strings and decode.
 */
#include "fwd.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------ */
/* Addresses                                                          */
/* ------------------------------------------------------------------ */
static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_uint(const char *s, uint32_t *out, uint32_t max)
{
    unsigned long long v = 0;
    size_t i;

    if (!s || !*s || strlen(s) > 10)
        return FWD_E_PARSE;
    for (i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9')
            return FWD_E_PARSE;
        v = v * 10 + (unsigned)(s[i] - '0');
    }
    if (v > max)
        return FWD_E_RANGE;
    *out = (uint32_t)v;
    return FWD_OK;
}

void fwd_addr_from_u32(fwd_addr *a, uint32_t v)
{
    memset(a, 0, sizeof *a);
    a->family = FWD_V4;
    a->bytes[0] = (uint8_t)(v >> 24);
    a->bytes[1] = (uint8_t)(v >> 16);
    a->bytes[2] = (uint8_t)(v >> 8);
    a->bytes[3] = (uint8_t)v;
}

int fwd_addr_to_u32(const fwd_addr *a, uint32_t *v)
{
    if (a->family != FWD_V4)
        return FWD_E_INVAL;
    *v = (uint32_t)a->bytes[0] << 24 | (uint32_t)a->bytes[1] << 16 |
         (uint32_t)a->bytes[2] << 8 | a->bytes[3];
    return FWD_OK;
}

int fwd_addr_from_string(fwd_addr *a, const char *s)
{
    memset(a, 0, sizeof *a);
    if (!s || !*s)
        return FWD_E_PARSE;
    if (inet_pton(AF_INET, s, a->bytes) == 1) {
        a->family = FWD_V4;
        return FWD_OK;
    }
    memset(a, 0, sizeof *a);
    if (inet_pton(AF_INET6, s, a->bytes) == 1) {
        a->family = FWD_V6;
        return FWD_OK;
    }
    memset(a, 0, sizeof *a);
    return FWD_E_PARSE;
}

int fwd_addr_to_string(const fwd_addr *a, char *buf, size_t len)
{
    if (a->family == FWD_V4) {
        if (!inet_ntop(AF_INET, a->bytes, buf, (socklen_t)len))
            return FWD_E_BUF;
    } else if (a->family == FWD_V6) {
        if (!inet_ntop(AF_INET6, a->bytes, buf, (socklen_t)len))
            return FWD_E_BUF;
    } else {
        if (len < 2)
            return FWD_E_BUF;
        strcpy(buf, "-");
    }
    return FWD_OK;
}

int fwd_addr_encode(const fwd_addr *a, char *buf, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    int n;

    if (a->family == FWD_V6) {
        int i;
        if (len < 35)
            return FWD_E_BUF;
        buf[0] = '0';
        buf[1] = 'x';
        for (i = 0; i < 16; i++) {
            buf[2 + 2 * i] = hex[a->bytes[i] >> 4];
            buf[3 + 2 * i] = hex[a->bytes[i] & 15];
        }
        buf[34] = '\0';
        return 34;
    }
    if (a->family == FWD_V4) {
        uint32_t v;
        fwd_addr_to_u32(a, &v);
        n = snprintf(buf, len, "%lu", (unsigned long)v);
    } else {
        n = snprintf(buf, len, "0");
    }
    if (n < 0 || (size_t)n >= len)
        return FWD_E_BUF;
    return n;
}

int fwd_addr_decode(fwd_addr *a, const char *s)
{
    size_t n, i;

    memset(a, 0, sizeof *a);
    if (!s || !*s)
        return FWD_E_PARSE;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        n = strlen(s);
        for (i = 0; i < n; i++)
            if (hexval(s[i]) < 0)
                return FWD_E_PARSE;
        if (n == 32) {
            a->family = FWD_V6;
            for (i = 0; i < 16; i++)
                a->bytes[i] =
                    (uint8_t)(hexval(s[2 * i]) << 4 | hexval(s[2 * i + 1]));
            return FWD_OK;
        }
        if (n >= 1 && n <= 8) {
            uint32_t v = 0;
            for (i = 0; i < n; i++)
                v = v << 4 | (uint32_t)hexval(s[i]);
            fwd_addr_from_u32(a, v);
            return FWD_OK;
        }
        return FWD_E_PARSE;
    } else {
        uint32_t v;
        int rc = parse_uint(s, &v, 0xFFFFFFFFu);
        if (rc)
            return rc;
        fwd_addr_from_u32(a, v);
        return FWD_OK;
    }
}

/* ------------------------------------------------------------------ */
/* Command table                                                      */
/* ------------------------------------------------------------------ */
typedef struct cmd_spec {
    int num;
    const char *name;
    const char *schema;
    int minargs;
} cmd_spec;

#define FWD_SCHEMA "Papap"
#define TUN_SCHEMA "TsapapauuMXH" /* mode, transport, pin optional */
#define ROUTE_SCHEMA "auasuu"

static const cmd_spec g_specs[] = {
    {FWD_CMD_PING, "ping", "", 0},
    {FWD_CMD_FWD_ADD, "fwd-add", FWD_SCHEMA, 5},
    {FWD_CMD_FWD_DEL, "fwd-del", "u", 1},
    {FWD_CMD_FWD_MOD, "fwd-mod", "u" FWD_SCHEMA, 6},
    {FWD_CMD_TUN_ADD, "tun-add", TUN_SCHEMA, 9},
    {FWD_CMD_TUN_DEL, "tun-del", "u", 1},
    {FWD_CMD_TUN_MOD, "tun-mod", "u" TUN_SCHEMA, 10},
    {FWD_CMD_ROUTE_ADD, "route-add", ROUTE_SCHEMA, 6},
    {FWD_CMD_ROUTE_DEL, "route-del", ROUTE_SCHEMA, 6},
    {FWD_CMD_ROUTE_MOD, "route-mod", ROUTE_SCHEMA, 6},
    {FWD_CMD_SYSCTL_SET, "sysctl-set", "ss", 2},
    {FWD_CMD_SYSCTL_GET, "sysctl-get", "s", 1},
    {FWD_CMD_LIST_FWD, "list-fwd", "u", 0},
    {FWD_CMD_LIST_TUN, "list-tun", "u", 0},
    {FWD_CMD_LIST_ROUTES, "list-routes", "u", 0},
    {FWD_CMD_LIST_TUN_PEERS, "list-tun-peers", "uu", 1},
};

static const cmd_spec *find_spec(int num)
{
    size_t i;
    for (i = 0; i < sizeof g_specs / sizeof g_specs[0]; i++)
        if (g_specs[i].num == num)
            return &g_specs[i];
    return NULL;
}

const char *fwd_cmd_name(int num)
{
    const cmd_spec *s = find_spec(num);
    return s ? s->name : NULL;
}

int fwd_cmd_by_name(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof g_specs / sizeof g_specs[0]; i++)
        if (strcasecmp(g_specs[i].name, name) == 0)
            return g_specs[i].num;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Argument values                                                    */
/* ------------------------------------------------------------------ */
typedef struct fwd_arg {
    uint32_t u;
    fwd_addr a;
    char s[FWD_SYSCTL_KEY_MAX];
} fwd_arg;

static int valid_str(const char *s, size_t cap)
{
    size_t i;
    if (strlen(s) >= cap)
        return 0;
    for (i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                 (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.' ||
                 c == '@' || c == '/' || c == '+' || c == '=' || c == ',' ||
                 c == '%';
        if (!ok)
            return 0;
    }
    return 1;
}

/* SHA-256 fingerprint: 64 hex digits, ':' and ' ' are ignored. Also used
 * by the TLS code (declared in fwd_internal.h). */
int fwd_parse_pin(const char *text, uint8_t pin[32])
{
    size_t n = 0;
    int hi = -1;

    for (; *text; text++) {
        int v;
        if (*text == ':' || *text == ' ')
            continue;
        if (!isxdigit((unsigned char)*text) || n == 32)
            return -1;
        v = isdigit((unsigned char)*text)
                ? *text - '0'
                : tolower((unsigned char)*text) - 'a' + 10;
        if (hi < 0) {
            hi = v;
        } else {
            pin[n++] = (uint8_t)(hi << 4 | v);
            hi = -1;
        }
    }
    return n == 32 && hi < 0 ? 0 : -1;
}

/* human != 0: accept "tcp"/"udp"/"tun"/"tap" and dotted / colon IPs. */
static int parse_one(char code, const char *tok, int human, fwd_arg *out)
{
    int rc;

    switch (code) {
    case 'u':
        return parse_uint(tok, &out->u, 0xFFFFFFFFu);
    case 'p':
        return parse_uint(tok, &out->u, 65535);
    case 'P':
        if (human && !strcasecmp(tok, "tcp")) {
            out->u = FWD_PROTO_TCP;
            return FWD_OK;
        }
        if (human && !strcasecmp(tok, "udp")) {
            out->u = FWD_PROTO_UDP;
            return FWD_OK;
        }
        rc = parse_uint(tok, &out->u, 2);
        if (rc == FWD_OK && out->u < 1)
            rc = FWD_E_RANGE;
        return rc;
    case 'T':
        if (human && !strcasecmp(tok, "tun")) {
            out->u = FWD_TUN_TUN;
            return FWD_OK;
        }
        if (human && !strcasecmp(tok, "tap")) {
            out->u = FWD_TUN_TAP;
            return FWD_OK;
        }
        rc = parse_uint(tok, &out->u, 2);
        if (rc == FWD_OK && out->u < 1)
            rc = FWD_E_RANGE;
        return rc;
    case 'M':
        if (human && !strcasecmp(tok, "connect")) {
            out->u = FWD_TUN_CONNECT;
            return FWD_OK;
        }
        if (human && !strcasecmp(tok, "listen")) {
            out->u = FWD_TUN_LISTEN;
            return FWD_OK;
        }
        return parse_uint(tok, &out->u, FWD_TUN_LISTEN);
    case 'X': {
        static const char *const names[] = {"auto", "quic", "udp"};
        uint32_t i;
        for (i = 0; human && i < 3; i++)
            if (!strcasecmp(tok, names[i])) {
                out->u = i;
                return FWD_OK;
            }
        return parse_uint(tok, &out->u, FWD_TT_UDP);
    }
    case 'H': {
        /* on the wire 64 hex digits; people may also write ':' or "-" */
        static const char hex[] = "0123456789abcdef";
        uint8_t pin[32];
        size_t i;
        if (!*tok || (human && !strcmp(tok, "-"))) {
            out->s[0] = '\0';
            return FWD_OK;
        }
        if ((!human && strlen(tok) != 64) || fwd_parse_pin(tok, pin) < 0)
            return FWD_E_PARSE;
        for (i = 0; i < 32; i++) {
            out->s[i * 2] = hex[pin[i] >> 4];
            out->s[i * 2 + 1] = hex[pin[i] & 15];
        }
        out->s[64] = '\0';
        return FWD_OK;
    }
    case 'a':
        if (human && fwd_addr_from_string(&out->a, tok) == FWD_OK)
            return FWD_OK;
        return fwd_addr_decode(&out->a, tok);
    case 's':
        if (!valid_str(tok, sizeof out->s))
            return FWD_E_PARSE;
        strcpy(out->s, tok);
        return FWD_OK;
    }
    return FWD_E_INVAL;
}

static int format_one(char code, const fwd_arg *v, char *buf, size_t len)
{
    int n;

    switch (code) {
    case 'u':
    case 'p':
    case 'P':
    case 'T':
    case 'M':
    case 'X':
        n = snprintf(buf, len, "%lu", (unsigned long)v->u);
        return (n < 0 || (size_t)n >= len) ? FWD_E_BUF : n;
    case 'a':
        return fwd_addr_encode(&v->a, buf, len);
    case 'H':
    case 's':
        if (!valid_str(v->s, sizeof v->s))
            return FWD_E_INVAL;
        n = snprintf(buf, len, "%s", v->s);
        return (n < 0 || (size_t)n >= len) ? FWD_E_BUF : n;
    }
    return FWD_E_INVAL;
}

/* ------------------------------------------------------------------ */
/* struct <-> argument vector                                         */
/* ------------------------------------------------------------------ */
static void put_str(fwd_arg *a, const char *s)
{
    snprintf(a->s, sizeof a->s, "%s", s);
}

static void put_fwd(fwd_arg *a, const fwd_forward_spec *s)
{
    a[0].u = s->proto;
    a[1].a = s->laddr;
    a[2].u = s->lport;
    a[3].a = s->raddr;
    a[4].u = s->rport;
}

static int get_fwd(const fwd_arg *a, fwd_forward_spec *s)
{
    s->proto = (uint8_t)a[0].u;
    s->laddr = a[1].a;
    s->lport = (uint16_t)a[2].u;
    s->raddr = a[3].a;
    s->rport = (uint16_t)a[4].u;
    return FWD_OK;
}

static void put_tun(fwd_arg *a, const fwd_tunnel_spec *s)
{
    a[0].u = s->type;
    put_str(&a[1], s->name);
    a[2].a = s->laddr;
    a[3].u = s->lport;
    a[4].a = s->raddr;
    a[5].u = s->rport;
    a[6].a = s->ifaddr;
    a[7].u = s->prefix;
    a[8].u = s->mtu;
    a[9].u = s->mode;
    a[10].u = s->transport;
    a[11].s[0] = '\0';
    if (s->pin[0])
        parse_one('H', s->pin, 1, &a[11]); /* normalise; checked later */
}

static int get_tun(const fwd_arg *a, fwd_tunnel_spec *s)
{
    if (a[7].u > 128 || a[8].u > 65535 || a[9].u > FWD_TUN_LISTEN ||
        a[10].u > FWD_TT_UDP || strlen(a[1].s) >= FWD_IFNAME_MAX ||
        strlen(a[11].s) >= FWD_PIN_MAX)
        return FWD_E_RANGE;
    s->type = (uint8_t)a[0].u;
    strcpy(s->name, a[1].s);
    s->laddr = a[2].a;
    s->lport = (uint16_t)a[3].u;
    s->raddr = a[4].a;
    s->rport = (uint16_t)a[5].u;
    s->ifaddr = a[6].a;
    s->prefix = (uint8_t)a[7].u;
    s->mtu = (uint16_t)a[8].u;
    s->mode = (uint8_t)a[9].u;      /* absent: 0 = connect */
    s->transport = (uint8_t)a[10].u; /* absent: 0 = auto    */
    strcpy(s->pin, a[11].s);
    return FWD_OK;
}

static void put_route(fwd_arg *a, const fwd_route_spec *s)
{
    a[0].a = s->dst;
    a[1].u = s->prefix;
    a[2].a = s->gateway;
    put_str(&a[3], s->ifname);
    a[4].u = s->metric;
    a[5].u = s->table;
}

static int get_route(const fwd_arg *a, fwd_route_spec *s)
{
    if (a[1].u > 128 || strlen(a[3].s) >= FWD_IFNAME_MAX)
        return FWD_E_RANGE;
    s->dst = a[0].a;
    s->prefix = (uint8_t)a[1].u;
    s->gateway = a[2].a;
    strcpy(s->ifname, a[3].s);
    s->metric = a[4].u;
    s->table = a[5].u;
    return FWD_OK;
}

static int cmd_to_args(const fwd_cmd *c, fwd_arg *a)
{
    switch (c->num) {
    case FWD_CMD_PING:
        break;
    case FWD_CMD_FWD_ADD:
        put_fwd(a, &c->u.fwd);
        break;
    case FWD_CMD_FWD_MOD:
        a[0].u = c->id;
        put_fwd(a + 1, &c->u.fwd);
        break;
    case FWD_CMD_TUN_ADD:
        put_tun(a, &c->u.tun);
        break;
    case FWD_CMD_TUN_MOD:
        a[0].u = c->id;
        put_tun(a + 1, &c->u.tun);
        break;
    case FWD_CMD_ROUTE_ADD:
    case FWD_CMD_ROUTE_DEL:
    case FWD_CMD_ROUTE_MOD:
        put_route(a, &c->u.route);
        break;
    case FWD_CMD_SYSCTL_SET:
        put_str(&a[0], c->u.sysctl.key);
        snprintf(a[1].s, sizeof a[1].s, "%s", c->u.sysctl.value);
        break;
    case FWD_CMD_SYSCTL_GET:
        put_str(&a[0], c->u.sysctl.key);
        break;
    case FWD_CMD_FWD_DEL:
    case FWD_CMD_TUN_DEL:
    case FWD_CMD_LIST_FWD:
    case FWD_CMD_LIST_TUN:
    case FWD_CMD_LIST_ROUTES:
        a[0].u = c->id;
        break;
    case FWD_CMD_LIST_TUN_PEERS:
        a[0].u = c->id;
        a[1].u = c->offset;
        break;
    default:
        return FWD_E_INVAL;
    }
    return FWD_OK;
}

static int args_to_cmd(int num, const fwd_arg *a, fwd_cmd *c)
{
    memset(c, 0, sizeof *c);
    c->num = num;
    switch (num) {
    case FWD_CMD_PING:
        return FWD_OK;
    case FWD_CMD_FWD_ADD:
        return get_fwd(a, &c->u.fwd);
    case FWD_CMD_FWD_MOD:
        c->id = a[0].u;
        return get_fwd(a + 1, &c->u.fwd);
    case FWD_CMD_TUN_ADD:
        return get_tun(a, &c->u.tun);
    case FWD_CMD_TUN_MOD:
        c->id = a[0].u;
        return get_tun(a + 1, &c->u.tun);
    case FWD_CMD_ROUTE_ADD:
    case FWD_CMD_ROUTE_DEL:
    case FWD_CMD_ROUTE_MOD:
        return get_route(a, &c->u.route);
    case FWD_CMD_SYSCTL_SET:
        if (strlen(a[1].s) >= FWD_SYSCTL_VAL_MAX)
            return FWD_E_RANGE;
        strcpy(c->u.sysctl.key, a[0].s);
        strcpy(c->u.sysctl.value, a[1].s);
        return FWD_OK;
    case FWD_CMD_SYSCTL_GET:
        strcpy(c->u.sysctl.key, a[0].s);
        return FWD_OK;
    case FWD_CMD_FWD_DEL:
    case FWD_CMD_TUN_DEL:
    case FWD_CMD_LIST_FWD:
    case FWD_CMD_LIST_TUN:
    case FWD_CMD_LIST_ROUTES:
        c->id = a[0].u;
        return FWD_OK;
    case FWD_CMD_LIST_TUN_PEERS:
        c->id = a[0].u;
        c->offset = a[1].u;
        return FWD_OK;
    }
    return FWD_E_INVAL;
}

/* ------------------------------------------------------------------ */
/* Encode / decode                                                    */
/* ------------------------------------------------------------------ */
static int app(char *out, size_t cap, size_t *pos, const char *s)
{
    size_t n = strlen(s);
    if (*pos + n + 1 > cap)
        return FWD_E_BUF;
    memcpy(out + *pos, s, n + 1);
    *pos += n;
    return FWD_OK;
}

static int emit(const cmd_spec *sp, const fwd_arg *args, int nargs, char *out,
                size_t cap)
{
    char tmp[64];
    size_t pos = 0;
    int i, rc;

    if (!out || cap == 0)
        return FWD_E_BUF;
    snprintf(tmp, sizeof tmp, "%d", sp->num);
    if ((rc = app(out, cap, &pos, tmp)) != FWD_OK)
        return rc;
    for (i = 0; i < nargs; i++) {
        char tok[FWD_SYSCTL_KEY_MAX + 8];
        if ((rc = app(out, cap, &pos, ":")) != FWD_OK)
            return rc;
        rc = format_one(sp->schema[i], &args[i], tok, sizeof tok);
        if (rc < 0)
            return rc;
        if ((rc = app(out, cap, &pos, tok)) != FWD_OK)
            return rc;
    }
    return (int)pos;
}

int fwd_cmd_encode(const fwd_cmd *cmd, char *out, size_t outlen)
{
    const cmd_spec *sp = find_spec(cmd->num);
    fwd_arg args[FWD_MAX_ARGS];
    int rc, nargs;

    if (!sp)
        return FWD_E_INVAL;
    memset(args, 0, sizeof args);
    if ((rc = cmd_to_args(cmd, args)) != FWD_OK)
        return rc;
    nargs = (int)strlen(sp->schema);
    /* leave out trailing optional arguments that have their default (no
     * pin, auto transport, connect mode), so such a command reads exactly
     * as before they existed */
    while (nargs > 0 && strchr("MXH", sp->schema[nargs - 1]) &&
           args[nargs - 1].u == 0 && args[nargs - 1].s[0] == '\0')
        nargs--;
    return emit(sp, args, nargs, out, outlen);
}

int fwd_cmd_encode_strs(int num, const char *const *argv, int argc, char *out,
                        size_t outlen)
{
    const cmd_spec *sp = find_spec(num);
    fwd_arg args[FWD_MAX_ARGS];
    fwd_cmd check;
    int i, rc;

    if (!sp)
        return FWD_E_INVAL;
    if (argc < sp->minargs || argc > (int)strlen(sp->schema))
        return FWD_E_PARSE;
    memset(args, 0, sizeof args);
    for (i = 0; i < argc; i++) {
        if (!argv[i])
            return FWD_E_INVAL;
        if ((rc = parse_one(sp->schema[i], argv[i], 1, &args[i])) != FWD_OK)
            return rc;
    }
    rc = emit(sp, args, argc, out, outlen);
    if (rc < 0)
        return rc;
    /* validate what we produced exactly like a receiver would */
    if ((rc = fwd_cmd_decode(out, &check)) != FWD_OK)
        return rc;
    return (int)strlen(out);
}

int fwd_cmd_decode(const char *line, fwd_cmd *cmd)
{
    char buf[FWD_MAX_CMD + 1];
    char *tok[FWD_MAX_ARGS + 2];
    fwd_arg args[FWD_MAX_ARGS];
    const cmd_spec *sp;
    uint32_t num;
    size_t n, slen;
    int ntok = 0, nargs, i, rc;
    char *p, *q;

    if (!line || !cmd)
        return FWD_E_INVAL;
    n = strlen(line);
    if (n == 0 || n > FWD_MAX_CMD)
        return FWD_E_PARSE;
    memcpy(buf, line, n + 1);

    p = buf;
    for (;;) {
        if (ntok >= FWD_MAX_ARGS + 2)
            return FWD_E_PARSE;
        tok[ntok++] = p;
        q = strchr(p, ':');
        if (!q)
            break;
        *q = '\0';
        p = q + 1;
    }

    if ((rc = parse_uint(tok[0], &num, 65535)) != FWD_OK)
        return rc;
    sp = find_spec((int)num);
    if (!sp)
        return FWD_E_PARSE;
    nargs = ntok - 1;
    slen = strlen(sp->schema);
    if (nargs < sp->minargs || (size_t)nargs > slen)
        return FWD_E_PARSE;

    memset(args, 0, sizeof args);
    for (i = 0; i < nargs; i++)
        if ((rc = parse_one(sp->schema[i], tok[i + 1], 0, &args[i])) !=
            FWD_OK)
            return rc;
    return args_to_cmd((int)num, args, cmd);
}

/* ------------------------------------------------------------------ */
/* Responses                                                          */
/* ------------------------------------------------------------------ */
int fwd_response_parse(const char *s, fwd_response *r)
{
    char *e;
    long v;

    if (!s || !r)
        return FWD_E_INVAL;
    memset(r, 0, sizeof *r);
    r->payload = "";

    v = strtol(s, &e, 10);
    if (e == s || *e != ':')
        return FWD_E_PARSE;
    r->cmd = (int)v;
    s = e + 1;

    v = strtol(s, &e, 10);
    if (e == s || v < 0)
        return FWD_E_PARSE;
    r->status = (int)v;
    if (*e == '\0')
        return FWD_OK;
    if (*e != ':')
        return FWD_E_PARSE;
    s = e + 1;

    if (r->status == 0) {
        r->payload = s;
        return FWD_OK;
    }
    v = strtol(s, &e, 10);
    if (e == s)
        return FWD_E_PARSE;
    r->sys_errno = (int)v;
    if (*e == ':')
        snprintf(r->message, sizeof r->message, "%s", e + 1);
    return FWD_OK;
}

/* ------------------------------------------------------------------ */
/* Human readable responses                                           */
/* ------------------------------------------------------------------ */
/*
 * Record layouts of the list responses, one character per wire field:
 *   u  number            z  number, 0 shown as "-"   U  64 bit counter
 *   P  protocol name     T  tunnel type name          s  word, "" as "-"
 *   M  tunnel mode name   X  tunnel transport name    V  transport in use
 *   a  IP address        g  IP address, all zero as "-"
 *   w  IP address, all zero as "*" (wildcard)
 *   p  port, joined to the address column before it ("1.2.3.4:80")
 *   q  like p, but 0 is shown as "*"
 *   /  prefix, joined to the address column before it ("10.0.0.0/8")
 * Every other field starts a new column; cols holds the column titles.
 */
typedef struct list_spec {
    int num;
    const char *schema;
    const char *cols[14];
} list_spec;

static const list_spec g_lists[] = {
    {FWD_CMD_LIST_FWD, "uPapapu",
     {"ID", "PROTO", "LISTEN", "REMOTE", "ACTIVE"}},
    {FWD_CMD_LIST_TUN, "uTsapwqg/zUUMgpXVu",
     {"ID", "TYPE", "IFNAME", "LOCAL", "PEER", "ADDRESS", "MTU", "TX", "RX",
      "MODE", "CURRENT", "TRANSPORT", "VIA", "PEERS"}},
    {FWD_CMD_LIST_ROUTES, "a/gsuu",
     {"DESTINATION", "GATEWAY", "DEV", "METRIC", "TABLE"}},
    {FWD_CMD_LIST_TUN_PEERS, "apVUUu",
     {"PEER", "VIA", "TX", "RX", "IDLE"}},
};

#define DEC_CELL 64
#define DEC_COLS 14

struct outbuf {
    char *p;
    size_t cap, len;
    int overflow;
};

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif
static void put(struct outbuf *o, const char *fmt, ...);

static void put(struct outbuf *o, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (o->overflow)
        return;
    va_start(ap, fmt);
    n = vsnprintf(o->p + o->len, o->cap - o->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->cap - o->len) {
        o->overflow = 1;
        o->p[o->len] = '\0';
        return;
    }
    o->len += (size_t)n;
}

static int addr_cell(const char *tok, const char *zero_as, char *cell)
{
    fwd_addr a;
    size_t i;
    int zero = 1;

    if (fwd_addr_decode(&a, tok) != FWD_OK)
        return FWD_E_PARSE;
    for (i = 0; i < sizeof a.bytes; i++)
        zero &= a.bytes[i] == 0;
    if (zero_as && zero) {
        strcpy(cell, zero_as);
        return FWD_OK;
    }
    return fwd_addr_to_string(&a, cell, DEC_CELL);
}

/* One record (fields f[0..]) -> cells. Returns the number of columns. */
static int record_cells(const char *schema, char *const *f,
                        char cells[][DEC_CELL])
{
    char tmp[DEC_CELL], num[12];
    int c = -1, n;
    size_t i, k;
    uint32_t v;

    for (i = 0; schema[i]; i++) {
        const char *t = f[i];
        char code = schema[i];

        if (code != 'p' && code != 'q' && code != '/' && ++c >= DEC_COLS)
            return FWD_E_PARSE;
        switch (code) {
        case 'u':
        case 'z':
            if (parse_uint(t, &v, 0xFFFFFFFFu) != FWD_OK)
                return FWD_E_PARSE;
            if (code == 'z' && v == 0)
                strcpy(cells[c], "-");
            else
                snprintf(cells[c], DEC_CELL, "%lu", (unsigned long)v);
            break;
        case 'U':
            for (k = 0; t[k]; k++)
                if (t[k] < '0' || t[k] > '9')
                    return FWD_E_PARSE;
            if (k == 0 || k > 20)
                return FWD_E_PARSE;
            strcpy(cells[c], t);
            break;
        case 'P':
        case 'T':
            if (parse_uint(t, &v, 0xFFFFFFFFu) != FWD_OK)
                return FWD_E_PARSE;
            if (code == 'P' && (v == FWD_PROTO_TCP || v == FWD_PROTO_UDP))
                strcpy(cells[c], v == FWD_PROTO_TCP ? "tcp" : "udp");
            else if (code == 'T' && (v == FWD_TUN_TUN || v == FWD_TUN_TAP))
                strcpy(cells[c], v == FWD_TUN_TUN ? "tun" : "tap");
            else
                snprintf(cells[c], DEC_CELL, "%lu", (unsigned long)v);
            break;
        case 's':
            if (*t && !valid_str(t, DEC_CELL))
                return FWD_E_PARSE;
            strcpy(cells[c], *t ? t : "-");
            break;
        case 'M':
            if (parse_uint(t, &v, 0xFFFFFFFFu) != FWD_OK)
                return FWD_E_PARSE;
            if (v == FWD_TUN_CONNECT || v == FWD_TUN_LISTEN)
                strcpy(cells[c], v == FWD_TUN_LISTEN ? "listen" : "connect");
            else
                snprintf(cells[c], DEC_CELL, "%lu", (unsigned long)v);
            break;
        case 'X':
        case 'V': {
            static const char *const tt[] = {"auto", "quic", "udp"};
            static const char *const via[] = {"-", "quic", "udp",
                                              "rejected"};
            if (parse_uint(t, &v, 0xFFFFFFFFu) != FWD_OK)
                return FWD_E_PARSE;
            if (code == 'X' && v <= FWD_TT_UDP)
                strcpy(cells[c], tt[v]);
            else if (code == 'V' && v <= FWD_VIA_REJECTED)
                strcpy(cells[c], via[v]);
            else
                snprintf(cells[c], DEC_CELL, "%lu", (unsigned long)v);
            break;
        }
        case 'a':
        case 'g':
        case 'w':
            if (addr_cell(t,
                          code == 'g' ? "-" : code == 'w' ? "*" : NULL,
                          cells[c]) != FWD_OK)
                return FWD_E_PARSE;
            break;
        case 'p':
        case 'q':
        case '/':
            if (c < 0 ||
                parse_uint(t, &v, code == '/' ? 128 : 65535) != FWD_OK)
                return FWD_E_PARSE;
            if (strcmp(cells[c], "-") == 0)
                break; /* no address, nothing to qualify */
            if (code == 'q' && v == 0)
                snprintf(num, sizeof num, "*");
            else
                snprintf(num, sizeof num, "%lu", (unsigned long)v);
            if (code == '/')
                n = snprintf(tmp, sizeof tmp, "%s/%s", cells[c], num);
            else if (strchr(cells[c], ':'))
                n = snprintf(tmp, sizeof tmp, "[%s]:%s", cells[c], num);
            else
                n = snprintf(tmp, sizeof tmp, "%s:%s", cells[c], num);
            if (n < 0 || (size_t)n >= sizeof tmp)
                return FWD_E_PARSE;
            strcpy(cells[c], tmp);
            break;
        default:
            return FWD_E_INVAL;
        }
    }
    return c + 1;
}

/* Columns padded to width, the last one not (no trailing blanks). */
static void put_row(struct outbuf *o, const char *const *cells,
                    const size_t *width, int ncols)
{
    int c;

    for (c = 0; c + 1 < ncols; c++)
        put(o, "%-*s  ", (int)width[c], cells[c]);
    put(o, "%s\n", cells[ncols - 1]);
}

static int decode_list(const list_spec *ls, const char *payload,
                       struct outbuf *o)
{
    char buf[FWD_MAX_RESP + 1];
    char *tok[FWD_MAX_RESP / 2 + 2];
    char cells[DEC_COLS][DEC_CELL];
    size_t width[DEC_COLS];
    size_t ntok = 0, nf = strlen(ls->schema), r;
    uint32_t total, count;
    int ncols = 0, c;
    char *p;

    if (strlen(payload) >= sizeof buf)
        return FWD_E_PARSE;
    strcpy(buf, payload);
    for (p = buf;; p++) {
        tok[ntok++] = p;
        if (!(p = strchr(p, ':')))
            break;
        *p = '\0';
        if (ntok == sizeof tok / sizeof tok[0])
            return FWD_E_PARSE;
    }
    if (ntok < 2 || parse_uint(tok[0], &total, 0xFFFFFFFFu) != FWD_OK ||
        parse_uint(tok[1], &count, 0xFFFFFFFFu) != FWD_OK ||
        ntok != 2 + (size_t)count * nf)
        return FWD_E_PARSE;
    if (count == 0) {
        if (total == 0)
            put(o, "no entries\n");
        else
            put(o, "no entries at this offset (%lu in total)\n",
                (unsigned long)total);
        return FWD_OK;
    }

    /* pass 1: validate and measure, pass 2: print */
    for (c = 0; c < DEC_COLS; c++)
        width[c] = ls->cols[c] ? strlen(ls->cols[c]) : 0;
    for (r = 0; r < count; r++) {
        if ((ncols = record_cells(ls->schema, &tok[2 + r * nf], cells)) < 0)
            return FWD_E_PARSE;
        for (c = 0; c < ncols; c++)
            if (strlen(cells[c]) > width[c])
                width[c] = strlen(cells[c]);
    }
    put_row(o, ls->cols, width, ncols);
    for (r = 0; r < count; r++) {
        const char *row[DEC_COLS];
        record_cells(ls->schema, &tok[2 + r * nf], cells);
        for (c = 0; c < ncols; c++)
            row[c] = cells[c];
        put_row(o, row, width, ncols);
    }
    if (count < total)
        put(o, "(%lu of %lu entries, page with the offset argument)\n",
            (unsigned long)count, (unsigned long)total);
    return FWD_OK;
}

int fwd_response_decode(const char *text, char *out, size_t outlen)
{
    struct outbuf o;
    fwd_response r;
    const char *name;
    size_t i;
    uint32_t id;
    char *e;
    int rc = FWD_OK;

    if (!text || !out || outlen == 0)
        return FWD_E_INVAL;
    out[0] = '\0';
    o.p = out;
    o.cap = outlen;
    o.len = 0;
    o.overflow = 0;
    if (fwd_response_parse(text, &r) != FWD_OK)
        return FWD_E_PARSE;
    name = fwd_cmd_name(r.cmd);

    if (r.status != 0) {
        put(&o, "%s%serror %d (%s)", name ? name : "", name ? ": " : "",
            r.status, fwd_strerror(-r.status));
        if (r.message[0])
            put(&o, ": %s", r.message);
        if (r.sys_errno)
            put(&o, " [errno %d]", r.sys_errno);
        put(&o, "\n");
    } else {
        switch (r.cmd) {
        case FWD_CMD_FWD_ADD:
            if (parse_uint(r.payload, &id, 0xFFFFFFFFu) != FWD_OK)
                return FWD_E_PARSE;
            put(&o, "ok, id %lu\n", (unsigned long)id);
            break;
        case FWD_CMD_TUN_ADD:
            id = (uint32_t)strtoul(r.payload, &e, 10);
            if (e == r.payload || (*e && *e != ':') ||
                (*e == ':' && !valid_str(e + 1, FWD_IFNAME_MAX)))
                return FWD_E_PARSE;
            put(&o, "ok, id %lu", (unsigned long)id);
            if (*e == ':' && e[1])
                put(&o, ", interface %s", e + 1);
            put(&o, "\n");
            break;
        case FWD_CMD_SYSCTL_GET:
            if (!valid_str(r.payload, FWD_SYSCTL_VAL_MAX))
                return FWD_E_PARSE;
            put(&o, "%s\n", r.payload);
            break;
        case FWD_CMD_LIST_FWD:
        case FWD_CMD_LIST_TUN:
        case FWD_CMD_LIST_ROUTES:
        case FWD_CMD_LIST_TUN_PEERS:
            for (i = 0; i < sizeof g_lists / sizeof g_lists[0]; i++)
                if (g_lists[i].num == r.cmd)
                    rc = decode_list(&g_lists[i], r.payload, &o);
            break;
        default:
            if (r.payload[0])
                return FWD_E_PARSE;
            put(&o, "ok\n");
            break;
        }
    }
    if (rc != FWD_OK)
        return rc;
    return o.overflow ? FWD_E_BUF : (int)o.len;
}
