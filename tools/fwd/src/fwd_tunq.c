/*
 * fwd_tunq.c - QUIC transport of TUN/TAP tunnels (see fwd_tunnel_spec).
 *
 * Every QUIC tunnel has a listener on the tunnel socket, so both ends can
 * accept a connection; a connect mode tunnel also opens one to its peer
 * from that listener (SSL_new_from_listener). fwd_tun.c keeps reading the
 * socket: datagrams that belong to QUIC are handed to the listener with
 * SSL_inject_net_dgram(), the rest are plain tunnel packets. The BIO only
 * sends.
 *
 * Telling them apart: a QUIC long header packet starts with 0b11 and a
 * 32 bit version, which neither IPv4 (0x4_) nor IPv6 (0x6_) packets do.
 * Short header packets cannot be recognised, they are QUIC when they come
 * from a source that is talking QUIC (connect mode: the peer, as long as a
 * connection is up or being made; listen mode: recent long header senders
 * and the connected peer).
 *
 * Listen mode keeps every accepted connection (up to FWD_TUN_MAX_PEERS
 * plus some still in their handshake); one that is up is a peer of the
 * tunnel (fwd_tun_peer_add), which routes device packets to it.
 *
 * The address of an accepted client is the sender of the long header packet
 * whose processing put the connection on the accept queue: those packets
 * are processed and accepted right away, one by one.
 *
 * Packets travel on one bidirectional stream opened by the connecting side
 * as <2 byte big endian length><packet>; length 0 is a keepalive.
 */
#include "fwd_tls.h"

#if FWD_ENABLE_TUNTAP && FWD_ENABLE_QUIC

#include <openssl/pem.h>

#define TQ_HANDSHAKE_MS 3000     /* no QUIC answer in time -> fallback      */
#define TQ_RETRY_MS 10000        /* first retry after fallback / rejection  */
#define TQ_RETRY_MAX_MS 300000
#define TQ_KEEPALIVE_MS 5000
#define TQ_IDLE_MS 15000         /* a peer that went away (restart) is
                                    noticed after this at the latest       */
#define TQ_SRC_SLOTS 8           /* listen mode: QUIC talking sources       */
#define TQ_SRC_IDLE_MS 60000
#define TQ_FRAME_MAX (2 + 65535)
#define TQ_MAX_LCONNS (FWD_TUN_MAX_PEERS + 16) /* listen: incl. handshakes */

static const unsigned char tun_alpn[] = "\x08" FWD_TUN_ALPN;

struct fwd_tlsid {
    EVP_PKEY *key;
    X509 *cert;
    STACK_OF(X509) *chain;
    char *ca_file;
};

struct tq_conn {
    struct tq_conn *next;          /* listen mode list                   */
    struct fwd_tunpeer *tp;        /* listen mode: its peer, once up     */
    int dropped;                   /* close on the next service          */
    SSL *c, *st;                   /* connection, packet stream          */
    int outgoing;
    int ready;                     /* stream open and accepted           */
    struct sockaddr_storage peer;  /* incoming: the client's address     */
    socklen_t peerlen;
    int64_t last_tx;
    size_t rlen, plen, poff;
    uint8_t rbuf[TQ_FRAME_MAX];    /* received, not yet a whole frame    */
    uint8_t pbuf[TQ_FRAME_MAX];    /* frame not completely written yet   */
};

/* connect side states */
enum { TQ_TRYING, TQ_UP, TQ_FALLBACK, TQ_NOANSWER, TQ_REJECTED };

struct fwd_tunq {
    SSL_CTX *sctx;
    SSL *lst;
    struct tq_conn *out;           /* ours (connect mode)                */
    struct tq_conn *in;            /* connect mode: accepted and ready   */
    struct tq_conn *pend;          /* connect mode: accepted, handshake
                                      still running                      */
    struct tq_conn *lconns;        /* listen mode: all accepted          */
    unsigned nlconns;
    int state, probe, heard;
    int64_t deadline, retry_at;
    int backoff;
    uint8_t pin[32];
    int has_pin, trust;            /* trust: the peer is verified        */
    struct sockaddr_storage peer;  /* connect mode: raddr:rport          */
    socklen_t peerlen;
    struct {
        struct sockaddr_storage a;
        socklen_t l;
        int64_t seen;
    } src[TQ_SRC_SLOTS];
};

