/*
 * fwd_tun.c - TUN/TAP tunnels (Linux).
 *
 * A tunnel connects a TUN (IP packets) or TAP (ethernet frames) interface to
 * a UDP socket. Plain UDP: packets read from the device are sent as one
 * datagram each; datagrams from the peer are written to the device. QUIC
 * (fwd_tunq.c) shares the socket: every datagram read here is first offered
 * to it, every device packet first goes to it.
 *
 * Connect mode: the socket is connected to the peer, so the kernel drops
 * datagrams from any other source.
 * Listen mode: the socket is only bound. Every received datagram is checked
 * against the raddr/rport filter (zero = any); its source is a peer (plain
 * UDP), or the datagram belongs to a QUIC connection, which is a peer once
 * it is up. Device packets go to the peer that has sent from their
 * destination address (learned in fwd_tun_from_peer), or to all peers.
 */
#include "fwd_internal.h"

#if FWD_ENABLE_TUNTAP

#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <linux/if_tun.h>

static int close_keep_errno(int fd)
{
    int e = errno;
    fwd_sys_close(fd);
    errno = e;
    return -1;
}

static int max_prefix(const fwd_addr *a)
{
    return a->family == FWD_V6 ? 128 : 32;
}

/* Listen mode: an unset or all-zero remote address accepts any peer. */
static int any_peer_addr(const fwd_tunnel_spec *s)
{
    return s->raddr.family == 0 || fwd_addr_is_zero(&s->raddr);
}

static int spec_check(fwd_ctx *ctx, const fwd_tunnel_spec *s)
{
    int listen = s->mode == FWD_TUN_LISTEN;

    if (s->type != FWD_TUN_TUN && s->type != FWD_TUN_TAP)
        return fwd_err(ctx, FWD_E_INVAL, "type must be tun or tap");
    if (s->mode != FWD_TUN_CONNECT && s->mode != FWD_TUN_LISTEN)
        return fwd_err(ctx, FWD_E_INVAL, "mode must be connect or listen");
    if (s->transport > FWD_TT_UDP)
        return fwd_err(ctx, FWD_E_INVAL, "transport must be auto, quic or udp");
    if (memchr(s->pin, '\0', sizeof s->pin) == NULL)
        return fwd_err(ctx, FWD_E_RANGE, "pin too long");
    if (s->pin[0]) {
        uint8_t pin[32];
        if (fwd_parse_pin(s->pin, pin) < 0)
            return fwd_err(ctx, FWD_E_INVAL,
                           "pin must be a SHA-256 fingerprint (64 hex digits)");
        if (s->transport == FWD_TT_UDP)
            return fwd_err(ctx, FWD_E_INVAL, "a pin needs QUIC (transport udp)");
    }
#if !FWD_ENABLE_QUIC
    if (s->transport == FWD_TT_QUIC || s->pin[0])
        return fwd_err(ctx, FWD_E_NOSYS, "QUIC tunnels not compiled in");
#endif
    if (strlen(s->name) >= FWD_IFNAME_MAX)
        return fwd_err(ctx, FWD_E_RANGE, "interface name too long");
    if (!fwd_addr_usable(&s->laddr) ||
        (!(listen && any_peer_addr(s)) && !fwd_addr_usable(&s->raddr)))
        return fwd_err(ctx, FWD_E_INVAL,
                       "tunnel endpoint: unsupported address family");
    if (!(listen && any_peer_addr(s)) && s->laddr.family != s->raddr.family)
        return fwd_err(ctx, FWD_E_INVAL,
                       "local and remote endpoint must use the same family");
    if (!s->lport || (!listen && !s->rport))
        return fwd_err(ctx, FWD_E_INVAL, "ports must be non-zero");
    if (s->prefix) {
        if (!fwd_addr_usable(&s->ifaddr))
            return fwd_err(ctx, FWD_E_INVAL,
                           "interface address: unsupported family");
        if (s->prefix > max_prefix(&s->ifaddr))
            return fwd_err(ctx, FWD_E_RANGE, "prefix length too large");
    }
    if (s->mtu && s->mtu < 68)
        return fwd_err(ctx, FWD_E_RANGE, "mtu too small");
    return FWD_OK;
}

