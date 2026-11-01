/*
 * fwd_forward.c - user space TCP and UDP port forwarding.
 *
 * TCP: every accepted connection gets a non blocking upstream connection and
 *      two buffers; half close is propagated with shutdown(SHUT_WR).
 * UDP: every client address gets its own connected upstream socket; replies
 *      are sent back through the listening socket. Idle sessions expire.
 *
 * Objects are never freed while fwd_run() dispatches events: they are only
 * marked dead and reaped at the end of the iteration.
 */
#include "fwd_internal.h"

#if FWD_ENABLE_PORTFWD

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static int would_block(int e)
{
    return e == EAGAIN || e == EWOULDBLOCK || e == EINTR;
}

static int close_keep_errno(int fd)
{
    int e = errno;
    fwd_sys_close(fd);
    errno = e;
    return -1;
}

static int spec_check(fwd_ctx *ctx, const fwd_forward_spec *s)
{
    if (s->proto == FWD_PROTO_TCP) {
        if (!FWD_ENABLE_TCP_FWD)
            return fwd_err(ctx, FWD_E_NOSYS, "tcp forwarding disabled");
    } else if (s->proto == FWD_PROTO_UDP) {
        if (!FWD_ENABLE_UDP_FWD)
            return fwd_err(ctx, FWD_E_NOSYS, "udp forwarding disabled");
    } else {
        return fwd_err(ctx, FWD_E_INVAL, "protocol must be tcp or udp");
    }
    if (!fwd_addr_usable(&s->laddr))
        return fwd_err(ctx, FWD_E_INVAL,
                       "listen address: unsupported address family");
    if (!fwd_addr_usable(&s->raddr))
        return fwd_err(ctx, FWD_E_INVAL,
                       "remote address: unsupported address family");
    if (!s->lport || !s->rport)
        return fwd_err(ctx, FWD_E_INVAL, "ports must be non-zero");
    return FWD_OK;
}

static struct fwd_fwd *find_fwd(fwd_ctx *ctx, uint32_t id)
{
    struct fwd_fwd *f;
    for (f = ctx->fwds; f; f = f->next)
        if (!f->dead && f->id == id)
            return f;
    return NULL;
}

static int same_listen(const fwd_forward_spec *a, const fwd_forward_spec *b)
{
    return a->proto == b->proto && a->lport == b->lport &&
           fwd_addr_equal(&a->laddr, &b->laddr);
}

static struct fwd_fwd *find_listen(fwd_ctx *ctx, const fwd_forward_spec *s,
                                   const struct fwd_fwd *except)
{
    struct fwd_fwd *f;
    for (f = ctx->fwds; f; f = f->next)
        if (!f->dead && f != except && same_listen(&f->spec, s))
            return f;
    return NULL;
}

static int open_listener(fwd_ctx *ctx, const fwd_forward_spec *sp, int *out)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    int fd, one = 1, tcp = sp->proto == FWD_PROTO_TCP;

    if (fwd_addr_to_sockaddr(&sp->laddr, sp->lport, &ss, &sl) != FWD_OK)
        return fwd_err(ctx, FWD_E_INVAL, "invalid listen address");
    fd = fwd_sys_socket(ss.ss_family, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd < 0)
        return fwd_err_sys(ctx, "socket");
    if (tcp)
        fwd_sys_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#if FWD_ENABLE_IPV6 && defined(IPV6_V6ONLY)
    if (ss.ss_family == AF_INET6)
        fwd_sys_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
    if (fwd_sys_set_nonblock(fd) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "set non-blocking");
    }
    if (fwd_sys_bind(fd, (struct sockaddr *)&ss, sl) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "bind");
    }
    if (tcp && fwd_sys_listen(fd, 128) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "listen");
    }
    *out = fd;
    return FWD_OK;
}

/* ------------------------------------------------------------------ */
/* Kill helpers (idempotent)                                          */
/* ------------------------------------------------------------------ */
static void kill_conn(fwd_ctx *ctx, struct fwd_tcpconn *c)
{
    if (c->dead)
        return;
    if (c->a >= 0)
        fwd_sys_close(c->a);
    if (c->b >= 0)
        fwd_sys_close(c->b);
    c->a = c->b = -1;
    c->dead = 1;
    ctx->nconns--;
}

static void kill_sess(fwd_ctx *ctx, struct fwd_udpsess *s)
{
    if (s->dead)
        return;
    if (s->up >= 0)
        fwd_sys_close(s->up);
    s->up = -1;
    s->dead = 1;
    ctx->nconns--;
}

