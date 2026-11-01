/*
 * fwd_core.c - context lifetime, error reporting, event loop, sysctl.
 */
#include "fwd_internal.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/* Errors                                                             */
/* ------------------------------------------------------------------ */
const char *fwd_strerror(int rc)
{
    switch (rc) {
    case FWD_OK:
        return "success";
    case FWD_E_INVAL:
        return "invalid argument";
    case FWD_E_PARSE:
        return "malformed command";
    case FWD_E_NOSYS:
        return "function not available";
    case FWD_E_SYS:
        return "system call failed";
    case FWD_E_NOENT:
        return "no such entry";
    case FWD_E_EXIST:
        return "entry already exists";
    case FWD_E_NOMEM:
        return "out of memory";
    case FWD_E_RANGE:
        return "value out of range";
    case FWD_E_BUF:
        return "buffer too small";
    case FWD_E_AUTH:
        return "authentication failed";
    case FWD_E_TIMEOUT:
        return "timeout";
    case FWD_E_TLS:
        return "TLS handshake or certificate verification failed";
    }
    return "unknown error";
}

int fwd_err(fwd_ctx *ctx, int rc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->last_msg, sizeof ctx->last_msg, fmt, ap);
    va_end(ap);
    ctx->last_errno = 0;
    return rc;
}

int fwd_err_sys(fwd_ctx *ctx, const char *what)
{
    int e = errno;
    snprintf(ctx->last_msg, sizeof ctx->last_msg, "%s: %s", what,
             strerror(e));
    ctx->last_errno = e;
    errno = e;
    return e == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
}

const char *fwd_last_error(const fwd_ctx *ctx, int *sys_errno)
{
    if (sys_errno)
        *sys_errno = ctx->last_errno;
    return ctx->last_msg;
}

/* ------------------------------------------------------------------ */
/* Address helpers                                                    */
/* ------------------------------------------------------------------ */
static size_t addr_len(const fwd_addr *a)
{
    return a->family == FWD_V6 ? 16 : a->family == FWD_V4 ? 4 : 0;
}

int fwd_addr_usable(const fwd_addr *a)
{
    if (a->family == FWD_V4)
        return FWD_ENABLE_IPV4;
    if (a->family == FWD_V6)
        return FWD_ENABLE_IPV6;
    return 0;
}

int fwd_addr_equal(const fwd_addr *a, const fwd_addr *b)
{
    return a->family == b->family &&
           memcmp(a->bytes, b->bytes, addr_len(a)) == 0;
}

int fwd_addr_is_zero(const fwd_addr *a)
{
    size_t i;
    for (i = 0; i < addr_len(a); i++)
        if (a->bytes[i])
            return 0;
    return 1;
}

int fwd_sockaddr_to_addr(const struct sockaddr_storage *ss, fwd_addr *a,
                           uint16_t *port)
{
    memset(a, 0, sizeof *a);
#if FWD_ENABLE_IPV4
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *s = (const struct sockaddr_in *)ss;
        a->family = FWD_V4;
        memcpy(a->bytes, &s->sin_addr, 4);
        *port = ntohs(s->sin_port);
        return FWD_OK;
    }
#endif
#if FWD_ENABLE_IPV6
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *s = (const struct sockaddr_in6 *)ss;
        a->family = FWD_V6;
        memcpy(a->bytes, &s->sin6_addr, 16);
        *port = ntohs(s->sin6_port);
        return FWD_OK;
    }
#endif
    return FWD_E_INVAL;
}


int fwd_addr_to_sockaddr(const fwd_addr *a, uint16_t port,
                         struct sockaddr_storage *ss, socklen_t *len)
{
    memset(ss, 0, sizeof *ss);
#if FWD_ENABLE_IPV4
    if (a->family == FWD_V4) {
        struct sockaddr_in *s = (struct sockaddr_in *)ss;
        s->sin_family = AF_INET;
        s->sin_port = htons(port);
        memcpy(&s->sin_addr, a->bytes, 4);
        *len = sizeof *s;
        return FWD_OK;
    }
#endif
#if FWD_ENABLE_IPV6
    if (a->family == FWD_V6) {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)ss;
        s->sin6_family = AF_INET6;
        s->sin6_port = htons(port);
        memcpy(&s->sin6_addr, a->bytes, 16);
        *len = sizeof *s;
        return FWD_OK;
    }
#endif
    return FWD_E_INVAL;
}

/* ------------------------------------------------------------------ */
/* Context                                                            */
/* ------------------------------------------------------------------ */
void fwd_config_default(fwd_config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->auto_sysctl = 1;
    cfg->max_conns = 1024;
    cfg->udp_timeout_ms = 60000;
}