static struct fwd_tun *find_tun(fwd_ctx *ctx, uint32_t id)
{
    struct fwd_tun *t;
    for (t = ctx->tuns; t; t = t->next)
        if (!t->dead && t->id == id)
            return t;
    return NULL;
}

static int open_dev(fwd_ctx *ctx, const fwd_tunnel_spec *sp, int *out,
                    char *name)
{
    struct ifreq ifr;
    int fd;

    fd = fwd_sys_open("/dev/net/tun", O_RDWR);
    if (fd < 0)
        return fwd_err_sys(ctx, "open /dev/net/tun");
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags =
        (short)((sp->type == FWD_TUN_TAP ? IFF_TAP : IFF_TUN) | IFF_NO_PI);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", sp->name);
    if (fwd_sys_ioctl(fd, TUNSETIFF, &ifr) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "TUNSETIFF");
    }
    if (fwd_sys_set_nonblock(fd) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "set non-blocking");
    }
    snprintf(name, FWD_IFNAME_MAX, "%.*s", FWD_IFNAME_MAX - 1, ifr.ifr_name);
    *out = fd;
    return FWD_OK;
}

static int open_sock(fwd_ctx *ctx, const fwd_tunnel_spec *sp, int *out)
{
    struct sockaddr_storage ls, rs;
    socklen_t ll, rl;
    int fd, one = 1, listen = sp->mode == FWD_TUN_LISTEN;

    if (fwd_addr_to_sockaddr(&sp->laddr, sp->lport, &ls, &ll) != FWD_OK ||
        (!listen &&
         fwd_addr_to_sockaddr(&sp->raddr, sp->rport, &rs, &rl) != FWD_OK))
        return fwd_err(ctx, FWD_E_INVAL, "invalid tunnel endpoint");
    fd = fwd_sys_socket(ls.ss_family, SOCK_DGRAM, 0);
    if (fd < 0)
        return fwd_err_sys(ctx, "socket");
#if FWD_ENABLE_IPV6 && defined(IPV6_V6ONLY)
    if (ls.ss_family == AF_INET6)
        fwd_sys_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
    (void)one;
    if (fwd_sys_set_nonblock(fd) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "set non-blocking");
    }
    if (fwd_sys_bind(fd, (struct sockaddr *)&ls, ll) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "bind");
    }
    if (!listen && fwd_sys_connect(fd, (struct sockaddr *)&rs, rl) < 0) {
        close_keep_errno(fd);
        return fwd_err_sys(ctx, "connect");
    }
    *out = fd;
    return FWD_OK;
}

/* Listen mode: may a datagram from this source become the peer? */
static int peer_allowed(const fwd_tunnel_spec *s,
                        const struct sockaddr_storage *from)
{
    fwd_addr a;
    uint16_t port;

    if (fwd_sockaddr_to_addr(from, &a, &port) != FWD_OK)
        return 0;
    if (!any_peer_addr(s) && !fwd_addr_equal(&a, &s->raddr))
        return 0;
    return s->rport == 0 || s->rport == port;
}

/* ------------------------------------------------------------------ */
/* listen mode: peers and learned inner addresses                     */
/* ------------------------------------------------------------------ */
static int same_addr(const struct sockaddr_storage *a, socklen_t al,
                     const struct sockaddr_storage *b, socklen_t bl)
{
    fwd_addr x, y;
    uint16_t xp, yp;

    return al && bl && fwd_sockaddr_to_addr(a, &x, &xp) == FWD_OK &&
           fwd_sockaddr_to_addr(b, &y, &yp) == FWD_OK && xp == yp &&
           fwd_addr_equal(&x, &y);
}

/* Inner source and destination of a packet; their length, 0 = unknown. */
static int inner_addrs(const struct fwd_tun *t, const uint8_t *p, size_t n,
                       const uint8_t **src, const uint8_t **dst)
{
    if (t->spec.type == FWD_TUN_TAP) {
        if (n < 14)
            return 0;
        *dst = p;
        *src = p + 6;
        return 6;
    }
    if (n >= 20 && p[0] >> 4 == 4) {
        *src = p + 12;
        *dst = p + 16;
        return 4;
    }
    if (n >= 40 && p[0] >> 4 == 6) {
        *src = p + 8;
        *dst = p + 24;
        return 16;
    }
    return 0;
}