static void kill_children(fwd_ctx *ctx, struct fwd_fwd *f, int conns,
                          int sess)
{
    struct fwd_tcpconn *c;
    struct fwd_udpsess *s;
    if (conns)
        for (c = f->conns; c; c = c->next)
            kill_conn(ctx, c);
    if (sess)
        for (s = f->sess; s; s = s->next)
            kill_sess(ctx, s);
}

static void kill_fwd(fwd_ctx *ctx, struct fwd_fwd *f)
{
    if (f->dead)
        return;
    kill_children(ctx, f, 1, 1);
    if (f->lfd >= 0)
        fwd_sys_close(f->lfd);
    f->lfd = -1;
    f->dead = 1;
}

/* ------------------------------------------------------------------ */
/* TCP                                                                */
/* ------------------------------------------------------------------ */
static void tcp_new_conn(fwd_ctx *ctx, struct fwd_fwd *f, int cfd)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    struct fwd_tcpconn *c;
    int bfd = -1;

    if (ctx->nconns >= ctx->cfg.max_conns || fwd_sys_set_nonblock(cfd) < 0 ||
        fwd_addr_to_sockaddr(&f->spec.raddr, f->spec.rport, &ss, &sl) !=
            FWD_OK)
        goto fail;
    bfd = fwd_sys_socket(ss.ss_family, SOCK_STREAM, 0);
    if (bfd < 0 || fwd_sys_set_nonblock(bfd) < 0)
        goto fail;
    c = FWD_CALLOC(1, sizeof *c);
    if (!c)
        goto fail;
    c->a = cfd;
    c->b = bfd;
    if (fwd_sys_connect(bfd, (struct sockaddr *)&ss, sl) < 0) {
        if (errno != EINPROGRESS) {
            FWD_FREE(c);
            goto fail;
        }
        c->connecting = 1;
    }
    c->next = f->conns;
    f->conns = c;
    ctx->nconns++;
    return;
fail:
    fwd_sys_close(cfd);
    if (bfd >= 0)
        fwd_sys_close(bfd);
}

static void tcp_accept(fwd_ctx *ctx, struct fwd_fwd *f)
{
    int i;
    for (i = 0; i < 16; i++) {
        int cfd = fwd_sys_accept(f->lfd, NULL, NULL);
        if (cfd < 0)
            return;
        tcp_new_conn(ctx, f, cfd);
    }
}

static void tcp_read(fwd_ctx *ctx, struct fwd_tcpconn *c, int fd,
                     struct fwd_buf *buf, int *eof)
{
    ssize_t n = fwd_sys_recv(fd, buf->d, sizeof buf->d, 0);
    if (n > 0) {
        buf->off = 0;
        buf->len = (size_t)n;
    } else if (n == 0) {
        *eof = 1;
    } else if (!would_block(errno)) {
        kill_conn(ctx, c);
    }
}

static void tcp_write(fwd_ctx *ctx, struct fwd_tcpconn *c, int fd,
                      struct fwd_buf *buf)
{
    ssize_t n = fwd_sys_send(fd, buf->d + buf->off, buf->len - buf->off,
                             MSG_NOSIGNAL);
    if (n > 0) {
        buf->off += (size_t)n;
        if (buf->off >= buf->len)
            buf->off = buf->len = 0;
    } else if (n < 0 && !would_block(errno)) {
        kill_conn(ctx, c);
    }
}

/* Propagate half closes; finish the connection when both directions ended */
static void tcp_after(fwd_ctx *ctx, struct fwd_tcpconn *c)
{
    if (!c->connecting) {
        if (c->a_eof && c->ab.len == 0 && !c->b_shut) {
            if (fwd_sys_shutdown(c->b, SHUT_WR) < 0) {
                kill_conn(ctx, c);
                return;
            }
            c->b_shut = 1;
        }
        if (c->b_eof && c->ba.len == 0 && !c->a_shut) {
            if (fwd_sys_shutdown(c->a, SHUT_WR) < 0) {
                kill_conn(ctx, c);
                return;
            }
            c->a_shut = 1;
        }
    }
    if (c->a_shut && c->b_shut)
        kill_conn(ctx, c);
}

