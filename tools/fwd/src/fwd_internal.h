/* fwd_internal.h - private structures shared by the library modules. */
#ifndef FWD_INTERNAL_H
#define FWD_INTERNAL_H

#include "fwd.h"
#include "fwd_sys.h"

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef __GNUC__
#define FWD_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define FWD_PRINTF(a, b)
#endif

#define FWD_TCP_BUF 8192
#define FWD_PKT_BUF 65536

struct fwd_buf {
    uint8_t d[FWD_TCP_BUF];
    size_t off, len;
};

struct fwd_tcpconn {
    struct fwd_tcpconn *next;
    int a, b;             /* a = accepted client, b = upstream            */
    int connecting;       /* b: connect() still in progress               */
    int a_eof, b_eof;     /* read side closed by peer                     */
    int a_shut, b_shut;   /* we shut down the write side                  */
    int dead;
    struct fwd_buf ab;    /* data read from a, waiting to be sent to b    */
    struct fwd_buf ba;
};

struct fwd_fwd;

struct fwd_udpsess {
    struct fwd_udpsess *next;
    struct fwd_fwd *owner;
    struct sockaddr_storage client;
    socklen_t clen;
    int up;               /* connected socket towards the remote          */
    int64_t last;
    int dead;
};

struct fwd_fwd {
    struct fwd_fwd *next;
    uint32_t id;
    fwd_forward_spec spec;
    int lfd;
    int dead;
    struct fwd_tcpconn *conns;
    struct fwd_udpsess *sess;
};

struct tq_conn;

/* A peer of a listen mode tunnel. */
struct fwd_tunpeer {
    struct sockaddr_storage addr;
    socklen_t addrlen;            /* 0 = free slot                        */
    struct tq_conn *qc;           /* its QUIC connection, NULL = plain UDP */
    int64_t last_rx;
    uint64_t seq;                 /* fwd_tun.rx_seq when last heard from  */
    uint64_t tx, rx;
};

/* A learned inner address (IPv4 4, IPv6 16, MAC 6 bytes) -> peer. */
struct fwd_tunroute {
    uint8_t len;                  /* 0 = free                             */
    uint8_t peer;                 /* index into fwd_tun.peers             */
    uint8_t key[16];
    int64_t seen;
};

struct fwd_tun {
    struct fwd_tun *next;
    uint32_t id;
    fwd_tunnel_spec spec;
    int dev;              /* TUN/TAP file descriptor                      */
    int sock;             /* UDP socket, connected to the peer in connect
                             mode, only bound in listen mode              */
    int dead;
    uint64_t tx, rx;
    struct fwd_tunq *q;           /* QUIC transport, NULL = plain UDP only */
    /* listen mode */
    struct fwd_tunpeer *peers;    /* FWD_TUN_MAX_PEERS slots              */
    unsigned npeers;
    struct fwd_tunroute *routes;  /* FWD_TUN_MAX_ROUTES slots             */
    int64_t last_sweep;
    uint64_t rx_seq;
};

enum {
    FWD_PK_CTL = 1,
    FWD_PK_LISTEN,
    FWD_PK_TCP_A,
    FWD_PK_TCP_B,
    FWD_PK_UDP_UP,
    FWD_PK_TUN_DEV,
    FWD_PK_TUN_SOCK,
    FWD_PK_CTL_QUIC
};

struct fwd_pollent {
    int kind;
    void *obj;
};

struct fwd_ctx {
    fwd_config cfg;
    char last_msg[160];
    int last_errno;

    uint32_t next_fwd_id, next_tun_id;
    struct fwd_fwd *fwds;
    struct fwd_tun *tuns;
    unsigned nconns;

    int ctl_fd;
    struct fwd_quic *quic; /* QUIC control channel, NULL if not open     */
    struct fwd_tlsid *tunid; /* TLS identity of QUIC tunnels, NULL = not
                                made yet                                */
    int nl_fd;
    uint32_t nl_seq;

    struct pollfd *pfds;
    struct fwd_pollent *pents;
    size_t pn, pcap;

    uint8_t *pkt; /* FWD_PKT_BUF scratch */
    volatile sig_atomic_t stop;
    int in_run;
};

/* fwd_core.c */
int fwd_err(fwd_ctx *ctx, int rc, const char *fmt, ...) FWD_PRINTF(3, 4);
int fwd_err_sys(fwd_ctx *ctx, const char *what);
int fwd_addr_to_sockaddr(const fwd_addr *a, uint16_t port,
                         struct sockaddr_storage *ss, socklen_t *len);