/* broadcast / multicast: for everybody, owned by nobody */
static int group_addr(const uint8_t *a, int len)
{
    return len == 6 ? a[0] & 1 : len == 4 ? a[0] >= 224 : a[0] == 0xff;
}

static int route_find(const struct fwd_tun *t, const uint8_t *key, int len)
{
    int i;
    for (i = 0; i < FWD_TUN_MAX_ROUTES; i++)
        if (t->routes[i].len == len && !memcmp(t->routes[i].key, key, len))
            return i;
    return -1;
}

static void route_learn(struct fwd_tun *t, int peer, const uint8_t *key,
                        int len, int64_t now)
{
    int i = route_find(t, key, len), j, zero = 1;

    if (i < 0) {
        for (j = 0; j < len; j++)
            zero &= key[j] == 0;
        if (zero || group_addr(key, len))
            return;
        /* a free slot, else the one not heard from for longest */
        for (i = 0, j = 1; j < FWD_TUN_MAX_ROUTES && t->routes[i].len; j++)
            if (!t->routes[j].len || t->routes[j].seen < t->routes[i].seen)
                i = j;
        t->routes[i].len = (uint8_t)len;
        memcpy(t->routes[i].key, key, (size_t)len);
    }
    t->routes[i].peer = (uint8_t)peer;
    t->routes[i].seen = now;
}

static struct fwd_tunpeer *plain_peer(const struct fwd_tun *t,
                                      const struct sockaddr_storage *a,
                                      socklen_t al)
{
    int i;
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
        if (t->peers[i].addrlen && !t->peers[i].qc &&
            same_addr(&t->peers[i].addr, t->peers[i].addrlen, a, al))
            return &t->peers[i];
    return NULL;
}

void fwd_tun_peer_del(struct fwd_tun *t, struct fwd_tunpeer *p)
{
    int idx = (int)(p - t->peers), i;

    if (!p->addrlen)
        return;
    for (i = 0; i < FWD_TUN_MAX_ROUTES; i++)
        if (t->routes[i].len && t->routes[i].peer == idx)
            t->routes[i].len = 0;
    if (p->qc)
        fwd_tunq_conn_drop(p->qc);
    memset(p, 0, sizeof *p);
    t->npeers--;
}

static void peers_clear(struct fwd_tun *t, int plain_only)
{
    int i;
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
        if (t->peers[i].addrlen && (!plain_only || !t->peers[i].qc))
            fwd_tun_peer_del(t, &t->peers[i]);
}

struct fwd_tunpeer *fwd_tun_peer_add(struct fwd_tun *t,
                                     const struct sockaddr_storage *a,
                                     socklen_t al, struct tq_conn *qc)
{
    struct fwd_tunpeer *p = NULL;
    int i;

    if (t->spec.mode != FWD_TUN_LISTEN || !t->peers)
        return NULL;
    /* QUIC peers push plain ones out (no downgrade while QUIC is used); a
     * new connection from the address of an old one replaces it */
    if (qc && t->spec.transport == FWD_TT_AUTO)
        peers_clear(t, 1);
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
        if (t->peers[i].addrlen &&
            same_addr(&t->peers[i].addr, t->peers[i].addrlen, a, al))
            fwd_tun_peer_del(t, &t->peers[i]);
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++) {
        struct fwd_tunpeer *x = &t->peers[i];
        if (!x->addrlen) {
            p = x;
            break;
        }
        if (!p || x->seq < p->seq)
            p = x;
    }
    if (p->addrlen) /* full: the one silent for longest makes room */
        fwd_tun_peer_del(t, p);
    p->addr = *a;
    p->addrlen = al;
    p->qc = qc;
    p->last_rx = fwd_sys_now_ms();
    p->seq = ++t->rx_seq;
    t->npeers++;
    return p;
}