static void tcp_service(fwd_ctx *ctx, struct fwd_tcpconn *c, int side,
                        short rev)
{
    int fd = side == 0 ? c->a : c->b;
    struct fwd_buf *in = side == 0 ? &c->ab : &c->ba;
    struct fwd_buf *out = side == 0 ? &c->ba : &c->ab;
    int *eof = side == 0 ? &c->a_eof : &c->b_eof;

    if (c->dead)
        return;

    if (side == 1 && c->connecting) {
        if (rev & (POLLOUT | POLLERR | POLLHUP)) {
            int err = 0;
            socklen_t el = sizeof err;
            if (fwd_sys_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 ||
                err)
                kill_conn(ctx, c);
            else
                c->connecting = 0;
        }
        return;
    }

    if ((rev & POLLIN) && !*eof && in->len == 0)
        tcp_read(ctx, c, fd, in, eof);
    if (!c->dead && (rev & POLLOUT) && out->len > 0)
        tcp_write(ctx, c, fd, out);
    if (c->dead)
        return;
    if (rev & (POLLERR | POLLNVAL))
        kill_conn(ctx, c);
    else if ((rev & POLLHUP) && !(rev & POLLIN))
        kill_conn(ctx, c);
    else
        tcp_after(ctx, c);
}

/* ------------------------------------------------------------------ */
/* UDP                                                                */
/* ------------------------------------------------------------------ */
static struct fwd_udpsess *sess_find(struct fwd_fwd *f,
                                     const struct sockaddr_storage *cs,
                                     socklen_t cl)
{
    struct fwd_udpsess *s;
    for (s = f->sess; s; s = s->next)
        if (!s->dead && s->clen == cl && memcmp(&s->client, cs, cl) == 0)
            return s;
    return NULL;
}

static struct fwd_udpsess *sess_new(fwd_ctx *ctx, struct fwd_fwd *f,
                                    const struct sockaddr_storage *cs,
                                    socklen_t cl, int64_t now)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    struct fwd_udpsess *s;
    int fd;

    if (ctx->nconns >= ctx->cfg.max_conns ||
        fwd_addr_to_sockaddr(&f->spec.raddr, f->spec.rport, &ss, &sl) !=
            FWD_OK)
        return NULL;
    fd = fwd_sys_socket(ss.ss_family, SOCK_DGRAM, 0);
    if (fd < 0)
        return NULL;
    if (fwd_sys_set_nonblock(fd) < 0 ||
        fwd_sys_connect(fd, (struct sockaddr *)&ss, sl) < 0) {
        fwd_sys_close(fd);
        return NULL;
    }
    s = FWD_CALLOC(1, sizeof *s);
    if (!s) {
        fwd_sys_close(fd);
        return NULL;
    }
    s->owner = f;
    s->client = *cs;
    s->clen = cl;
    s->up = fd;
    s->last = now;
    s->next = f->sess;
    f->sess = s;
    ctx->nconns++;
    return s;
}

static void udp_listener_read(fwd_ctx *ctx, struct fwd_fwd *f)
{
    int i;
    for (i = 0; i < 32; i++) {
        struct sockaddr_storage cs;
        socklen_t cl = sizeof cs;
        struct fwd_udpsess *s;
        int64_t now = fwd_sys_now_ms();
        ssize_t n;

        memset(&cs, 0, sizeof cs);
        n = fwd_sys_recvfrom(f->lfd, ctx->pkt, FWD_PKT_BUF, 0,
                             (struct sockaddr *)&cs, &cl);
        if (n < 0)
            break;
        s = sess_find(f, &cs, cl);
        if (!s)
            s = sess_new(ctx, f, &cs, cl, now);
        if (!s)
            continue;
        s->last = now;
        fwd_sys_send(s->up, ctx->pkt, (size_t)n, MSG_NOSIGNAL);
    }
}

static void udp_up_read(fwd_ctx *ctx, struct fwd_udpsess *s)
{
    int i;
    for (i = 0; i < 32; i++) {
        ssize_t n = fwd_sys_recv(s->up, ctx->pkt, FWD_PKT_BUF, 0);
        if (n < 0) {
            if (errno == ECONNREFUSED)
                continue; /* ICMP error from an earlier send */
            break;
        }
        fwd_sys_sendto(s->owner->lfd, ctx->pkt, (size_t)n, 0,
                       (struct sockaddr *)&s->client, s->clen);
        s->last = fwd_sys_now_ms();
    }
}

