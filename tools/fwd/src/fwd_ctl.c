/*
 * fwd_ctl.c - UDP control server and a small blocking client. The QUIC
 * transport (fwd_quic.c) shares fwd_ctl_handle().
 */
#include "fwd_internal.h"

#if FWD_ENABLE_CTL

static int close_keep_errno(int fd)
{
    int e = errno;
    fwd_sys_close(fd);
    errno = e;
    return -1;
}

int fwd_ctl_listen(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    int fd, one = 1;

    if (!ctx || !addr)
        return FWD_E_INVAL;
    if (ctx->ctl_fd >= 0)
        return fwd_err(ctx, FWD_E_EXIST, "control channel already open");
    if (!fwd_addr_usable(addr) ||
        fwd_addr_to_sockaddr(addr, port, &ss, &sl) != FWD_OK)
        return fwd_err(ctx, FWD_E_INVAL, "invalid control address");
    fd = fwd_sys_socket(ss.ss_family, SOCK_DGRAM, 0);
    if (fd < 0)
        return fwd_err_sys(ctx, "socket");
#if FWD_ENABLE_IPV6 && defined(IPV6_V6ONLY)
    if (ss.ss_family == AF_INET6)
        fwd_sys_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
    (void)one;
    if (fwd_sys_set_nonblock(fd) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "set non-blocking");
    }
    if (fwd_sys_bind(fd, (struct sockaddr *)&ss, sl) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "bind");
    }
    ctx->ctl_fd = fd;
    return FWD_OK;
}

void fwd_ctl_close(fwd_ctx *ctx)
{
    if (ctx->ctl_fd >= 0)
        fwd_sys_close(ctx->ctl_fd);
    ctx->ctl_fd = -1;
}

void fwd_ctl_collect(fwd_ctx *ctx)
{
    if (ctx->ctl_fd >= 0)
        fwd_pollset_add(ctx, ctx->ctl_fd, POLLIN, FWD_PK_CTL, NULL);
}

/* Compare "<token>@" at the start of line without early exit; on success
 * *rest points behind the '@'. */
static int token_ok(const char *tok, const char *line, const char **rest)
{
    size_t tl = strlen(tok), i;
    unsigned diff = 0;

    if (strlen(line) <= tl)
        return 0;
    for (i = 0; i < tl; i++)
        diff |= (unsigned char)tok[i] ^ (unsigned char)line[i];
    diff |= (unsigned char)line[tl] ^ (unsigned char)'@';
    *rest = line + tl + 1;
    return diff == 0;
}

/* One request of either transport: strip trailing whitespace, check the
 * token, execute. in must be NUL terminated and writable. Returns the
 * response length or a negative error. */
int fwd_ctl_handle(fwd_ctx *ctx, char *in, size_t n, char *out, size_t outlen)
{
    const char *line = in;

    while (n > 0 && (in[n - 1] == '\n' || in[n - 1] == '\r' ||
                     in[n - 1] == ' '))
        in[--n] = '\0';

    if (ctx->cfg.ctl_token[0] && !token_ok(ctx->cfg.ctl_token, in, &line)) {
        ctx->last_msg[0] = '\0';
        fwd_err(ctx, FWD_E_AUTH, "authentication failed");
        return snprintf(out, outlen, "-1:%d:0:authentication failed",
                        -FWD_E_AUTH);
    }
    return fwd_execute(ctx, line, out, outlen);
}

void fwd_ctl_dispatch(fwd_ctx *ctx, short revents)
{
    int i;

    if (ctx->ctl_fd < 0 || !(revents & (POLLIN | POLLERR)))
        return;
    for (i = 0; i < 16; i++) {
        char in[FWD_MAX_CMD + 2];
        char out[FWD_MAX_RESP + 1];
        struct sockaddr_storage from;
        socklen_t fl = sizeof from;
        ssize_t n;
        int rn;

        memset(&from, 0, sizeof from);
        n = fwd_sys_recvfrom(ctx->ctl_fd, in, sizeof in - 1, 0,
                             (struct sockaddr *)&from, &fl);
        if (n < 0)
            break;
        in[n] = '\0';
        rn = fwd_ctl_handle(ctx, in, (size_t)n, out, sizeof out);
        if (rn >= 0)
            fwd_sys_sendto(ctx->ctl_fd, out, (size_t)rn, 0,
                           (struct sockaddr *)&from, fl);
    }
}

int fwd_client_request(const fwd_addr *server, uint16_t port, const char *cmd,
                       char *resp, size_t resplen, int timeout_ms)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    struct pollfd p;
    ssize_t n;
    int fd, rc;

    if (!server || !cmd || !resp || resplen < 2)
        return FWD_E_INVAL;
    if (!fwd_addr_usable(server) ||
        fwd_addr_to_sockaddr(server, port, &ss, &sl) != FWD_OK)
        return FWD_E_INVAL;
    fd = fwd_sys_socket(ss.ss_family, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
    if (fwd_sys_connect(fd, (struct sockaddr *)&ss, sl) < 0 ||
        fwd_sys_send(fd, cmd, strlen(cmd), 0) < 0) {
        rc = errno == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
        fwd_sys_close(fd);
        return rc;
    }
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    rc = fwd_sys_poll(&p, 1, timeout_ms);
    if (rc == 0) {
        fwd_sys_close(fd);
        return FWD_E_TIMEOUT;
    }
    if (rc < 0) {
        rc = errno == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
        fwd_sys_close(fd);
        return rc;
    }
    n = fwd_sys_recv(fd, resp, resplen - 1, 0);
    fwd_sys_close(fd);
    if (n < 0)
        return FWD_E_SYS; /* e.g. ECONNREFUSED: nobody listening */
    resp[n] = '\0';
    return (int)n;
}

#else /* !FWD_ENABLE_CTL */

int fwd_ctl_listen(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port)
{
    (void)addr, (void)port;
    return fwd_err(ctx, FWD_E_NOSYS, "control channel disabled");
}
void fwd_ctl_close(fwd_ctx *ctx) { (void)ctx; }
void fwd_ctl_collect(fwd_ctx *ctx) { (void)ctx; }
void fwd_ctl_dispatch(fwd_ctx *ctx, short revents)
{
    (void)ctx, (void)revents;
}
int fwd_client_request(const fwd_addr *server, uint16_t port, const char *cmd,
                       char *resp, size_t resplen, int timeout_ms)
{
    (void)server, (void)port, (void)cmd, (void)resp, (void)resplen,
        (void)timeout_ms;
    return FWD_E_NOSYS;
}

#endif