/* ------------------------------------------------------------------ */
/* TLS identity                                                       */
/* ------------------------------------------------------------------ */
static void tlsid_free(struct fwd_tlsid *id)
{
    if (!id)
        return;
    EVP_PKEY_free(id->key);
    X509_free(id->cert);
    OSSL_STACK_OF_X509_free(id->chain);
    FWD_FREE(id->ca_file);
    FWD_FREE(id);
}

void fwd_tunq_ctx_free(fwd_ctx *ctx)
{
    tlsid_free(ctx->tunid);
    ctx->tunid = NULL;
}

static int load_id(struct fwd_tlsid *id, const char *cert_file,
                   const char *key_file, char *err, size_t errlen)
{
    BIO *b;
    X509 *x;

    if (!(b = BIO_new_file(cert_file, "r")) ||
        !(id->cert = PEM_read_bio_X509(b, NULL, NULL, NULL))) {
        BIO_free(b);
        fwd_tls_errmsg(err, errlen, "tunnel certificate");
        return 0;
    }
    if (!(id->chain = sk_X509_new_null())) {
        BIO_free(b);
        return 0;
    }
    while ((x = PEM_read_bio_X509(b, NULL, NULL, NULL)))
        if (!sk_X509_push(id->chain, x)) {
            X509_free(x);
            BIO_free(b);
            return 0;
        }
    BIO_free(b);
    ERR_clear_error(); /* the end of the file */
    if (!(b = BIO_new_file(key_file, "r")) ||
        !(id->key = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL))) {
        BIO_free(b);
        fwd_tls_errmsg(err, errlen, "tunnel private key");
        return 0;
    }
    BIO_free(b);
    if (!X509_check_private_key(id->cert, id->key)) {
        fwd_tls_errmsg(err, errlen, "tunnel key does not match certificate");
        return 0;
    }
    return 1;
}

int fwd_tunnel_tls(fwd_ctx *ctx, const char *cert_file, const char *key_file,
                   const char *ca_file)
{
    struct fwd_tlsid *id;
    struct fwd_tun *t;
    char err[sizeof ctx->last_msg];
    BIO *b;

    if (!ctx || !cert_file != !key_file)
        return FWD_E_INVAL;
    if (!(id = FWD_CALLOC(1, sizeof *id)))
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
    ERR_clear_error();
    err[0] = '\0';
    if (cert_file && !load_id(id, cert_file, key_file, err, sizeof err))
        goto fail;
    if (ca_file) {
        /* only checked for readability here; loaded per tunnel */
        if (!(b = BIO_new_file(ca_file, "r"))) {
            fwd_tls_errmsg(err, sizeof err, "tunnel CA file");
            goto fail;
        }
        BIO_free(b);
        if (!(id->ca_file = FWD_MALLOC(strlen(ca_file) + 1))) {
            tlsid_free(id);
            return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
        }
        strcpy(id->ca_file, ca_file);
    }
    tlsid_free(ctx->tunid);
    ctx->tunid = id;
    ERR_clear_error();
    for (t = ctx->tuns; t; t = t->next)
        if (!t->dead && t->spec.transport != FWD_TT_UDP) {
            fwd_tunq_stop(t);
            fwd_tunq_start(ctx, t);
        }
    return FWD_OK;
fail:
    tlsid_free(id);
    ERR_clear_error();
    return fwd_err(ctx, FWD_E_TLS, "%s", err[0] ? err : "tunnel TLS setup");
}

/* The identity, with a self-signed certificate made if none was set. */
static struct fwd_tlsid *get_id(fwd_ctx *ctx)
{
    struct fwd_tlsid *id = ctx->tunid;