/* ------------------------------------------------------------------ */
/* Event loop hooks                                                   */
/* ------------------------------------------------------------------ */
void fwd_forward_collect(fwd_ctx *ctx)
{
    struct fwd_fwd *f;
    for (f = ctx->fwds; f; f = f->next) {
        struct fwd_tcpconn *c;
        struct fwd_udpsess *s;
        if (f->dead)
            continue;
        fwd_pollset_add(ctx, f->lfd, POLLIN, FWD_PK_LISTEN, f);
        for (c = f->conns; c; c = c->next) {
            short aev = 0, bev = 0;
            if (c->dead)
                continue;
            if (!c->a_eof && c->ab.len == 0)
                aev |= POLLIN;
            if (c->ba.len > 0)
                aev |= POLLOUT;
            if (c->connecting) {
                bev = POLLOUT;
            } else {
                if (!c->b_eof && c->ba.len == 0)
                    bev |= POLLIN;
                if (c->ab.len > 0)
                    bev |= POLLOUT;
            }
            fwd_pollset_add(ctx, c->a, aev, FWD_PK_TCP_A, c);
            fwd_pollset_add(ctx, c->b, bev, FWD_PK_TCP_B, c);
        }
        for (s = f->sess; s; s = s->next)
            if (!s->dead)
                fwd_pollset_add(ctx, s->up, POLLIN, FWD_PK_UDP_UP, s);
    }
}

void fwd_forward_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e,
                          short rev)
{
    switch (e->kind) {
    case FWD_PK_LISTEN: {
        struct fwd_fwd *f = e->obj;
        if (f->dead || !(rev & (POLLIN | POLLERR)))
            return;
        if (f->spec.proto == FWD_PROTO_TCP)
            tcp_accept(ctx, f);
        else
            udp_listener_read(ctx, f);
        break;
    }
    case FWD_PK_TCP_A:
        tcp_service(ctx, e->obj, 0, rev);
        break;
    case FWD_PK_TCP_B:
        tcp_service(ctx, e->obj, 1, rev);
        break;
    case FWD_PK_UDP_UP: {
        struct fwd_udpsess *s = e->obj;
        if (s->dead || s->owner->dead)
            return;
        if (rev & (POLLIN | POLLERR))
            udp_up_read(ctx, s);
        break;
    }
    }
}

void fwd_forward_housekeeping(fwd_ctx *ctx, int64_t now)
{
    struct fwd_fwd *f;
    if (now < 0 || ctx->cfg.udp_timeout_ms == 0)
        return;
    for (f = ctx->fwds; f; f = f->next) {
        struct fwd_udpsess *s;
        for (s = f->sess; s; s = s->next)
            if (!s->dead && now - s->last > (int64_t)ctx->cfg.udp_timeout_ms)
                kill_sess(ctx, s);
    }
}

void fwd_forward_reap(fwd_ctx *ctx)
{
    struct fwd_fwd **pf = &ctx->fwds;

    while (*pf) {
        struct fwd_fwd *f = *pf;
        struct fwd_tcpconn **pc = &f->conns;
        struct fwd_udpsess **ps = &f->sess;

        while (*pc) {
            struct fwd_tcpconn *c = *pc;
            if (c->dead || f->dead) {
                kill_conn(ctx, c);
                *pc = c->next;
                FWD_FREE(c);
            } else {
                pc = &c->next;
            }
        }
        while (*ps) {
            struct fwd_udpsess *s = *ps;
            if (s->dead || f->dead) {
                kill_sess(ctx, s);
                *ps = s->next;
                FWD_FREE(s);
            } else {
                ps = &s->next;
            }
        }
        if (f->dead) {
            *pf = f->next;
            FWD_FREE(f);
        } else {
            pf = &f->next;
        }
    }
}