void fwd_tun_from_peer(struct fwd_tun *t, struct fwd_tunpeer *p,
                       const uint8_t *pkt, size_t n)
{
    const uint8_t *src, *dst;
    int len;

    if (p) {
        p->last_rx = fwd_sys_now_ms();
        p->seq = ++t->rx_seq;
        p->rx++;
        if ((len = inner_addrs(t, pkt, n, &src, &dst)) > 0)
            route_learn(t, (int)(p - t->peers), src, len, p->last_rx);
    }
    if (fwd_sys_write(t->dev, pkt, n) >= 0)
        t->rx++;
}

static int send_peer(struct fwd_tun *t, struct fwd_tunpeer *p,
                     const uint8_t *pkt, size_t n)
{
    int ok = p->qc ? fwd_tunq_conn_send(p->qc, pkt, n) == 0
                   : fwd_sys_sendto(t->sock, pkt, n, MSG_NOSIGNAL,
                                    (struct sockaddr *)&p->addr,
                                    p->addrlen) >= 0;
    if (ok)
        p->tx++;
    return ok;
}

/* A device packet in listen mode: to its owner, or to everybody. */
static void listen_out(struct fwd_tun *t, const uint8_t *pkt, size_t n)
{
    struct fwd_tunpeer *to = NULL;
    const uint8_t *src, *dst;
    int i, len, sent = 0;

    if (!t->npeers)
        return; /* nobody yet: drop */
    if (t->npeers == 1) {
        for (i = 0; !to; i++)
            if (t->peers[i].addrlen)
                to = &t->peers[i];
    } else if ((len = inner_addrs(t, pkt, n, &src, &dst)) > 0 &&
               !group_addr(dst, len) && (i = route_find(t, dst, len)) >= 0) {
        to = &t->peers[t->routes[i].peer];
    }
    if (to) {
        sent = send_peer(t, to, pkt, n);
    } else {
        for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
            if (t->peers[i].addrlen)
                sent |= send_peer(t, &t->peers[i], pkt, n);
    }
    if (sent)
        t->tx++;
}

/* Forget plain UDP peers that went quiet, but never the last one. */
static void sweep_peers(struct fwd_tun *t, int64_t now)
{
    int i;

    if (now - t->last_sweep < 1000)
        return;
    t->last_sweep = now;
    for (i = 0; i < FWD_TUN_MAX_PEERS && t->npeers > 1; i++)
        if (t->peers[i].addrlen && !t->peers[i].qc &&
            now - t->peers[i].last_rx > FWD_TUN_PEER_IDLE_MS)
            fwd_tun_peer_del(t, &t->peers[i]);
}

/* The peer heard from last. */
static struct fwd_tunpeer *last_peer(struct fwd_tun *t)
{
    struct fwd_tunpeer *p = NULL;
    int i;
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
        if (t->peers[i].addrlen && (!p || t->peers[i].seq > p->seq))
            p = &t->peers[i];
    return p;
}

/* MTU through SIOCSIFMTU, administrative state through SIOCSIFFLAGS; both
 * work on any socket, we use the tunnel's UDP socket. */
static int set_mtu(fwd_ctx *ctx, int sock, const char *name, unsigned mtu)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
    ifr.ifr_mtu = (int)mtu;
    if (fwd_sys_ioctl(sock, SIOCSIFMTU, &ifr) < 0)
        return fwd_err_sys(ctx, "set mtu");
    return FWD_OK;
}