    if (!id) {
        if (!(id = FWD_CALLOC(1, sizeof *id)))
            return NULL;
        ctx->tunid = id;
    }
    if (!id->cert &&
        !fwd_tls_make_cert(NULL, "fwd-tunnel", 3650, &id->key, &id->cert))
        return NULL;
    return id;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
static int64_t now_ms(void)
{
    return fwd_sys_now_ms();
}

static int same_sockaddr(const struct sockaddr_storage *a, socklen_t al,
                         const struct sockaddr_storage *b, socklen_t bl)
{
    fwd_addr x, y;
    uint16_t xp, yp;

    if (!al || !bl || fwd_sockaddr_to_addr(a, &x, &xp) != FWD_OK ||
        fwd_sockaddr_to_addr(b, &y, &yp) != FWD_OK)
        return 0;
    return xp == yp && fwd_addr_equal(&x, &y);
}

/* QUIC v1 or v2 long header (Initial, 0-RTT, Handshake, Retry). */
static int quic_long(const uint8_t *p, size_t n)
{
    uint32_t v;

    if (n < 7 || (p[0] & 0xC0) != 0xC0)
        return 0;
    v = (uint32_t)p[1] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 8 |
        p[4];
    return v == 0x00000001u || v == 0x6b3343cfu;
}

/* TUN: could this be a plain IPv4/IPv6 packet (version and length agree)?
 * A QUIC short header packet passes by chance at most once in 65536. */
static int plain_ip(const uint8_t *p, size_t n)
{
    if (n >= 20 && p[0] >> 4 == 4 && (p[0] & 15) >= 5)
        return ((size_t)p[2] << 8 | p[3]) == n;
    if (n >= 40 && p[0] >> 4 == 6)
        return ((size_t)p[4] << 8 | p[5]) + 40 == n;
    return 0;
}

/* Who may be the peer: the pin, else the CA file, else anybody. */
static int verify_peer(X509_STORE_CTX *xs, void *arg)
{
    struct fwd_tunq *q = arg;
    unsigned char md[32];

    if (q->has_pin) {
        if (fwd_tls_cert_sha256(X509_STORE_CTX_get0_cert(xs), md) == 0 &&
            CRYPTO_memcmp(md, q->pin, 32) == 0)
            return 1;
        X509_STORE_CTX_set_error(xs, X509_V_ERR_CERT_REJECTED);
        return 0;
    }
    return X509_verify_cert(xs);
}

static int alpn_select(SSL *ssl, const unsigned char **out,
                       unsigned char *outlen, const unsigned char *in,
                       unsigned int inlen, void *arg)
{
    (void)ssl, (void)arg;
    if (SSL_select_next_proto((unsigned char **)out, outlen, tun_alpn,
                              sizeof tun_alpn - 1, in,
                              inlen) != OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    return SSL_TLSEXT_ERR_OK;
}

/* ------------------------------------------------------------------ */
/* connections                                                        */
/* ------------------------------------------------------------------ */
static void conn_free(struct tq_conn **pc)
{
    struct tq_conn *c = *pc;

    if (!c)
        return;
    SSL_free(c->st);
    if (c->c) {
        /* tell the peer (one CONNECTION_CLOSE, not waited for) so that it
         * reconnects now instead of after the idle timeout */
        (void)SSL_shutdown_ex(c->c, SSL_SHUTDOWN_FLAG_RAPID |
                                        SSL_SHUTDOWN_FLAG_NO_BLOCK,
                              NULL, 0);
        SSL_handle_events(c->c);
        SSL_free(c->c);
    }
    FWD_FREE(c);
    *pc = NULL;
}

static int conn_dead(struct tq_conn *c)
{
    SSL_CONN_CLOSE_INFO ci;
    return SSL_get_conn_close_info(c->c, &ci, sizeof ci) == 1;
}

/* Write what is left of the pending frame: 0 done, 1 blocked, -1 error. */
static int conn_flush(struct tq_conn *c)
{
    size_t n;

    while (c->poff < c->plen) {
        if (!SSL_write_ex(c->st, c->pbuf + c->poff, c->plen - c->poff, &n))
            return SSL_get_error(c->st, 0) == SSL_ERROR_WANT_WRITE ? 1 : -1;
        c->poff += n;
    }
    c->plen = c->poff = 0;
    return 0;
}

/* One frame. A frame that cannot even start because the previous one is
 * still stuck is dropped, like a full queue on a real link. */
static int conn_send(struct tq_conn *c, const uint8_t *p, size_t n)
{
    int rc;

    if (c->plen && (rc = conn_flush(c)) != 0)
        return rc;
    c->pbuf[0] = (uint8_t)(n >> 8);
    c->pbuf[1] = (uint8_t)n;
    if (n)
        memcpy(c->pbuf + 2, p, n);
    c->plen = n + 2;
    c->poff = 0;
    c->last_tx = now_ms();
    return conn_flush(c) < 0 ? -1 : 0;
}

/* Frames from the stream to the device. -1: the stream is gone. */
static int conn_read(struct fwd_tun *t, struct tq_conn *c)
{
    size_t n, off;

    for (;;) {
        if (!SSL_read_ex(c->st, c->rbuf + c->rlen, sizeof c->rbuf - c->rlen,
                         &n))
            return SSL_get_error(c->st, 0) == SSL_ERROR_WANT_READ ? 0 : -1;
        c->rlen += n;
        for (off = 0; c->rlen - off >= 2;) {
            size_t len = (size_t)c->rbuf[off] << 8 | c->rbuf[off + 1];
            if (c->rlen - off - 2 < len)
                break;
            if (len)
                fwd_tun_from_peer(t, c->tp, c->rbuf + off + 2, len);
            off += 2 + len;
            c->ready = 1; /* the peer accepted us (see open_stream) */
        }
        memmove(c->rbuf, c->rbuf + off, c->rlen - off);
        c->rlen -= off;
    }
}

/* After the TLS handshake: the connecting side opens the stream (the
 * peer only sees it once something is sent on it), the accepting side
 * takes it and answers with an empty frame. That answer is what makes the
 * connecting side ready: its handshake completes before the peer has
 * checked the client certificate. */
static int open_stream(struct tq_conn *c)
{
    if (c->outgoing)
        c->st = SSL_new_stream(c->c, 0);
    else
        c->st = SSL_accept_stream(c->c, SSL_ACCEPT_STREAM_NO_BLOCK);
    if (!c->st)
        return 0;
    (void)SSL_set_mode(c->st, SSL_MODE_ENABLE_PARTIAL_WRITE |
                                  SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    if (conn_send(c, NULL, 0) < 0)
        return -1;
    if (!c->outgoing)
        c->ready = 1;
    return 0;
}

/* Drive a connection: 0 fine, -1 finished (close it). */
static int conn_step(struct fwd_tun *t, struct tq_conn *c)
{
    int r;

    if (conn_dead(c))
        return -1;
    if (!c->st) {
        /* a rejected certificate ends the attempt now, not only when the
         * connection has finished closing */
        if (t->q->trust && SSL_get_verify_result(c->c) != X509_V_OK)
            return -1;
        r = SSL_do_handshake(c->c);
        if (r != 1) {
            int e = SSL_get_error(c->c, r);
            return e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE ? 0
                                                                       : -1;
        }
        if (open_stream(c) < 0)
            return -1;
        if (!c->st)
            return 0;
        if (!c->outgoing && t->spec.mode == FWD_TUN_LISTEN &&
            !(c->tp = fwd_tun_peer_add(t, &c->peer, c->peerlen, c)))
            return -1;
    }
    if (c->plen && conn_flush(c) < 0)
        return -1;
    if (conn_read(t, c) < 0)
        return -1;
    if (c->outgoing && c->ready && now_ms() - c->last_tx >= TQ_KEEPALIVE_MS &&
        conn_send(c, NULL, 0) < 0)
        return -1;
    return 0;
}

static struct tq_conn *active(struct fwd_tunq *q)
{
    if (q->out && q->out->ready)
        return q->out;
    if (q->in && q->in->ready)
        return q->in;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* connect side                                                       */
/* ------------------------------------------------------------------ */
static void start_attempt(struct fwd_tunq *q, int probe)
{
    struct tq_conn *c;
    BIO_ADDR *peer = NULL;

    q->state = TQ_TRYING;
    q->probe = probe;
    q->heard = 0;
    q->deadline = now_ms() + TQ_HANDSHAKE_MS;
    if (!(c = FWD_CALLOC(1, sizeof *c)))
        return; /* counts as no answer at the deadline */
    c->outgoing = 1;
    if (!(c->c = SSL_new_from_listener(q->lst, 0)) ||
        !(peer = BIO_ADDR_new()) ||
        !fwd_tls_sockaddr_to_bioaddr(peer, &q->peer) ||
        !SSL_set1_initial_peer_addr(c->c, peer) ||
        SSL_set_alpn_protos(c->c, tun_alpn, sizeof tun_alpn - 1) != 0 ||
        !SSL_set_blocking_mode(c->c, 0) ||
        !SSL_set_feature_request_uint(c->c, SSL_VALUE_QUIC_IDLE_TIMEOUT,
                                      TQ_IDLE_MS) ||
        !SSL_set_default_stream_mode(c->c, SSL_DEFAULT_STREAM_MODE_NONE) ||
        !SSL_set_incoming_stream_policy(c->c,
                                        SSL_INCOMING_STREAM_POLICY_REJECT, 0))
        conn_free(&c);
    BIO_ADDR_free(peer);
    q->out = c;
    if (c)
        (void)SSL_connect(c->c); /* sends the first Initial */
    ERR_clear_error();
}

static void retry_later(struct fwd_tunq *q, int state)
{
    q->state = state;
    q->retry_at = now_ms() + q->backoff;
    q->backoff = q->backoff * 2 > TQ_RETRY_MAX_MS ? TQ_RETRY_MAX_MS
                                                  : q->backoff * 2;
}

/* The attempt did not get a connection up. A peer that answered in QUIC
 * but failed the handshake is not fallen back from: that would let an
 * attacker downgrade a pinned tunnel by sending a bad certificate. */
static void attempt_failed(const struct fwd_tun *t, struct fwd_tunq *q)
{
    retry_later(q, q->heard ? TQ_REJECTED
                   : t->spec.transport == FWD_TT_AUTO ? TQ_FALLBACK
                                                      : TQ_NOANSWER);
}

static void connect_service(struct fwd_tun *t, struct fwd_tunq *q)
{
    int64_t now = now_ms();

    if (q->out && conn_step(t, q->out) < 0) {
        int was_ready = q->out->ready;
        conn_free(&q->out);
        if (q->state == TQ_TRYING && !was_ready)
            attempt_failed(t, q);
    }
    if (q->state == TQ_TRYING && q->out && !q->out->ready &&
        now >= q->deadline) {
        conn_free(&q->out);
        attempt_failed(t, q);
    }
    if (active(q)) {
        q->state = TQ_UP;
        q->backoff = TQ_RETRY_MS;
        return;
    }
    switch (q->state) {
    case TQ_UP: /* lost it: try again right away */
        start_attempt(q, 0);
        break;
    case TQ_TRYING:
        if (!q->out && now >= q->deadline) /* could not even start */
            attempt_failed(t, q);
        break;
    default:
        if (now >= q->retry_at)
            start_attempt(q, q->state == TQ_FALLBACK);
        break;
    }
}

/* Plain UDP towards / from the peer allowed right now? */
static int connect_raw_ok(const struct fwd_tun *t, const struct fwd_tunq *q)
{
    return t->spec.transport == FWD_TT_AUTO &&
           (q->state == TQ_FALLBACK || (q->state == TQ_TRYING && q->probe));
}

/* ------------------------------------------------------------------ */
/* listen side                                                        */
/* ------------------------------------------------------------------ */
static int src_known(struct fwd_tunq *q, const struct sockaddr_storage *a,
                     socklen_t l, int64_t now)
{
    int i;

    for (i = 0; i < TQ_SRC_SLOTS; i++)
        if (q->src[i].l && now - q->src[i].seen < TQ_SRC_IDLE_MS &&
            same_sockaddr(&q->src[i].a, q->src[i].l, a, l)) {
            q->src[i].seen = now;
            return 1;
        }
    return 0;
}

static void src_add(struct fwd_tunq *q, const struct sockaddr_storage *a,
                    socklen_t l, int64_t now)
{
    int i, old = 0;

    if (src_known(q, a, l, now))
        return;
    for (i = 1; i < TQ_SRC_SLOTS; i++)
        if (q->src[i].seen < q->src[old].seen)
            old = i;
    q->src[old].a = *a;
    q->src[old].l = l;
    q->src[old].seen = now;
}

static void src_forget(struct fwd_tunq *q, const struct tq_conn *c)
{
    int i;

    for (i = 0; c && i < TQ_SRC_SLOTS; i++)
        if (same_sockaddr(&q->src[i].a, q->src[i].l, &c->peer, c->peerlen))
            q->src[i].l = 0;
}

static int has_quic_peer(const struct fwd_tunq *q)
{
    const struct tq_conn *c;
    for (c = q->lconns; c; c = c->next)
        if (c->tp)
            return 1;
    return 0;
}

/* Close a listen mode connection, and drop its peer. */
static void lconn_free(struct fwd_tun *t, struct fwd_tunq *q,
                       struct tq_conn *c)
{
    struct fwd_tunpeer *tp = c->tp;

    if (tp) {
        c->tp = NULL;
        tp->qc = NULL; /* so that it does not call back */
        fwd_tun_peer_del(t, tp);
    }
    src_forget(q, c);
    q->nlconns--;
    conn_free(&c);
}

/* from: the sender of the datagram just processed, if any */
static void accept_conns(struct fwd_tun *t, struct fwd_tunq *q,
                         const struct sockaddr_storage *from,
                         socklen_t fromlen)
{
    struct tq_conn *c;
    SSL *s;

    while ((s = SSL_accept_connection(q->lst,
                                      SSL_ACCEPT_CONNECTION_NO_BLOCK))) {
        if (!(c = FWD_CALLOC(1, sizeof *c))) {
            SSL_free(s);
            continue;
        }
        c->c = s;
        if (from) {
            c->peer = *from;
            c->peerlen = fromlen;
        }
        (void)SSL_set_blocking_mode(s, 0);
        (void)SSL_set_default_stream_mode(s, SSL_DEFAULT_STREAM_MODE_NONE);
        (void)SSL_set_incoming_stream_policy(
            s, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0);
        if (t->spec.mode == FWD_TUN_LISTEN) {
            if (q->nlconns >= TQ_MAX_LCONNS) {
                conn_free(&c);
                continue;
            }
            c->next = q->lconns;
            q->lconns = c;
            q->nlconns++;
        } else {
            conn_free(&q->pend); /* the newest client wins */
            q->pend = c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* interface for fwd_tun.c                                            */
/* ------------------------------------------------------------------ */
void fwd_tunq_stop(struct fwd_tun *t)
{
    struct fwd_tunq *q = t->q;

    if (!q)
        return;
    conn_free(&q->out);
    conn_free(&q->in);
    conn_free(&q->pend);
    while (q->lconns) {
        struct tq_conn *c = q->lconns;
        q->lconns = c->next;
        lconn_free(t, q, c);
    }
    SSL_free(q->lst);
    SSL_CTX_free(q->sctx);
    FWD_FREE(q);
    t->q = NULL;
}

static int start_fail(fwd_ctx *ctx, struct fwd_tun *t, const char *what)
{
    char msg[sizeof ctx->last_msg];

    fwd_tls_errmsg(msg, sizeof msg, what);
    ERR_clear_error();
    fwd_tunq_stop(t);
    return fwd_err(ctx, FWD_E_TLS, "%s", msg);
}

int fwd_tunq_start(fwd_ctx *ctx, struct fwd_tun *t)
{
    struct fwd_tlsid *id;
    struct fwd_tunq *q;
    BIO *bio;
    int trust;

    if (t->spec.transport == FWD_TT_UDP)
        return FWD_OK;
    if (!(q = FWD_CALLOC(1, sizeof *q)))
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
    t->q = q;
    q->backoff = TQ_RETRY_MS;
    q->has_pin = t->spec.pin[0] != '\0';
    if (q->has_pin && fwd_parse_pin(t->spec.pin, q->pin) < 0)
        return start_fail(ctx, t, "bad pin");
    if (t->spec.mode == FWD_TUN_CONNECT &&
        fwd_addr_to_sockaddr(&t->spec.raddr, t->spec.rport, &q->peer,
                             &q->peerlen) != FWD_OK)
        return start_fail(ctx, t, "bad peer address");

    ERR_clear_error();
    if (!(id = get_id(ctx)))
        return start_fail(ctx, t, "tunnel certificate");
    trust = q->trust = q->has_pin || id->ca_file;
    if (!(q->sctx = SSL_CTX_new(OSSL_QUIC_server_method())))
        return start_fail(ctx, t, "SSL_CTX_new");
    if (!SSL_CTX_use_cert_and_key(q->sctx, id->cert, id->key, id->chain, 1))
        return start_fail(ctx, t, "tunnel certificate");
    if (!q->has_pin && id->ca_file) {
        if (!SSL_CTX_load_verify_locations(q->sctx, id->ca_file, NULL))
            return start_fail(ctx, t, "tunnel CA file");
        X509_VERIFY_PARAM_set_flags(SSL_CTX_get0_param(q->sctx),
                                    X509_V_FLAG_PARTIAL_CHAIN);
    }
    SSL_CTX_set_verify(q->sctx,
                       trust ? SSL_VERIFY_PEER |
                                   SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                             : SSL_VERIFY_NONE,
                       NULL);
    SSL_CTX_set_cert_verify_callback(q->sctx, verify_peer, q);
    SSL_CTX_set_alpn_select_cb(q->sctx, alpn_select, NULL);

    if (!(q->lst = SSL_new_listener(q->sctx, 0)))
        return start_fail(ctx, t, "SSL_new_listener");
    if (!(bio = fwd_tls_bio_new(t->sock, 0)))
        return start_fail(ctx, t, "BIO");
    SSL_set_bio(q->lst, bio, bio);
    if (!SSL_set_blocking_mode(q->lst, 0) || !SSL_listen(q->lst))
        return start_fail(ctx, t, "SSL_listen");
    if (t->spec.mode == FWD_TUN_CONNECT)
        start_attempt(q, 0);
    ERR_clear_error();
    return FWD_OK;
}

int fwd_tunq_send(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                  size_t n)
{
    struct fwd_tunq *q = t->q;
    struct tq_conn *c = active(q);

    (void)ctx;
    if (c) {
        if (n <= 65535 && conn_send(c, p, n) == 0)
            t->tx++;
        ERR_clear_error();
        return 1;
    }
    return !connect_raw_ok(t, q);
}

int fwd_tunq_conn_send(struct tq_conn *c, const uint8_t *p, size_t n)
{
    int rc;

    if (!c->ready || c->dropped || n > 65535)
        return -1;
    rc = conn_send(c, p, n);
    ERR_clear_error();
    return rc == 0 ? 0 : -1;
}

void fwd_tunq_conn_drop(struct tq_conn *c)
{
    c->tp = NULL;
    c->dropped = 1;
}

int fwd_tunq_input(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                   size_t n, const struct sockaddr_storage *from,
                   socklen_t fromlen)
{
    struct fwd_tunq *q = t->q;
    int64_t now = now_ms();
    int is_long = quic_long(p, n), quic;
    BIO_ADDR *peer;

    (void)ctx;
    if (t->spec.mode == FWD_TUN_CONNECT) {
        from = &q->peer;
        fromlen = q->peerlen;
        if (is_long)
            q->heard = 1;
        /* short headers: only while a connection is up or coming up */
        quic = is_long || q->state == TQ_UP || q->in || q->pend ||
               (q->out && !q->probe);
        if (!quic)
            return t->spec.transport == FWD_TT_AUTO &&
                           q->state != TQ_REJECTED
                       ? 0
                       : 1;
    } else {
        if (is_long)
            src_add(q, from, fromlen, now);
        /* on TUN, what is no IP packet goes to QUIC too: short header
         * packets of a connection we lost (e.g. restarted) */
        quic = is_long || src_known(q, from, fromlen, now) ||
               (t->spec.type == FWD_TUN_TUN && !plain_ip(p, n));
        if (!quic)
            return t->spec.transport == FWD_TT_AUTO && !has_quic_peer(q) ? 0
                                                                         : 1;
    }

    if (!(peer = BIO_ADDR_new()) || !fwd_tls_sockaddr_to_bioaddr(peer, from)) {
        BIO_ADDR_free(peer);
        return 1;
    }
    (void)SSL_inject_net_dgram(q->lst, p, n, peer, NULL);
    BIO_ADDR_free(peer);
    if (is_long) {
        /* may complete a connection: process it now, while we know who
         * sent it */
        SSL_handle_events(q->lst);
        accept_conns(t, q, from, fromlen);
    }
    ERR_clear_error();
    return 1;
}

void fwd_tunq_service(fwd_ctx *ctx, struct fwd_tun *t)
{
    struct fwd_tunq *q = t->q;

    (void)ctx;
    ERR_clear_error();
    SSL_handle_events(q->lst);
    accept_conns(t, q, NULL, 0);
    if (t->spec.mode == FWD_TUN_LISTEN) {
        struct tq_conn **pp = &q->lconns, *c;
        while ((c = *pp)) {
            if (c->dropped || conn_step(t, c) < 0) {
                *pp = c->next;
                lconn_free(t, q, c);
            } else {
                pp = &c->next;
            }
        }
    } else {
        if (q->pend && conn_step(t, q->pend) < 0)
            conn_free(&q->pend);
        if (q->pend && q->pend->ready) {
            /* authenticated (if required) and talking: it takes over */
            src_forget(q, q->in);
            conn_free(&q->in);
            q->in = q->pend;
            q->pend = NULL;
        }
        if (q->in && conn_step(t, q->in) < 0) {
            src_forget(q, q->in);
            conn_free(&q->in);
        }
        connect_service(t, q);
    }
    /* our writes are only queued; send them now */
    SSL_handle_events(q->lst);
    ERR_clear_error();
}

int fwd_tunq_timeout(struct fwd_tun *t, int timeout_ms)
{
    struct fwd_tunq *q = t->q;
    struct timeval tv;
    int64_t now = now_ms(), due = -1;
    int inf = 1;

    if (SSL_get_event_timeout(q->lst, &tv, &inf) && !inf)
        due = now + (int64_t)tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000;
    if (t->spec.mode == FWD_TUN_CONNECT) {
        int64_t own = q->state == TQ_TRYING ? q->deadline
                      : q->state == TQ_UP   ? -1
                                            : q->retry_at;
        if (q->out && q->out->ready)
            own = q->out->last_tx + TQ_KEEPALIVE_MS;
        if (own >= 0 && (due < 0 || own < due))
            due = own;
    }
    if (due < 0)
        return timeout_ms;
    due = due <= now ? 0 : due - now;
    if (timeout_ms < 0 || due < timeout_ms)
        return (int)due;
    return timeout_ms;
}

int fwd_tunq_want_write(struct fwd_tun *t)
{
    return SSL_net_write_desired(t->q->lst);
}

void fwd_tunq_info(struct fwd_tun *t, fwd_tunnel_info *info)
{
    struct fwd_tunq *q = t->q;

    info->via = active(q)                ? FWD_VIA_QUIC
                : q->state == TQ_REJECTED ? FWD_VIA_REJECTED
                : connect_raw_ok(t, q)    ? FWD_VIA_UDP
                                          : FWD_VIA_NONE;
}

#else /* !(FWD_ENABLE_TUNTAP && FWD_ENABLE_QUIC) */

int fwd_tunnel_tls(fwd_ctx *ctx, const char *cert_file, const char *key_file,
                   const char *ca_file)
{
    (void)cert_file, (void)key_file, (void)ca_file;
    if (!ctx)
        return FWD_E_INVAL;
    return fwd_err(ctx, FWD_E_NOSYS, "QUIC tunnels not compiled in");
}

void fwd_tunq_ctx_free(fwd_ctx *ctx) { (void)ctx; }

#if FWD_ENABLE_TUNTAP
/* t->q stays NULL: the tunnel is plain UDP (spec_check refuses what needs
 * QUIC), so nothing else here is ever called. */
int fwd_tunq_start(fwd_ctx *ctx, struct fwd_tun *t)
{
    (void)ctx, (void)t;
    return FWD_OK;
}
void fwd_tunq_stop(struct fwd_tun *t) { (void)t; }
int fwd_tunq_send(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                  size_t n)
{
    (void)ctx, (void)t, (void)p, (void)n;
    return 0;
}
int fwd_tunq_input(fwd_ctx *ctx, struct fwd_tun *t, const uint8_t *p,
                   size_t n, const struct sockaddr_storage *from,
                   socklen_t fromlen)
{
    (void)ctx, (void)t, (void)p, (void)n, (void)from, (void)fromlen;
    return 0;
}
void fwd_tunq_service(fwd_ctx *ctx, struct fwd_tun *t) { (void)ctx, (void)t; }
int fwd_tunq_timeout(struct fwd_tun *t, int timeout_ms)
{
    (void)t;
    return timeout_ms;
}
int fwd_tunq_want_write(struct fwd_tun *t)
{
    (void)t;
    return 0;
}
void fwd_tunq_info(struct fwd_tun *t, fwd_tunnel_info *info)
{
    (void)t, (void)info;
}
int fwd_tunq_conn_send(struct tq_conn *c, const uint8_t *p, size_t n)
{
    (void)c, (void)p, (void)n;
    return -1;
}
void fwd_tunq_conn_drop(struct tq_conn *c) { (void)c; }
#endif

#endif
