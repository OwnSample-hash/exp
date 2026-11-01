/*
 * fwd_exec.c - executes an encoded command string and builds the response.
 */
#include "fwd_internal.h"

#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Error responses                                                    */
/* ------------------------------------------------------------------ */
static int leading_num(const char *s)
{
    char *e;
    long v = strtol(s, &e, 10);
    return (e == s || *e != ':' || v < 0 || v > 65535) ? -1 : (int)v;
}

static int fmt_error(fwd_ctx *ctx, int cmd, int rc, char *resp, size_t cap)
{
    char msg[sizeof ctx->last_msg];
    const char *src = ctx->last_msg[0] ? ctx->last_msg : fwd_strerror(rc);
    size_t i;
    int n;

    snprintf(msg, sizeof msg, "%s", src);
    for (i = 0; msg[i]; i++)
        if (msg[i] == ':' || !isprint((unsigned char)msg[i]))
            msg[i] = ' ';
    n = snprintf(resp, cap, "%d:%d:%d:%s", cmd, -rc, ctx->last_errno, msg);
    return (n < 0 || (size_t)n >= cap) ? FWD_E_BUF : n;
}

/* ------------------------------------------------------------------ */
/* Listing                                                            */
/* ------------------------------------------------------------------ */
struct listbuf {
    char *body;
    size_t cap, used;
    uint32_t off, seen, emitted;
    int full;
};

static void lb_add(struct listbuf *lb, const char *rec)
{
    uint32_t idx = lb->seen++;
    size_t n = strlen(rec);

    if (idx < lb->off || lb->full)
        return;
    if (lb->used + n + 2 > lb->cap) {
        lb->full = 1;
        return;
    }
    lb->body[lb->used++] = ':';
    memcpy(lb->body + lb->used, rec, n + 1);
    lb->used += n;
    lb->emitted++;
}

static void enc(const fwd_addr *a, char *out) /* out: >= 40 bytes */
{
    if (fwd_addr_encode(a, out, 40) < 0)
        strcpy(out, "0");
}

static int cb_fwd(const fwd_forward_info *i, void *ud)
{
    char la[40], ra[40], rec[160];
    enc(&i->spec.laddr, la);
    enc(&i->spec.raddr, ra);
    snprintf(rec, sizeof rec, "%u:%u:%s:%u:%s:%u:%u", i->id, i->spec.proto,
             la, i->spec.lport, ra, i->spec.rport, i->active);
    lb_add(ud, rec);
    return 0;
}

static int cb_tun(const fwd_tunnel_info *i, void *ud)
{
    char la[40], ra[40], ia[40], pa[40], rec[300];
    enc(&i->spec.laddr, la);
    enc(&i->spec.raddr, ra);
    enc(&i->spec.ifaddr, ia);
    enc(&i->peer, pa);
    snprintf(rec, sizeof rec,
             "%u:%u:%s:%s:%u:%s:%u:%s:%u:%u:%llu:%llu:%u:%s:%u:%u:%u:%u",
             i->id,
             i->spec.type, i->spec.name, la, i->spec.lport, ra, i->spec.rport,
             ia, i->spec.prefix, i->spec.mtu,
             (unsigned long long)i->tx_packets,
             (unsigned long long)i->rx_packets, i->spec.mode, pa,
             i->peer_port, i->spec.transport, i->via, i->peers);
    lb_add(ud, rec);
    return 0;
}

static int cb_peer(const fwd_tunnel_peer *p, void *ud)
{
    char pa[40], rec[160];
    enc(&p->addr, pa);
    snprintf(rec, sizeof rec, "%s:%u:%u:%llu:%llu:%u", pa, p->port, p->via,
             (unsigned long long)p->tx_packets,
             (unsigned long long)p->rx_packets, p->idle_ms / 1000);
    lb_add(ud, rec);
    return 0;
}

static int cb_route(const fwd_route_spec *r, void *ud)
{
    char da[40], ga[40], rec[200];
    enc(&r->dst, da);
    enc(&r->gateway, ga);
    snprintf(rec, sizeof rec, "%s:%u:%s:%s:%u:%u", da, r->prefix, ga,
             r->ifname, r->metric, r->table);
    lb_add(ud, rec);
    return 0;
}