static int set_up(fwd_ctx *ctx, int sock, const char *name)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
    if (fwd_sys_ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
        return fwd_err_sys(ctx, "get interface flags");
    ifr.ifr_flags |= IFF_UP;
    if (fwd_sys_ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
        return fwd_err_sys(ctx, "bring interface up");
    return FWD_OK;
}

static void kill_tun(struct fwd_tun *t)
{
    if (t->dead)
        return;
    fwd_tunq_stop(t);
    if (t->dev >= 0)
        fwd_sys_close(t->dev);
    if (t->sock >= 0)
        fwd_sys_close(t->sock);
    t->dev = t->sock = -1;
    t->dead = 1;
}

/* ------------------------------------------------------------------ */
void fwd_tun_collect(fwd_ctx *ctx)
{
    struct fwd_tun *t;
    for (t = ctx->tuns; t; t = t->next) {
        if (t->dead)
            continue;
        fwd_pollset_add(ctx, t->dev, POLLIN, FWD_PK_TUN_DEV, t);
        fwd_pollset_add(ctx, t->sock,
                        (short)(POLLIN |
                                (t->q && fwd_tunq_want_write(t) ? POLLOUT : 0)),
                        FWD_PK_TUN_SOCK, t);
    }
}

int fwd_tun_timeout(fwd_ctx *ctx, int timeout_ms)
{
    struct fwd_tun *t;
    for (t = ctx->tuns; t; t = t->next)
        if (!t->dead && t->q)
            timeout_ms = fwd_tunq_timeout(t, timeout_ms);
    return timeout_ms;
}

/* QUIC has timers (retransmission, idle, our retries), so it is serviced on
 * every loop iteration, not only when the socket is readable. */
void fwd_tun_service(fwd_ctx *ctx)
{
    struct fwd_tun *t;
    int64_t now = fwd_sys_now_ms();

    for (t = ctx->tuns; t; t = t->next) {
        if (t->dead)
            continue;
        if (t->q)
            fwd_tunq_service(ctx, t);
        if (t->spec.mode == FWD_TUN_LISTEN)
            sweep_peers(t, now);
    }
}

void fwd_tun_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e, short rev)
{
    struct fwd_tun *t = e->obj;
    int i;

    if (t->dead || !(rev & (POLLIN | POLLERR)))
        return;
    for (i = 0; i < 32; i++) {
        ssize_t n;
        if (e->kind == FWD_PK_TUN_DEV) {
            n = fwd_sys_read(t->dev, ctx->pkt, FWD_PKT_BUF);
            if (n <= 0)
                break;
            if (t->spec.mode == FWD_TUN_LISTEN) {
                listen_out(t, ctx->pkt, (size_t)n);
                continue;
            }
            if (t->q && fwd_tunq_send(ctx, t, ctx->pkt, (size_t)n))
                continue;
            if (fwd_sys_send(t->sock, ctx->pkt, (size_t)n, MSG_NOSIGNAL) >= 0)
                t->tx++;
        } else if (t->spec.mode == FWD_TUN_LISTEN) {
            struct sockaddr_storage from;
            socklen_t fl = sizeof from;
            struct fwd_tunpeer *p;

            memset(&from, 0, sizeof from);
            n = fwd_sys_recvfrom(t->sock, ctx->pkt, FWD_PKT_BUF, 0,
                                 (struct sockaddr *)&from, &fl);
            if (n < 0)
                break;
            if (n == 0 || !peer_allowed(&t->spec, &from))
                continue;
            if (t->q &&
                fwd_tunq_input(ctx, t, ctx->pkt, (size_t)n, &from, fl))
                continue;
            if (!(p = plain_peer(t, &from, fl)) &&
                !(p = fwd_tun_peer_add(t, &from, fl, NULL)))
                continue;
            fwd_tun_from_peer(t, p, ctx->pkt, (size_t)n);
        } else {
            n = fwd_sys_recv(t->sock, ctx->pkt, FWD_PKT_BUF, 0);
            if (n < 0) {
                if (errno == ECONNREFUSED)
                    continue; /* peer not up yet */
                break;
            }
            if (n == 0)
                continue;
            if (t->q && fwd_tunq_input(ctx, t, ctx->pkt, (size_t)n, NULL, 0))
                continue;
            fwd_tun_from_peer(t, NULL, ctx->pkt, (size_t)n);
        }
    }
}

void fwd_tun_reap(fwd_ctx *ctx)
{
    struct fwd_tun **pp = &ctx->tuns;
    while (*pp) {
        struct fwd_tun *t = *pp;
        if (t->dead) {
            *pp = t->next;
            FWD_FREE(t->peers);
            FWD_FREE(t->routes);
            FWD_FREE(t);
        } else {
            pp = &t->next;
        }
    }
}

void fwd_tun_free_all(fwd_ctx *ctx)
{
    struct fwd_tun *t;
    for (t = ctx->tuns; t; t = t->next)
        kill_tun(t);
    fwd_tun_reap(ctx);
}