fwd_ctx *fwd_create(const fwd_config *cfg)
{
    fwd_ctx *ctx = FWD_CALLOC(1, sizeof *ctx);
    if (!ctx)
        return NULL;
    ctx->pkt = FWD_MALLOC(FWD_PKT_BUF);
    if (!ctx->pkt) {
        FWD_FREE(ctx);
        return NULL;
    }
    if (cfg)
        ctx->cfg = *cfg;
    else
        fwd_config_default(&ctx->cfg);
    ctx->cfg.ctl_token[sizeof ctx->cfg.ctl_token - 1] = '\0';
    ctx->ctl_fd = -1;
    ctx->nl_fd = -1;
    ctx->next_fwd_id = 1;
    ctx->next_tun_id = 1;
    return ctx;
}

void fwd_destroy(fwd_ctx *ctx)
{
    if (!ctx)
        return;
    fwd_ctl_close(ctx);
    fwd_ctl_close_quic(ctx);
    fwd_forward_free_all(ctx);
    fwd_tun_free_all(ctx);
    fwd_tunq_ctx_free(ctx);
    fwd_nl_close(ctx);
    FWD_FREE(ctx->pfds);
    FWD_FREE(ctx->pents);
    FWD_FREE(ctx->pkt);
    FWD_FREE(ctx);
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */
int fwd_pollset_add(fwd_ctx *ctx, int fd, short events, int kind, void *obj)
{
    if (ctx->pn == ctx->pcap) {
        size_t ncap = ctx->pcap ? ctx->pcap * 2 : 64;
        struct pollfd *np = FWD_REALLOC(ctx->pfds, ncap * sizeof *np);
        struct fwd_pollent *ne;
        if (!np)
            return FWD_E_NOMEM;
        ctx->pfds = np;
        ne = FWD_REALLOC(ctx->pents, ncap * sizeof *ne);
        if (!ne)
            return FWD_E_NOMEM;
        ctx->pents = ne;
        ctx->pcap = ncap;
    }
    ctx->pfds[ctx->pn].fd = fd;
    ctx->pfds[ctx->pn].events = events;
    ctx->pfds[ctx->pn].revents = 0;
    ctx->pents[ctx->pn].kind = kind;
    ctx->pents[ctx->pn].obj = obj;
    ctx->pn++;
    return FWD_OK;
}

int fwd_run(fwd_ctx *ctx, int timeout_ms)
{
    size_t i, n;
    int ready;

    ctx->pn = 0;
    ctx->in_run = 1;
    fwd_ctl_collect(ctx);
    fwd_quic_collect(ctx);
    fwd_forward_collect(ctx);
    fwd_tun_collect(ctx);
    timeout_ms = fwd_quic_timeout(ctx, timeout_ms);
    timeout_ms = fwd_tun_timeout(ctx, timeout_ms);

    ready = fwd_sys_poll(ctx->pfds, (nfds_t)ctx->pn, timeout_ms);
    if (ready < 0) {
        int e = errno;
        ctx->in_run = 0;
        if (e == EINTR)
            return 0;
        errno = e;
        return fwd_err_sys(ctx, "poll");
    }

    n = ctx->pn;
    for (i = 0; i < n; i++) {
        short rev = ctx->pfds[i].revents;
        const struct fwd_pollent *e = &ctx->pents[i];
        if (!rev)
            continue;
        switch (e->kind) {
        case FWD_PK_CTL:
            fwd_ctl_dispatch(ctx, rev);
            break;
        case FWD_PK_LISTEN:
        case FWD_PK_TCP_A:
        case FWD_PK_TCP_B:
        case FWD_PK_UDP_UP:
            fwd_forward_dispatch(ctx, e, rev);
            break;
        case FWD_PK_TUN_DEV:
        case FWD_PK_TUN_SOCK:
            fwd_tun_dispatch(ctx, e, rev);
            break;
        }
    }

    /* QUIC also has timers (retransmission, idle), so it is serviced on
     * every iteration, not only when its socket is readable. */
    fwd_quic_service(ctx);
    fwd_tun_service(ctx);
    fwd_forward_housekeeping(ctx, fwd_sys_now_ms());
    ctx->in_run = 0;
    fwd_forward_reap(ctx);
    fwd_tun_reap(ctx);
    return ready;
}

int fwd_run_forever(fwd_ctx *ctx)
{
    ctx->stop = 0;
    while (!ctx->stop) {
        int rc = fwd_run(ctx, 500);
        if (rc < 0)
            return rc;
    }
    return FWD_OK;
}

void fwd_stop(fwd_ctx *ctx)
{
    ctx->stop = 1;
}

/* ------------------------------------------------------------------ */
/* sysctl (/proc/sys)                                                 */
/* ------------------------------------------------------------------ */
#if FWD_ENABLE_SYSCTL

static int sysctl_path(fwd_ctx *ctx, const char *key, char *path, size_t cap)
{
    size_t i, n = strlen(key);
    const char *root = "/proc/sys/";

    if (n == 0 || n >= FWD_SYSCTL_KEY_MAX)
        return fwd_err(ctx, FWD_E_INVAL, "bad sysctl key length");
    if (strncmp(key, "net.ipv4.", 9) && strncmp(key, "net.ipv6.", 9))
        return fwd_err(ctx, FWD_E_INVAL,
                       "only net.ipv4.* and net.ipv6.* keys are allowed");
    if (key[n - 1] == '.' || strstr(key, ".."))
        return fwd_err(ctx, FWD_E_INVAL, "bad sysctl key");
    if (strlen(root) + n + 1 > cap)
        return fwd_err(ctx, FWD_E_BUF, "path too long");
    strcpy(path, root);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)key[i];
        if (c == '.')
            path[strlen(root) + i] = '/';
        else if (isalnum(c) || c == '_' || c == '-')
            path[strlen(root) + i] = (char)c;
        else
            return fwd_err(ctx, FWD_E_INVAL, "bad character in sysctl key");
    }
    path[strlen(root) + n] = '\0';
    return FWD_OK;
}