int fwd_addr_usable(const fwd_addr *a);
int fwd_addr_equal(const fwd_addr *a, const fwd_addr *b);
int fwd_addr_is_zero(const fwd_addr *a);
int fwd_parse_pin(const char *text, uint8_t pin[32]);
int fwd_sockaddr_to_addr(const struct sockaddr_storage *ss, fwd_addr *a,
                         uint16_t *port);
int fwd_pollset_add(fwd_ctx *ctx, int fd, short events, int kind, void *obj);
int fwd_sysctl_enable_forwarding(fwd_ctx *ctx, int family);

/* fwd_forward.c */
void fwd_forward_collect(fwd_ctx *ctx);
void fwd_forward_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e,
                          short revents);
void fwd_forward_housekeeping(fwd_ctx *ctx, int64_t now);
void fwd_forward_reap(fwd_ctx *ctx);
void fwd_forward_free_all(fwd_ctx *ctx);

/* fwd_tun.c */
void fwd_tun_collect(fwd_ctx *ctx);
void fwd_tun_dispatch(fwd_ctx *ctx, const struct fwd_pollent *e,
                      short revents);
void fwd_tun_reap(fwd_ctx *ctx);
void fwd_tun_free_all(fwd_ctx *ctx);
int fwd_tun_timeout(fwd_ctx *ctx, int timeout_ms);
void fwd_tun_service(fwd_ctx *ctx);

/* Listen mode peers, for fwd_tunq.c. peer_add returns NULL if the peer
 * is not wanted; with qc it may make room by dropping another peer. */
struct fwd_tunpeer *fwd_tun_peer_add(struct fwd_tun *t,
                                     const struct sockaddr_storage *a,
                                     socklen_t al, struct tq_conn *qc);
void fwd_tun_peer_del(struct fwd_tun *t, struct fwd_tunpeer *p);
/* A packet that came from peer p (NULL in connect mode): learn its source,
 * write it to the device. */
void fwd_tun_from_peer(struct fwd_tun *t, struct fwd_tunpeer *p,
                       const uint8_t *pkt, size_t n);

/* fwd_tunq.c - QUIC transport of a tunnel. Only called with t->q set,
 * except start (which sets it) and ctx_free. */
int fwd_tunq_start(fwd_ctx *ctx, struct fwd_tun *t);
void fwd_tunq_stop(struct fwd_tun *t);
/* Connect mode, a packet from the device: 1 = taken care of (sent over
 * QUIC or dropped), 0 = send it as a plain datagram. */
int fwd_tunq_send(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                  size_t n);
/* A datagram from the socket (from: sender, NULL in connect mode):
 * 1 = taken care of (QUIC or dropped), 0 = a plain packet to deliver. */
/* Listen mode: send one packet on a peer's connection; 0 or -1. Closing
 * one (the peer is dropped) happens on the next service. */
int fwd_tunq_conn_send(struct tq_conn *c, const uint8_t *p, size_t n);
void fwd_tunq_conn_drop(struct tq_conn *c);
int fwd_tunq_input(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                   size_t n, const struct sockaddr_storage *from,
                   socklen_t fromlen);
void fwd_tunq_service(fwd_ctx *ctx, struct fwd_tun *t);
int fwd_tunq_timeout(struct fwd_tun *t, int timeout_ms);
int fwd_tunq_want_write(struct fwd_tun *t);
/* Connect mode: fill info->via. */
void fwd_tunq_info(struct fwd_tun *t, fwd_tunnel_info *info);
void fwd_tunq_ctx_free(fwd_ctx *ctx);

/* fwd_netlink.c */
int fwd_ifindex(fwd_ctx *ctx, const char *name);
int fwd_nl_addr(fwd_ctx *ctx, int add, const char *ifname, const fwd_addr *a,
                unsigned prefix);
void fwd_nl_close(fwd_ctx *ctx);

/* fwd_ctl.c */
void fwd_ctl_collect(fwd_ctx *ctx);
void fwd_ctl_dispatch(fwd_ctx *ctx, short revents);
int fwd_ctl_handle(fwd_ctx *ctx, char *in, size_t n, char *out, size_t outlen);

/* fwd_quic.c */
void fwd_quic_collect(fwd_ctx *ctx);
int fwd_quic_timeout(fwd_ctx *ctx, int timeout_ms);
void fwd_quic_service(fwd_ctx *ctx);

#endif /* FWD_INTERNAL_H */