/* ------------------------------------------------------------------ */
int fwd_tunnel_add(fwd_ctx *ctx, const fwd_tunnel_spec *spec, uint32_t *id)
{
    struct fwd_tun *t;
    char name[FWD_IFNAME_MAX];
    int rc, dev = -1, sock = -1;

    if (!ctx || !spec)
        return FWD_E_INVAL;
    if ((rc = spec_check(ctx, spec)) != FWD_OK)
        return rc;
    if ((rc = open_dev(ctx, spec, &dev, name)) != FWD_OK)
        return rc;
    if ((rc = open_sock(ctx, spec, &sock)) != FWD_OK)
        goto fail;
    if (spec->mtu && (rc = set_mtu(ctx, sock, name, spec->mtu)) != FWD_OK)
        goto fail;
    if ((rc = set_up(ctx, sock, name)) != FWD_OK)
        goto fail;
    if (spec->prefix) {
        if ((rc = fwd_nl_addr(ctx, 1, name, &spec->ifaddr, spec->prefix)) !=
            FWD_OK)
            goto fail;
        if ((rc = fwd_sysctl_enable_forwarding(ctx, spec->ifaddr.family)) !=
            FWD_OK)
            goto fail;
    }
    t = FWD_CALLOC(1, sizeof *t);
    if (!t) {
        rc = fwd_err(ctx, FWD_E_NOMEM, "out of memory");
        goto fail;
    }
    t->peers = FWD_CALLOC(FWD_TUN_MAX_PEERS, sizeof *t->peers);
    t->routes = FWD_CALLOC(FWD_TUN_MAX_ROUTES, sizeof *t->routes);
    if (!t->peers || !t->routes) {
        FWD_FREE(t->peers);
        FWD_FREE(t->routes);
        FWD_FREE(t);
        rc = fwd_err(ctx, FWD_E_NOMEM, "out of memory");
        goto fail;
    }
    t->id = ctx->next_tun_id++;
    t->spec = *spec;
    strcpy(t->spec.name, name);
    t->dev = dev;
    t->sock = sock;
    if ((rc = fwd_tunq_start(ctx, t)) != FWD_OK) {
        FWD_FREE(t->peers);
        FWD_FREE(t->routes);
        FWD_FREE(t);
        goto fail;
    }
    t->next = ctx->tuns;
    ctx->tuns = t;
    if (id)
        *id = t->id;
    return FWD_OK;
fail:
    if (sock >= 0)
        fwd_sys_close(sock);
    fwd_sys_close(dev); /* the interface disappears with the descriptor */
    return rc;
}

int fwd_tunnel_del(fwd_ctx *ctx, uint32_t id)
{
    struct fwd_tun *t = find_tun(ctx, id);
    if (!t)
        return fwd_err(ctx, FWD_E_NOENT, "no tunnel with id %u", id);
    kill_tun(t);
    if (!ctx->in_run)
        fwd_tun_reap(ctx);
    return FWD_OK;
}