int fwd_sysctl_get(fwd_ctx *ctx, const char *key, char *value, size_t len)
{
    char path[160], tmp[FWD_SYSCTL_VAL_MAX + 1];
    ssize_t n;
    int fd, rc, e;

    if (!key || !value || len == 0)
        return fwd_err(ctx, FWD_E_INVAL, "bad argument");
    if ((rc = sysctl_path(ctx, key, path, sizeof path)) != FWD_OK)
        return rc;
    fd = fwd_sys_open(path, O_RDONLY);
    if (fd < 0)
        return fwd_err_sys(ctx, key);
    n = fwd_sys_read(fd, tmp, FWD_SYSCTL_VAL_MAX);
    e = errno;
    fwd_sys_close(fd);
    if (n < 0) {
        errno = e;
        return fwd_err_sys(ctx, key);
    }
    while (n > 0 && (tmp[n - 1] == '\n' || tmp[n - 1] == ' '))
        n--;
    tmp[n] = '\0';
    if ((size_t)n + 1 > len)
        return fwd_err(ctx, FWD_E_BUF, "value buffer too small");
    memcpy(value, tmp, (size_t)n + 1);
    return FWD_OK;
}

int fwd_sysctl_set(fwd_ctx *ctx, const char *key, const char *value)
{
    char path[160], tmp[FWD_SYSCTL_VAL_MAX + 2];
    size_t i, n;
    ssize_t w;
    int fd, rc, e;

    if (!key || !value)
        return fwd_err(ctx, FWD_E_INVAL, "bad argument");
    n = strlen(value);
    if (n == 0 || n >= FWD_SYSCTL_VAL_MAX)
        return fwd_err(ctx, FWD_E_INVAL, "bad sysctl value length");
    for (i = 0; i < n; i++)
        if (!isalnum((unsigned char)value[i]) && value[i] != '_' &&
            value[i] != '-')
            return fwd_err(ctx, FWD_E_INVAL, "bad character in value");
    if ((rc = sysctl_path(ctx, key, path, sizeof path)) != FWD_OK)
        return rc;
    fd = fwd_sys_open(path, O_WRONLY);
    if (fd < 0)
        return fwd_err_sys(ctx, key);
    snprintf(tmp, sizeof tmp, "%s\n", value);
    w = fwd_sys_write(fd, tmp, strlen(tmp));
    e = errno;
    fwd_sys_close(fd);
    if (w < 0) {
        errno = e;
        return fwd_err_sys(ctx, key);
    }
    return FWD_OK;
}

int fwd_sysctl_enable_forwarding(fwd_ctx *ctx, int family)
{
    const char *key;
    char cur[FWD_SYSCTL_VAL_MAX];

    if (!ctx->cfg.auto_sysctl)
        return FWD_OK;
    key = family == FWD_V6 ? "net.ipv6.conf.all.forwarding"
                           : "net.ipv4.ip_forward";
    if (fwd_sysctl_get(ctx, key, cur, sizeof cur) == FWD_OK &&
        strcmp(cur, "1") == 0)
        return FWD_OK;
    return fwd_sysctl_set(ctx, key, "1");
}

#else /* !FWD_ENABLE_SYSCTL */

int fwd_sysctl_get(fwd_ctx *ctx, const char *key, char *value, size_t len)
{
    (void)key, (void)value, (void)len;
    return fwd_err(ctx, FWD_E_NOSYS, "sysctl support disabled");
}

int fwd_sysctl_set(fwd_ctx *ctx, const char *key, const char *value)
{
    (void)key, (void)value;
    return fwd_err(ctx, FWD_E_NOSYS, "sysctl support disabled");
}

int fwd_sysctl_enable_forwarding(fwd_ctx *ctx, int family)
{
    (void)ctx, (void)family;
    return FWD_OK; /* nothing we can do; kernel setup is up to the host */
}

#endif
