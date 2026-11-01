/*
 * fwd_netlink.c - routes and interface addresses through rtnetlink.
 *
 * Route add/delete/modify/list for IPv4 and IPv6. Adding or changing a route
 * also makes sure the matching forwarding sysctl is on (cfg.auto_sysctl).
 */
#include "fwd_internal.h"

#if FWD_ENABLE_NETLINK

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

/* ------------------------------------------------------------------ */
/* Message building                                                   */
/* ------------------------------------------------------------------ */
typedef struct nlbuf {
    char buf[1024];
    size_t len;
} nlbuf;

static void nlb_init(nlbuf *b, uint16_t type, uint16_t flags,
                     const void *body, size_t bodylen)
{
    struct nlmsghdr *h = (struct nlmsghdr *)b->buf;
    memset(b, 0, sizeof *b);
    h->nlmsg_type = type;
    h->nlmsg_flags = flags;
    memcpy(b->buf + NLMSG_HDRLEN, body, bodylen);
    b->len = NLMSG_ALIGN(NLMSG_HDRLEN + bodylen);
    h->nlmsg_len = (uint32_t)b->len;
}

static int nlb_attr(nlbuf *b, uint16_t type, const void *data, size_t n)
{
    struct rtattr *rta;
    if (b->len + RTA_ALIGN(RTA_LENGTH(n)) > sizeof b->buf)
        return -1;
    rta = (struct rtattr *)(b->buf + b->len);
    rta->rta_type = type;
    rta->rta_len = (unsigned short)RTA_LENGTH(n);
    memcpy(RTA_DATA(rta), data, n);
    b->len += RTA_ALIGN(rta->rta_len);
    ((struct nlmsghdr *)b->buf)->nlmsg_len = (uint32_t)b->len;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Socket and transaction                                             */
/* ------------------------------------------------------------------ */
static int nl_open(fwd_ctx *ctx)
{
    struct sockaddr_nl sa;
    struct timeval tv = {5, 0};
    int fd;

    if (ctx->nl_fd >= 0)
        return FWD_OK;
    fd = fwd_sys_socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return fwd_err_sys(ctx, "netlink socket");
    memset(&sa, 0, sizeof sa);
    sa.nl_family = AF_NETLINK;
    if (fwd_sys_bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        int e = errno;
        fwd_sys_close(fd);
        errno = e;
        return fwd_err_sys(ctx, "netlink bind");
    }
    fwd_sys_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ctx->nl_fd = fd;
    return FWD_OK;
}

void fwd_nl_close(fwd_ctx *ctx)
{
    if (ctx->nl_fd >= 0)
        fwd_sys_close(ctx->nl_fd);
    ctx->nl_fd = -1;
}

typedef int (*nl_cb)(const struct nlmsghdr *, void *);

/* Send one request, wait for its ack (or DONE for dumps). */
static int nl_talk(fwd_ctx *ctx, nlbuf *b, nl_cb cb, void *ud)
{
    struct nlmsghdr *h = (struct nlmsghdr *)b->buf;
    const size_t cap = 32768;
    char *rbuf;
    uint32_t seq;
    int rc;

    if ((rc = nl_open(ctx)) != FWD_OK)
        return rc;
    seq = ++ctx->nl_seq;
    h->nlmsg_seq = seq;
    h->nlmsg_pid = 0;
    if (fwd_sys_send(ctx->nl_fd, b->buf, b->len, 0) < 0)
        return fwd_err_sys(ctx, "netlink send");
    rbuf = FWD_MALLOC(cap);
    if (!rbuf)
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");

    for (;;) {
        ssize_t n = fwd_sys_recv(ctx->nl_fd, rbuf, cap, 0);
        struct nlmsghdr *m;
        int len;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            rc = fwd_err_sys(ctx, "netlink recv");
            break;
        }
        len = (int)n;
        for (m = (struct nlmsghdr *)rbuf; NLMSG_OK(m, len);
             m = NLMSG_NEXT(m, len)) {
            if (m->nlmsg_seq != seq)
                continue;
            if (m->nlmsg_type == NLMSG_DONE) {
                rc = FWD_OK;
                goto out;
            }
            if (m->nlmsg_type == NLMSG_ERROR) {
                const struct nlmsgerr *e = NLMSG_DATA(m);
                if (e->error == 0) {
                    rc = FWD_OK;
                } else {
                    errno = -e->error;
                    rc = fwd_err_sys(ctx, "netlink");
                }
                goto out;
            }
            if (cb)
                cb(m, ud);
        }
    }
out:
    FWD_FREE(rbuf);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Interface lookup (ioctl, works on any socket)                      */
/* ------------------------------------------------------------------ */
static int ctl_sock(void)
{
    int s = fwd_sys_socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        s = fwd_sys_socket(AF_INET6, SOCK_DGRAM, 0);
    return s;
}

int fwd_ifindex(fwd_ctx *ctx, const char *name)
{
    struct ifreq ifr;
    int s, idx, e;

    if (strlen(name) >= IFNAMSIZ)
        return fwd_err(ctx, FWD_E_RANGE, "interface name too long");
    s = ctl_sock();
    if (s < 0)
        return fwd_err_sys(ctx, "socket");
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
    if (fwd_sys_ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        e = errno;
        fwd_sys_close(s);
        errno = e;
        return fwd_err_sys(ctx, name);
    }
    idx = ifr.ifr_ifindex;
    fwd_sys_close(s);
    return idx;
}

static int ifname_of(int sock, int idx, char *out)
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_ifindex = idx;
    if (sock < 0 || fwd_sys_ioctl(sock, SIOCGIFNAME, &ifr) < 0)
        return -1;
    snprintf(out, FWD_IFNAME_MAX, "%.*s", FWD_IFNAME_MAX - 1, ifr.ifr_name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Interface addresses                                                */
/* ------------------------------------------------------------------ */
int fwd_nl_addr(fwd_ctx *ctx, int add, const char *ifname, const fwd_addr *a,
                unsigned prefix)
{
    struct ifaddrmsg am;
    nlbuf b;
    size_t alen = a->family == FWD_V6 ? 16 : 4;
    int idx;

    if (!fwd_addr_usable(a))
        return fwd_err(ctx, FWD_E_INVAL, "unsupported address family");
    if ((idx = fwd_ifindex(ctx, ifname)) < 0)
        return idx;
    memset(&am, 0, sizeof am);
    am.ifa_family = a->family == FWD_V6 ? AF_INET6 : AF_INET;
    am.ifa_prefixlen = (unsigned char)prefix;
    am.ifa_scope = RT_SCOPE_UNIVERSE;
    am.ifa_index = (unsigned)idx;
    nlb_init(&b, add ? RTM_NEWADDR : RTM_DELADDR,
             (uint16_t)(NLM_F_REQUEST | NLM_F_ACK |
                        (add ? NLM_F_CREATE | NLM_F_EXCL : 0)),
             &am, sizeof am);
    if (nlb_attr(&b, IFA_LOCAL, a->bytes, alen) ||
        nlb_attr(&b, IFA_ADDRESS, a->bytes, alen))
        return fwd_err(ctx, FWD_E_BUF, "netlink message too large");
    return nl_talk(ctx, &b, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Routes                                                             */
/* ------------------------------------------------------------------ */
enum { OP_ADD, OP_DEL, OP_MOD };

static int route_op(fwd_ctx *ctx, int op, const fwd_route_spec *r)
{
    struct rtmsg rm;
    nlbuf b;
    uint16_t flags = NLM_F_REQUEST | NLM_F_ACK;
    uint32_t table, metric;
    size_t alen;
    unsigned maxp;
    int has_gw, idx = 0, rc;

    if (!r)
        return FWD_E_INVAL;
    metric = r->metric;
    if (!fwd_addr_usable(&r->dst))
        return fwd_err(ctx, FWD_E_INVAL,
                       "route destination: unsupported address family");
    alen = r->dst.family == FWD_V6 ? 16 : 4;
    maxp = r->dst.family == FWD_V6 ? 128 : 32;
    if (r->prefix > maxp)
        return fwd_err(ctx, FWD_E_RANGE, "prefix length too large");
    has_gw = !fwd_addr_is_zero(&r->gateway);
    if (has_gw && r->gateway.family != r->dst.family)
        return fwd_err(ctx, FWD_E_INVAL,
                       "gateway and destination family differ");
    if (r->ifname[0] && (idx = fwd_ifindex(ctx, r->ifname)) < 0)
        return idx;
    if (op != OP_DEL && !has_gw && !idx)
        return fwd_err(ctx, FWD_E_INVAL,
                       "route needs a gateway or an interface");

    if (op != OP_DEL &&
        (rc = fwd_sysctl_enable_forwarding(ctx, r->dst.family)) != FWD_OK)
        return rc;

    table = r->table ? r->table : RT_TABLE_MAIN;
    memset(&rm, 0, sizeof rm);
    rm.rtm_family = r->dst.family == FWD_V6 ? AF_INET6 : AF_INET;
    rm.rtm_dst_len = r->prefix;
    rm.rtm_table = table < 256 ? (unsigned char)table : RT_TABLE_UNSPEC;
    if (op == OP_DEL) {
        rm.rtm_scope = RT_SCOPE_NOWHERE;
    } else {
        rm.rtm_protocol = RTPROT_STATIC;
        rm.rtm_scope = has_gw ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
        rm.rtm_type = RTN_UNICAST;
    }
    if (op == OP_ADD)
        flags |= NLM_F_CREATE | NLM_F_EXCL;
    else if (op == OP_MOD)
        flags |= NLM_F_REPLACE;

    nlb_init(&b, op == OP_DEL ? RTM_DELROUTE : RTM_NEWROUTE, flags, &rm,
             sizeof rm);
    if ((r->prefix && nlb_attr(&b, RTA_DST, r->dst.bytes, alen)) ||
        (has_gw && nlb_attr(&b, RTA_GATEWAY, r->gateway.bytes, alen)) ||
        (idx && nlb_attr(&b, RTA_OIF, &idx, sizeof idx)) ||
        (metric && nlb_attr(&b, RTA_PRIORITY, &metric, sizeof metric)) ||
        nlb_attr(&b, RTA_TABLE, &table, sizeof table))
        return fwd_err(ctx, FWD_E_BUF, "netlink message too large");
    return nl_talk(ctx, &b, NULL, NULL);
}

int fwd_route_add(fwd_ctx *ctx, const fwd_route_spec *r)
{
    return route_op(ctx, OP_ADD, r);
}
int fwd_route_del(fwd_ctx *ctx, const fwd_route_spec *r)
{
    return route_op(ctx, OP_DEL, r);
}
int fwd_route_mod(fwd_ctx *ctx, const fwd_route_spec *r)
{
    return route_op(ctx, OP_MOD, r);
}

struct dump {
    int sock;
    int (*cb)(const fwd_route_spec *, void *);
    void *ud;
    int stop;
};

static int dump_cb(const struct nlmsghdr *h, void *p)
{
    struct dump *d = p;
    const struct rtmsg *rm;
    const struct rtattr *rta;
    fwd_route_spec r;
    uint8_t fam;
    size_t alen;
    int len;

    if (d->stop || h->nlmsg_type != RTM_NEWROUTE)
        return 0;
    rm = NLMSG_DATA(h);
    if (rm->rtm_type != RTN_UNICAST)
        return 0;
    if (rm->rtm_family == AF_INET && FWD_ENABLE_IPV4)
        fam = FWD_V4, alen = 4;
    else if (rm->rtm_family == AF_INET6 && FWD_ENABLE_IPV6)
        fam = FWD_V6, alen = 16;
    else
        return 0;

    memset(&r, 0, sizeof r);
    r.dst.family = fam;
    r.gateway.family = fam;
    r.prefix = rm->rtm_dst_len;
    r.table = rm->rtm_table;

    len = (int)RTM_PAYLOAD(h);
    for (rta = RTM_RTA(rm); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        size_t pl = RTA_PAYLOAD(rta);
        switch (rta->rta_type) {
        case RTA_DST:
            if (pl == alen)
                memcpy(r.dst.bytes, RTA_DATA(rta), alen);
            break;
        case RTA_GATEWAY:
            if (pl == alen)
                memcpy(r.gateway.bytes, RTA_DATA(rta), alen);
            break;
        case RTA_OIF:
            if (pl == sizeof(int)) {
                int idx;
                memcpy(&idx, RTA_DATA(rta), sizeof idx);
                ifname_of(d->sock, idx, r.ifname);
            }
            break;
        case RTA_PRIORITY:
            if (pl == sizeof(uint32_t))
                memcpy(&r.metric, RTA_DATA(rta), sizeof r.metric);
            break;
        case RTA_TABLE:
            if (pl == sizeof(uint32_t))
                memcpy(&r.table, RTA_DATA(rta), sizeof r.table);
            break;
        }
    }
    if (d->cb(&r, d->ud))
        d->stop = 1;
    return 0;
}

int fwd_route_list(fwd_ctx *ctx, int (*cb)(const fwd_route_spec *, void *),
                   void *ud)
{
    struct rtgenmsg g;
    struct dump d;
    nlbuf b;
    int rc;

    memset(&g, 0, sizeof g);
    g.rtgen_family = AF_UNSPEC;
    nlb_init(&b, RTM_GETROUTE, NLM_F_REQUEST | NLM_F_DUMP, &g, sizeof g);
    d.sock = ctl_sock();
    d.cb = cb;
    d.ud = ud;
    d.stop = 0;
    rc = nl_talk(ctx, &b, dump_cb, &d);
    if (d.sock >= 0)
        fwd_sys_close(d.sock);
    return rc;
}

#else /* !FWD_ENABLE_NETLINK */

#define NOSYS(ctx) fwd_err((ctx), FWD_E_NOSYS, "netlink support disabled")

int fwd_ifindex(fwd_ctx *ctx, const char *name)
{
    (void)name;
    return NOSYS(ctx);
}
int fwd_nl_addr(fwd_ctx *ctx, int add, const char *ifname, const fwd_addr *a,
                unsigned prefix)
{
    (void)add, (void)ifname, (void)a, (void)prefix;
    return NOSYS(ctx);
}
void fwd_nl_close(fwd_ctx *ctx) { (void)ctx; }
int fwd_route_add(fwd_ctx *ctx, const fwd_route_spec *r)
{
    (void)r;
    return NOSYS(ctx);
}
int fwd_route_del(fwd_ctx *ctx, const fwd_route_spec *r)
{
    (void)r;
    return NOSYS(ctx);
}
int fwd_route_mod(fwd_ctx *ctx, const fwd_route_spec *r)
{
    (void)r;
    return NOSYS(ctx);
}
int fwd_route_list(fwd_ctx *ctx, int (*cb)(const fwd_route_spec *, void *),
                   void *ud)
{
    (void)cb, (void)ud;
    return NOSYS(ctx);
}

#endif