int fwd_tunnel_mod(fwd_ctx *ctx, uint32_t id, const fwd_tunnel_spec *spec)
{
    struct fwd_tun *t = find_tun(ctx, id);
    int rc, ns = -1, sock;
    int local_changed, remote_changed, old_has, new_has, addr_changed;
    int mode_changed, quic_changed;
    int listen = spec ? spec->mode == FWD_TUN_LISTEN : 0;

    if (!t)
        return fwd_err(ctx, FWD_E_NOENT, "no tunnel with id %u", id);
    if (!spec)
        return FWD_E_INVAL;
    if ((rc = spec_check(ctx, spec)) != FWD_OK)
        return rc;
    if (spec->type != t->spec.type)
        return fwd_err(ctx, FWD_E_INVAL, "tunnel type cannot be changed");
    if (spec->name[0] && strcmp(spec->name, t->spec.name))
        return fwd_err(ctx, FWD_E_INVAL, "interface name cannot be changed");

    local_changed = spec->lport != t->spec.lport ||
                    !fwd_addr_equal(&spec->laddr, &t->spec.laddr);
    remote_changed = spec->rport != t->spec.rport ||
                     !fwd_addr_equal(&spec->raddr, &t->spec.raddr);
    mode_changed = spec->mode != t->spec.mode;
    quic_changed = local_changed || remote_changed || mode_changed ||
                   spec->transport != t->spec.transport ||
                   strcmp(spec->pin, t->spec.pin) != 0;
    old_has = t->spec.prefix != 0;
    new_has = spec->prefix != 0;
    addr_changed = old_has != new_has ||
                   (new_has && (spec->prefix != t->spec.prefix ||
                                !fwd_addr_equal(&spec->ifaddr,
                                                &t->spec.ifaddr)));

    if (local_changed) {
        if ((rc = open_sock(ctx, spec, &ns)) != FWD_OK)
            return rc;
    } else if (!listen && (remote_changed || mode_changed)) {
        struct sockaddr_storage rs;
        socklen_t rl;
        if (fwd_addr_to_sockaddr(&spec->raddr, spec->rport, &rs, &rl) !=
            FWD_OK)
            return fwd_err(ctx, FWD_E_INVAL, "invalid peer address");
        if (fwd_sys_connect(t->sock, (struct sockaddr *)&rs, rl) < 0)
            return fwd_err_sys(ctx, "connect");
    } else if (listen && mode_changed) {
        /* connect -> listen: dissolve the association (POSIX: AF_UNSPEC) */
        struct sockaddr_storage un;
        memset(&un, 0, sizeof un);
        un.ss_family = AF_UNSPEC;
        if (fwd_sys_connect(t->sock, (struct sockaddr *)&un, sizeof un) < 0)
            return fwd_err_sys(ctx, "disconnect");
    }
    sock = ns >= 0 ? ns : t->sock;

    if (spec->mtu && spec->mtu != t->spec.mtu &&
        (rc = set_mtu(ctx, sock, t->spec.name, spec->mtu)) != FWD_OK)
        goto fail;
    if (addr_changed) {
        if (old_has)
            fwd_nl_addr(ctx, 0, t->spec.name, &t->spec.ifaddr,
                        t->spec.prefix);
        if (new_has) {
            if ((rc = fwd_nl_addr(ctx, 1, t->spec.name, &spec->ifaddr,
                                  spec->prefix)) != FWD_OK)
                goto fail;
            if ((rc = fwd_sysctl_enable_forwarding(
                     ctx, spec->ifaddr.family)) != FWD_OK)
                goto fail;
        }
    }
    if (quic_changed)
        fwd_tunq_stop(t); /* its BIO uses the old socket */
    if (ns >= 0) {
        fwd_sys_close(t->sock);
        t->sock = ns;
    }
    {
        char keep[FWD_IFNAME_MAX];
        strcpy(keep, t->spec.name);
        t->spec = *spec;
        strcpy(t->spec.name, keep);
    }
    if (!listen || local_changed || mode_changed) {
        peers_clear(t, 0);
    } else {
        int i;
        for (i = 0; i < FWD_TUN_MAX_PEERS; i++)
            if (t->peers[i].addrlen &&
                !peer_allowed(&t->spec, &t->peers[i].addr))
                fwd_tun_peer_del(t, &t->peers[i]);
    }
    /* the change itself is done; a QUIC setup failure leaves the tunnel
     * without QUIC (auto: plain UDP) and is reported */
    if (quic_changed && !t->q)
        return fwd_tunq_start(ctx, t);
    return FWD_OK;
fail:
    if (ns >= 0)
        fwd_sys_close(ns);
    return rc;
}

int fwd_tunnel_get(fwd_ctx *ctx, uint32_t id, fwd_tunnel_info *info)
{
    struct fwd_tun *t = find_tun(ctx, id);
    if (!t)
        return fwd_err(ctx, FWD_E_NOENT, "no tunnel with id %u", id);
    memset(info, 0, sizeof *info);
    info->id = t->id;
    info->spec = t->spec;
    info->tx_packets = t->tx;
    info->rx_packets = t->rx;
    if (t->spec.mode == FWD_TUN_LISTEN) {
        struct fwd_tunpeer *p = last_peer(t);
        if (p) {
            fwd_sockaddr_to_addr(&p->addr, &info->peer, &info->peer_port);
            info->via = p->qc ? FWD_VIA_QUIC : FWD_VIA_UDP;
        }
        info->peers = t->npeers;
    } else {
        info->peer = t->spec.raddr;
        info->peer_port = t->spec.rport;
        info->via = FWD_VIA_UDP;
        if (t->q)
            fwd_tunq_info(t, info);
        info->peers = info->via == FWD_VIA_QUIC || info->via == FWD_VIA_UDP;
    }
    return FWD_OK;
}