static int do_list(fwd_ctx *ctx, const fwd_cmd *c, char *payload, size_t cap)
{
    struct listbuf lb;
    int rc;
    char *body = FWD_MALLOC(FWD_MAX_RESP);

    if (!body)
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
    body[0] = '\0';
    lb.body = body;
    lb.cap = FWD_MAX_RESP - 64;
    lb.used = 0;
    lb.off = c->num == FWD_CMD_LIST_TUN_PEERS ? c->offset : c->id;
    lb.seen = lb.emitted = 0;
    lb.full = 0;

    switch (c->num) {
    case FWD_CMD_LIST_FWD:
        rc = fwd_forward_list(ctx, cb_fwd, &lb);
        break;
    case FWD_CMD_LIST_TUN:
        rc = fwd_tunnel_list(ctx, cb_tun, &lb);
        break;
    case FWD_CMD_LIST_TUN_PEERS:
        rc = fwd_tunnel_peers(ctx, c->id, cb_peer, &lb);
        break;
    default:
        rc = fwd_route_list(ctx, cb_route, &lb);
        break;
    }
    if (rc == FWD_OK) {
        int n = snprintf(payload, cap, ":%u:%u%s", lb.seen, lb.emitted, body);
        if (n < 0 || (size_t)n >= cap)
            rc = fwd_err(ctx, FWD_E_BUF, "response too large");
    }
    FWD_FREE(body);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                           */
/* ------------------------------------------------------------------ */
static int run_cmd(fwd_ctx *ctx, const fwd_cmd *c, char *payload, size_t cap)
{
    uint32_t id = 0;
    int rc;

    switch (c->num) {
    case FWD_CMD_PING:
        return FWD_OK;

    case FWD_CMD_FWD_ADD:
        if ((rc = fwd_forward_add(ctx, &c->u.fwd, &id)) == FWD_OK)
            snprintf(payload, cap, ":%u", id);
        return rc;
    case FWD_CMD_FWD_DEL:
        return fwd_forward_del(ctx, c->id);
    case FWD_CMD_FWD_MOD:
        return fwd_forward_mod(ctx, c->id, &c->u.fwd);

    case FWD_CMD_TUN_ADD:
        if ((rc = fwd_tunnel_add(ctx, &c->u.tun, &id)) == FWD_OK) {
            fwd_tunnel_info info;
            if (fwd_tunnel_get(ctx, id, &info) == FWD_OK)
                snprintf(payload, cap, ":%u:%s", id, info.spec.name);
            else
                snprintf(payload, cap, ":%u", id);
        }
        return rc;
    case FWD_CMD_TUN_DEL:
        return fwd_tunnel_del(ctx, c->id);
    case FWD_CMD_TUN_MOD:
        return fwd_tunnel_mod(ctx, c->id, &c->u.tun);

    case FWD_CMD_ROUTE_ADD:
        return fwd_route_add(ctx, &c->u.route);
    case FWD_CMD_ROUTE_DEL:
        return fwd_route_del(ctx, &c->u.route);
    case FWD_CMD_ROUTE_MOD:
        return fwd_route_mod(ctx, &c->u.route);

    case FWD_CMD_SYSCTL_SET:
        return fwd_sysctl_set(ctx, c->u.sysctl.key, c->u.sysctl.value);
    case FWD_CMD_SYSCTL_GET: {
        char val[FWD_SYSCTL_VAL_MAX];
        if ((rc = fwd_sysctl_get(ctx, c->u.sysctl.key, val, sizeof val)) ==
            FWD_OK)
            snprintf(payload, cap, ":%s", val);
        return rc;
    }

    case FWD_CMD_LIST_FWD:
    case FWD_CMD_LIST_TUN:
    case FWD_CMD_LIST_ROUTES:
    case FWD_CMD_LIST_TUN_PEERS:
        return do_list(ctx, c, payload, cap);
    }
    return fwd_err(ctx, FWD_E_PARSE, "unknown command %d", c->num);
}

int fwd_execute(fwd_ctx *ctx, const char *line, char *resp, size_t cap)
{
    fwd_cmd c;
    char *payload;
    int rc, n;

    if (!ctx || !line || !resp || cap < 64)
        return FWD_E_BUF;
    ctx->last_msg[0] = '\0';
    ctx->last_errno = 0;

    rc = fwd_cmd_decode(line, &c);
    if (rc != FWD_OK)
        return fmt_error(ctx, leading_num(line), rc, resp, cap);

    payload = FWD_MALLOC(FWD_MAX_RESP);
    if (!payload) {
        fwd_err(ctx, FWD_E_NOMEM, "out of memory");
        return fmt_error(ctx, c.num, FWD_E_NOMEM, resp, cap);
    }
    payload[0] = '\0';
    rc = run_cmd(ctx, &c, payload, FWD_MAX_RESP);
    if (rc != FWD_OK) {
        FWD_FREE(payload);
        return fmt_error(ctx, c.num, rc, resp, cap);
    }
    n = snprintf(resp, cap, "%d:0%s", c.num, payload);
    FWD_FREE(payload);
    if (n < 0 || (size_t)n >= cap)
        return FWD_E_BUF;
    return n;
}