void fwd_forward_free_all(fwd_ctx *ctx)
{
    struct fwd_fwd *f;
    for (f = ctx->fwds; f; f = f->next)
        kill_fwd(ctx, f);
    fwd_forward_reap(ctx);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
int fwd_forward_add(fwd_ctx *ctx, const fwd_forward_spec *spec, uint32_t *id)
{
    struct fwd_fwd *f;
    int rc, fd;

    if (!ctx || !spec)
        return FWD_E_INVAL;
    if ((rc = spec_check(ctx, spec)) != FWD_OK)
        return rc;
    if (find_listen(ctx, spec, NULL))
        return fwd_err(ctx, FWD_E_EXIST, "listen endpoint already forwarded");
    if ((rc = open_listener(ctx, spec, &fd)) != FWD_OK)
        return rc;
    f = FWD_CALLOC(1, sizeof *f);
    if (!f) {
        fwd_sys_close(fd);
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
    }
    f->id = ctx->next_fwd_id++;
    f->spec = *spec;
    f->lfd = fd;
    f->next = ctx->fwds;
    ctx->fwds = f;
    if (id)
        *id = f->id;
    return FWD_OK;
}

int fwd_forward_del(fwd_ctx *ctx, uint32_t id)
{
    struct fwd_fwd *f = find_fwd(ctx, id);
    if (!f)
        return fwd_err(ctx, FWD_E_NOENT, "no forward with id %u", id);
    kill_fwd(ctx, f);
    if (!ctx->in_run)
        fwd_forward_reap(ctx);
    return FWD_OK;
}

int fwd_forward_mod(fwd_ctx *ctx, uint32_t id, const fwd_forward_spec *spec)
{
    struct fwd_fwd *f = find_fwd(ctx, id);
    int rc, proto_changed, remote_changed;

    if (!f)
        return fwd_err(ctx, FWD_E_NOENT, "no forward with id %u", id);
    if (!spec)
        return FWD_E_INVAL;
    if ((rc = spec_check(ctx, spec)) != FWD_OK)
        return rc;

    proto_changed = spec->proto != f->spec.proto;
    remote_changed = spec->rport != f->spec.rport ||
                     !fwd_addr_equal(&spec->raddr, &f->spec.raddr);

    if (!same_listen(spec, &f->spec)) {
        int nfd;
        if (find_listen(ctx, spec, f))
            return fwd_err(ctx, FWD_E_EXIST,
                           "listen endpoint already forwarded");
        if ((rc = open_listener(ctx, spec, &nfd)) != FWD_OK)
            return rc;
        /* TCP connections survive a change of listen endpoint */
        kill_children(ctx, f, proto_changed, 1);
        fwd_sys_close(f->lfd);
        f->lfd = nfd;
    } else if (remote_changed) {
        kill_children(ctx, f, 0, 1); /* UDP sessions point at the old remote */
    }
    f->spec = *spec;
    if (!ctx->in_run)
        fwd_forward_reap(ctx);
    return FWD_OK;
}

int fwd_forward_list(fwd_ctx *ctx,
                     int (*cb)(const fwd_forward_info *, void *), void *ud)
{
    struct fwd_fwd *f;
    for (f = ctx->fwds; f; f = f->next) {
        fwd_forward_info info;
        struct fwd_tcpconn *c;
        struct fwd_udpsess *s;
        if (f->dead)
            continue;
        memset(&info, 0, sizeof info);
        info.id = f->id;
        info.spec = f->spec;
        for (c = f->conns; c; c = c->next)
            info.active += !c->dead;
        for (s = f->sess; s; s = s->next)
            info.active += !s->dead;
        if (cb(&info, ud))
            break;
    }
    return FWD_OK;
}

#else /* !FWD_ENABLE_PORTFWD */

void fwd_forward_collect(fwd_ctx *ctx) { (void)ctx; }
void fwd_forward_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e, short r)
{
    (void)ctx, (void)e, (void)r;
}
void fwd_forward_housekeeping(fwd_ctx *ctx, int64_t now)
{
    (void)ctx, (void)now;
}
void fwd_forward_reap(fwd_ctx *ctx) { (void)ctx; }
void fwd_forward_free_all(fwd_ctx *ctx) { (void)ctx; }

int fwd_forward_add(fwd_ctx *ctx, const fwd_forward_spec *spec, uint32_t *id)
{
    (void)spec, (void)id;
    return fwd_err(ctx, FWD_E_NOSYS, "port forwarding disabled");
}
int fwd_forward_del(fwd_ctx *ctx, uint32_t id)
{
    (void)id;
    return fwd_err(ctx, FWD_E_NOSYS, "port forwarding disabled");
}
int fwd_forward_mod(fwd_ctx *ctx, uint32_t id, const fwd_forward_spec *spec)
{
    (void)id, (void)spec;
    return fwd_err(ctx, FWD_E_NOSYS, "port forwarding disabled");
}
int fwd_forward_list(fwd_ctx *ctx,
                     int (*cb)(const fwd_forward_info *, void *), void *ud)
{
    (void)cb, (void)ud;
    return fwd_err(ctx, FWD_E_NOSYS, "port forwarding disabled");
}

#endif