int fwd_tunnel_list(fwd_ctx *ctx, int (*cb)(const fwd_tunnel_info *, void *),
                    void *ud)
{
    struct fwd_tun *t;
    for (t = ctx->tuns; t; t = t->next) {
        fwd_tunnel_info info;
        if (t->dead)
            continue;
        fwd_tunnel_get(ctx, t->id, &info);
        if (cb(&info, ud))
            break;
    }
    return FWD_OK;
}

int fwd_tunnel_peers(fwd_ctx *ctx, uint32_t id,
                     int (*cb)(const fwd_tunnel_peer *, void *), void *ud)
{
    struct fwd_tun *t = find_tun(ctx, id);
    int64_t now = fwd_sys_now_ms();
    fwd_tunnel_peer tp;
    int i;

    if (!t)
        return fwd_err(ctx, FWD_E_NOENT, "no tunnel with id %u", id);
    if (t->spec.mode == FWD_TUN_CONNECT) {
        fwd_tunnel_info info;
        fwd_tunnel_get(ctx, id, &info);
        if (info.peers) {
            memset(&tp, 0, sizeof tp);
            tp.addr = info.peer;
            tp.port = info.peer_port;
            tp.via = info.via;
            tp.tx_packets = info.tx_packets;
            tp.rx_packets = info.rx_packets;
            cb(&tp, ud);
        }
        return FWD_OK;
    }
    for (i = 0; i < FWD_TUN_MAX_PEERS; i++) {
        const struct fwd_tunpeer *p = &t->peers[i];
        if (!p->addrlen)
            continue;
        memset(&tp, 0, sizeof tp);
        fwd_sockaddr_to_addr(&p->addr, &tp.addr, &tp.port);
        tp.via = p->qc ? FWD_VIA_QUIC : FWD_VIA_UDP;
        tp.tx_packets = p->tx;
        tp.rx_packets = p->rx;
        tp.idle_ms = now > p->last_rx ? (uint32_t)(now - p->last_rx) : 0;
        if (cb(&tp, ud))
            break;
    }
    return FWD_OK;
}

#else /* !FWD_ENABLE_TUNTAP */

void fwd_tun_collect(fwd_ctx *ctx) { (void)ctx; }
void fwd_tun_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e, short r)
{
    (void)ctx, (void)e, (void)r;
}
void fwd_tun_reap(fwd_ctx *ctx) { (void)ctx; }
int fwd_tun_timeout(fwd_ctx *ctx, int timeout_ms)
{
    (void)ctx;
    return timeout_ms;
}
void fwd_tun_service(fwd_ctx *ctx) { (void)ctx; }
void fwd_tun_free_all(fwd_ctx *ctx) { (void)ctx; }

#define NOSYS(ctx) fwd_err((ctx), FWD_E_NOSYS, "tun/tap support disabled")
int fwd_tunnel_add(fwd_ctx *ctx, const fwd_tunnel_spec *s, uint32_t *id)
{
    (void)s, (void)id;
    return NOSYS(ctx);
}
int fwd_tunnel_del(fwd_ctx *ctx, uint32_t id)
{
    (void)id;
    return NOSYS(ctx);
}
int fwd_tunnel_mod(fwd_ctx *ctx, uint32_t id, const fwd_tunnel_spec *s)
{
    (void)id, (void)s;
    return NOSYS(ctx);
}
int fwd_tunnel_get(fwd_ctx *ctx, uint32_t id, fwd_tunnel_info *i)
{
    (void)id, (void)i;
    return NOSYS(ctx);
}
int fwd_tunnel_list(fwd_ctx *ctx, int (*cb)(const fwd_tunnel_info *, void *),
                    void *ud)
{
    (void)cb, (void)ud;
    return NOSYS(ctx);
}
int fwd_tunnel_peers(fwd_ctx *ctx, uint32_t id,
                     int (*cb)(const fwd_tunnel_peer *, void *), void *ud)
{
    (void)id, (void)cb, (void)ud;
    return NOSYS(ctx);
}

#endif
