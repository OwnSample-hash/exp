/*
 * fwd_quic.c - QUIC control channel (server + blocking client) on OpenSSL.
 *
 * Datagrams go through the fwd_tls BIO (fwd_sys_sendto/recvfrom) on a
 * socket made with fwd_sys_socket().
 *
 * Server: one listener SSL on a non-blocking UDP socket. Every command is a
 * client initiated bidirectional stream: read until FIN, run it through
 * fwd_ctl_handle(), write the response and FIN.
 */
#include "fwd_tls.h"

#if FWD_ENABLE_CTL && FWD_ENABLE_QUIC

#define QUIC_MAX_CONNS 64
#define QUIC_MAX_STREAMS 8   /* open command streams per connection     */
#define QUIC_IDLE_MS 30000

static const unsigned char alpn_wire[] = "\x08" FWD_QUIC_ALPN;

struct qstream {
    struct qstream *next;
    SSL *s;
    int responded;            /* response is in out[], being written      */
    size_t inlen, outlen, outoff;
    char in[FWD_MAX_CMD + 2];
    char out[FWD_MAX_RESP + 1];
};

struct qconn {
    struct qconn *next;
    SSL *c;
    struct qstream *streams;
    unsigned nstreams;
};

struct fwd_quic {
    SSL_CTX *sctx;
    SSL *lst;
    int fd;
    struct qconn *conns;
    unsigned nconns;
};

#define seterr fwd_tls_seterr
#define ssl_errmsg fwd_tls_errmsg

static int udp_socket(const fwd_addr *a, uint16_t port, int do_bind,
                      struct sockaddr_storage *ss, socklen_t *sl)
{
    int fd, one = 1;

    if (!fwd_addr_usable(a) || fwd_addr_to_sockaddr(a, port, ss, sl) != FWD_OK) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    fd = fwd_sys_socket(ss->ss_family, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
#if FWD_ENABLE_IPV6 && defined(IPV6_V6ONLY)
    if (do_bind && ss->ss_family == AF_INET6)
        fwd_sys_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);
#endif
    (void)one;
    if (fwd_sys_set_nonblock(fd) < 0 ||
        (do_bind && fwd_sys_bind(fd, (struct sockaddr *)ss, *sl) < 0)) {
        int e = errno;
        fwd_sys_close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

static int alpn_select(SSL *ssl, const unsigned char **out,
                       unsigned char *outlen, const unsigned char *in,
                       unsigned int inlen, void *arg)
{
    (void)ssl, (void)arg;
    if (SSL_select_next_proto((unsigned char **)out, outlen, alpn_wire,
                              sizeof alpn_wire - 1, in,
                              inlen) != OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    return SSL_TLSEXT_ERR_OK;
}

/* ------------------------------------------------------------------ */
/* server                                                             */
/* ------------------------------------------------------------------ */
static void stream_free(struct qconn *qc, struct qstream *st)
{
    struct qstream **pp;

    for (pp = &qc->streams; *pp; pp = &(*pp)->next)
        if (*pp == st) {
            *pp = st->next;
            break;
        }
    SSL_free(st->s);
    FWD_FREE(st);
    qc->nstreams--;
}

static void conn_free(struct fwd_quic *q, struct qconn *qc)
{
    struct qconn **pp;

    while (qc->streams)
        stream_free(qc, qc->streams);
    for (pp = &q->conns; *pp; pp = &(*pp)->next)
        if (*pp == qc) {
            *pp = qc->next;
            break;
        }
    SSL_free(qc->c);
    FWD_FREE(qc);
    q->nconns--;
}

void fwd_ctl_close_quic(fwd_ctx *ctx)
{
    struct fwd_quic *q = ctx ? ctx->quic : NULL;

    if (!q)
        return;
    while (q->conns)
        conn_free(q, q->conns);
    SSL_free(q->lst);
    SSL_CTX_free(q->sctx);
    if (q->fd >= 0)
        fwd_sys_close(q->fd);
    FWD_FREE(q);
    ctx->quic = NULL;
}

static int listen_fail(fwd_ctx *ctx, struct fwd_quic *q, const char *what)
{
    char msg[sizeof ctx->last_msg];

    ssl_errmsg(msg, sizeof msg, what);
    ERR_clear_error();
    ctx->quic = q;
    fwd_ctl_close_quic(ctx);
    return fwd_err(ctx, FWD_E_TLS, "%s", msg);
}

int fwd_ctl_listen_quic(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port,
                        const char *cert_file, const char *key_file)
{
    struct sockaddr_storage ss;
    socklen_t sl;
    struct fwd_quic *q;
    BIO *bio;

    if (!ctx || !addr || !cert_file || !key_file)
        return FWD_E_INVAL;
    if (ctx->quic)
        return fwd_err(ctx, FWD_E_EXIST, "QUIC control channel already open");
    if (!fwd_addr_usable(addr))
        return fwd_err(ctx, FWD_E_INVAL, "invalid control address");
    q = FWD_CALLOC(1, sizeof *q);
    if (!q)
        return fwd_err(ctx, FWD_E_NOMEM, "out of memory");
    q->fd = udp_socket(addr, port, 1, &ss, &sl);
    if (q->fd < 0) {
        int rc = fwd_err_sys(ctx, "QUIC socket");
        FWD_FREE(q);
        return rc;
    }

    ERR_clear_error();
    q->sctx = SSL_CTX_new(OSSL_QUIC_server_method());
    if (!q->sctx)
        return listen_fail(ctx, q, "SSL_CTX_new");
    if (SSL_CTX_use_certificate_chain_file(q->sctx, cert_file) <= 0)
        return listen_fail(ctx, q, "certificate");
    if (SSL_CTX_use_PrivateKey_file(q->sctx, key_file, SSL_FILETYPE_PEM) <= 0)
        return listen_fail(ctx, q, "private key");
    if (!SSL_CTX_check_private_key(q->sctx))
        return listen_fail(ctx, q, "key does not match certificate");
    SSL_CTX_set_alpn_select_cb(q->sctx, alpn_select, NULL);

    q->lst = SSL_new_listener(q->sctx, 0);
    if (!q->lst)
        return listen_fail(ctx, q, "SSL_new_listener");
    bio = fwd_tls_bio_new(q->fd, 1);
    if (!bio)
        return listen_fail(ctx, q, "BIO");
    SSL_set_bio(q->lst, bio, bio);
    if (!SSL_set_blocking_mode(q->lst, 0) || !SSL_listen(q->lst))
        return listen_fail(ctx, q, "SSL_listen");
    ctx->quic = q;
    return FWD_OK;
}

void fwd_quic_collect(fwd_ctx *ctx)
{
    struct fwd_quic *q = ctx->quic;
    short ev = POLLIN;

    if (!q)
        return;
    if (SSL_net_write_desired(q->lst))
        ev |= POLLOUT;
    fwd_pollset_add(ctx, q->fd, ev, FWD_PK_CTL_QUIC, NULL);
}

int fwd_quic_timeout(fwd_ctx *ctx, int timeout_ms)
{
    struct timeval tv;
    int inf = 1;
    long ms;

    if (!ctx->quic || !SSL_get_event_timeout(ctx->quic->lst, &tv, &inf) || inf)
        return timeout_ms;
    ms = (long)tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000;
    if (timeout_ms < 0 || ms < timeout_ms)
        return (int)ms;
    return timeout_ms;
}

/* Returns 1 when the stream is finished with (answered or failed). */
static int stream_step(fwd_ctx *ctx, struct qstream *st)
{
    size_t n;

    if (!st->responded) {
        for (;;) {
            if (st->inlen == sizeof st->in - 1) {
                st->outlen = (size_t)snprintf(st->out, sizeof st->out,
                                              "-1:%d:0:command too long",
                                              -FWD_E_PARSE);
                st->responded = 1;
                break;
            }
            if (SSL_read_ex(st->s, st->in + st->inlen,
                            sizeof st->in - 1 - st->inlen, &n)) {
                st->inlen += n;
                continue;
            }
            switch (SSL_get_error(st->s, 0)) {
            case SSL_ERROR_WANT_READ:
                return 0;
            case SSL_ERROR_ZERO_RETURN: { /* FIN: the command is complete */
                int rn;
                st->in[st->inlen] = '\0';
                rn = fwd_ctl_handle(ctx, st->in, st->inlen, st->out,
                                    sizeof st->out);
                if (rn < 0)
                    return 1;
                st->outlen = (size_t)rn;
                st->responded = 1;
                break;
            }
            default:
                return 1; /* reset by peer, connection gone */
            }
            break;
        }
    }
    while (st->outoff < st->outlen) { /* FIN goes with the last byte */
        if (!SSL_write_ex2(st->s, st->out + st->outoff,
                           st->outlen - st->outoff, SSL_WRITE_FLAG_CONCLUDE,
                           &n))
            return SSL_get_error(st->s, 0) == SSL_ERROR_WANT_WRITE ? 0 : 1;
        st->outoff += n;
    }
    return 1;
}

static void conn_service(fwd_ctx *ctx, struct fwd_quic *q, struct qconn *qc)
{
    SSL_CONN_CLOSE_INFO ci;
    struct qstream *st, *nx;
    SSL *s;

    if (SSL_get_conn_close_info(qc->c, &ci, sizeof ci)) {
        conn_free(q, qc);
        return;
    }
    while ((s = SSL_accept_stream(qc->c, SSL_ACCEPT_STREAM_NO_BLOCK))) {
        if (qc->nstreams >= QUIC_MAX_STREAMS ||
            SSL_get_stream_type(s) != SSL_STREAM_TYPE_BIDI ||
            !(st = FWD_CALLOC(1, sizeof *st))) {
            (void)SSL_stream_reset(s, NULL, 0);
            SSL_free(s);
            continue;
        }
        st->s = s;
        st->next = qc->streams;
        qc->streams = st;
        qc->nstreams++;
    }
    for (st = qc->streams; st; st = nx) {
        nx = st->next;
        if (stream_step(ctx, st))
            stream_free(qc, st);
    }
}

void fwd_quic_service(fwd_ctx *ctx)
{
    struct fwd_quic *q = ctx->quic;
    struct qconn *qc, *nx;
    SSL *c;

    if (!q)
        return;
    ERR_clear_error();
    SSL_handle_events(q->lst);
    while ((c = SSL_accept_connection(q->lst,
                                      SSL_ACCEPT_CONNECTION_NO_BLOCK))) {
        if (q->nconns >= QUIC_MAX_CONNS || !(qc = FWD_CALLOC(1, sizeof *qc))) {
            (void)SSL_shutdown_ex(c, SSL_SHUTDOWN_FLAG_RAPID, NULL, 0);
            SSL_free(c);
            continue;
        }
        (void)SSL_set_default_stream_mode(c, SSL_DEFAULT_STREAM_MODE_NONE);
        (void)SSL_set_incoming_stream_policy(
            c, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0);
        qc->c = c;
        qc->next = q->conns;
        q->conns = qc;
        q->nconns++;
    }
    for (qc = q->conns; qc; qc = nx) {
        nx = qc->next;
        conn_service(ctx, q, qc);
    }
    ERR_clear_error();
}

/* ------------------------------------------------------------------ */
/* client                                                             */
/* ------------------------------------------------------------------ */
struct verify_state {
    int pin_only;
};

static int verify_ex_idx = -1;

/* Pin-only mode: chain and name errors are ignored here, the pin is checked
 * after the handshake, before anything is sent. */
static int verify_cb(int ok, X509_STORE_CTX *xs)
{
    SSL *ssl = X509_STORE_CTX_get_ex_data(
        xs, SSL_get_ex_data_X509_STORE_CTX_idx());
    struct verify_state *vs = ssl ? SSL_get_ex_data(ssl, verify_ex_idx) : NULL;

    if (vs && vs->pin_only)
        return 1;
    return ok;
}

/* Wait until the connection wants to be serviced again or the deadline
 * passes. Returns 0, or FWD_E_TIMEOUT / FWD_E_SYS. */
static int client_wait(SSL *ssl, int fd, int64_t deadline)
{
    struct pollfd p;
    struct timeval tv;
    int64_t now = fwd_sys_now_ms();
    int inf = 1, wait;

    if (now >= deadline)
        return FWD_E_TIMEOUT;
    wait = (int)(deadline - now);
    if (SSL_get_event_timeout(ssl, &tv, &inf) && !inf) {
        long ms = (long)tv.tv_sec * 1000 + (tv.tv_usec + 999) / 1000;
        if (ms < wait)
            wait = (int)ms;
    }
    p.fd = fd;
    p.events = POLLIN;
    if (SSL_net_write_desired(ssl))
        p.events |= POLLOUT;
    p.revents = 0;
    if (fwd_sys_poll(&p, 1, wait) < 0 && errno != EINTR)
        return errno == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
    SSL_handle_events(ssl);
    return 0;
}

/* After a failed SSL_* call: keep going (0), or a final error. */
static int client_retry(SSL *ssl, int ret, int fd, int64_t deadline,
                        char *err, size_t errlen, const char *what)
{
    switch (SSL_get_error(ssl, ret)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        return client_wait(ssl, fd, deadline);
    case SSL_ERROR_SYSCALL:
        ssl_errmsg(err, errlen, what);
        return FWD_E_SYS;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        seterr(err, errlen, "%s: certificate rejected: %s", what,
               X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
    } else {
        ssl_errmsg(err, errlen, what);
    }
    return FWD_E_TLS;
}

static int client_setup(SSL_CTX *cctx, SSL *ssl, const fwd_addr *server,
                        const fwd_tls_trust *t, struct verify_state *vs,
                        char *err, size_t errlen)
{
    char ip[64];

    vs->pin_only = t->pin_sha256 && !t->ca_file;
    if (t->ca_file) {
        if (!SSL_CTX_load_verify_locations(cctx, t->ca_file, NULL)) {
            ssl_errmsg(err, errlen, "cannot load trusted certificates");
            return FWD_E_TLS;
        }
        /* a self-signed or other non-root certificate in the file is an
         * anchor by itself */
        X509_VERIFY_PARAM_set_flags(SSL_get0_param(ssl),
                                    X509_V_FLAG_PARTIAL_CHAIN);
    } else if (!vs->pin_only && !SSL_CTX_set_default_verify_paths(cctx)) {
        ssl_errmsg(err, errlen, "cannot load system trust store");
        return FWD_E_TLS;
    }
    SSL_set_verify(ssl, SSL_VERIFY_PEER, verify_cb);
    if (verify_ex_idx < 0)
        verify_ex_idx = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
    if (verify_ex_idx < 0 || !SSL_set_ex_data(ssl, verify_ex_idx, vs))
        return FWD_E_NOMEM;

    if (t->server_name) {
        if (!SSL_set_tlsext_host_name(ssl, t->server_name) ||
            (!vs->pin_only && !SSL_set1_host(ssl, t->server_name))) {
            seterr(err, errlen, "bad server name");
            return FWD_E_INVAL;
        }
    } else if (!vs->pin_only) {
        if (fwd_addr_to_string(server, ip, sizeof ip) < 0 ||
            !X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), ip))
            return FWD_E_INVAL;
    }
    if (SSL_set_alpn_protos(ssl, alpn_wire, sizeof alpn_wire - 1) != 0)
        return FWD_E_NOMEM;
    return FWD_OK;
}

static int check_pin(SSL *ssl, const unsigned char pin[32], char *err,
                     size_t errlen)
{
    X509 *peer = SSL_get0_peer_certificate(ssl);
    unsigned char md[32];
    char fp[FWD_FINGERPRINT_LEN];

    if (!peer || fwd_tls_cert_sha256(peer, md) != 0) {
        seterr(err, errlen, "no server certificate");
        return FWD_E_TLS;
    }
    if (CRYPTO_memcmp(md, pin, 32) != 0) {
        fwd_tls_format_fingerprint(md, 32, fp);
        seterr(err, errlen,
               "server certificate does not match the pinned fingerprint "
               "(server presented %s)",
               fp);
        return FWD_E_TLS;
    }
    return FWD_OK;
}

int fwd_client_request_quic(const fwd_addr *server, uint16_t port,
                            const fwd_tls_trust *trust, const char *cmd,
                            char *resp, size_t resplen, int timeout_ms,
                            char *err, size_t errlen)
{
    static const fwd_tls_trust no_trust;
    struct verify_state vs = {0};
    struct sockaddr_storage ss;
    socklen_t sl;
    unsigned char pin[32];
    int64_t deadline;
    SSL_CTX *cctx = NULL;
    SSL *ssl = NULL;
    BIO *bio;
    BIO_ADDR *peer = NULL;
    size_t got = 0, n, cmdlen;
    int fd = -1, rc, ret;

    if (err && errlen)
        err[0] = '\0';
    if (!server || !cmd || !resp || resplen < 2)
        return FWD_E_INVAL;
    if (!trust)
        trust = &no_trust;
    if (trust->pin_sha256 && fwd_parse_pin(trust->pin_sha256, pin) < 0) {
        seterr(err, errlen, "pin must be 32 hex bytes (SHA-256)");
        return FWD_E_INVAL;
    }
    cmdlen = strlen(cmd);
    if (fwd_sys_now_ms() < 0)
        return FWD_E_NOSYS;
    deadline = fwd_sys_now_ms() + (timeout_ms < 0 ? 0 : timeout_ms);

    fd = udp_socket(server, port, 0, &ss, &sl);
    if (fd < 0) {
        int e = errno;
        seterr(err, errlen, "socket: %s", strerror(e));
        return e == ENOSYS ? FWD_E_NOSYS : FWD_E_SYS;
    }
    ERR_clear_error();
    rc = FWD_E_NOMEM;
    if (!(cctx = SSL_CTX_new(OSSL_QUIC_client_method())) ||
        !(ssl = SSL_new(cctx)) || !(peer = BIO_ADDR_new()) ||
        !(bio = fwd_tls_bio_new(fd, 1)))
        goto out;
    SSL_set_bio(ssl, bio, bio);
    if (!fwd_tls_sockaddr_to_bioaddr(peer, &ss) || !SSL_set1_initial_peer_addr(ssl, peer))
        goto out;
    if ((rc = client_setup(cctx, ssl, server, trust, &vs, err, errlen)) !=
        FWD_OK)
        goto out;
    if (!SSL_set_blocking_mode(ssl, 0) ||
        !SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_AUTO_BIDI)) {
        rc = FWD_E_TLS;
        goto out;
    }

    while ((ret = SSL_connect(ssl)) != 1)
        if ((rc = client_retry(ssl, ret, fd, deadline, err, errlen,
                               "handshake")) != 0)
            goto out;
    if (trust->pin_sha256 &&
        (rc = check_pin(ssl, pin, err, errlen)) != FWD_OK)
        goto out;

    while (!SSL_write_ex2(ssl, cmd, cmdlen, SSL_WRITE_FLAG_CONCLUDE, &n))
        if ((rc = client_retry(ssl, 0, fd, deadline, err, errlen, "send")) !=
            0)
            goto out;
    for (;;) {
        if (got == resplen - 1) {
            rc = FWD_E_BUF;
            goto out;
        }
        if (SSL_read_ex(ssl, resp + got, resplen - 1 - got, &n)) {
            got += n;
            continue;
        }
        if (SSL_get_error(ssl, 0) == SSL_ERROR_ZERO_RETURN)
            break;
        if ((rc = client_retry(ssl, 0, fd, deadline, err, errlen,
                               "receive")) != 0)
            goto out;
    }
    resp[got] = '\0';
    rc = (int)got;

    /* polite close, but don't wait long for it */
    deadline = fwd_sys_now_ms() + 100;
    while (SSL_shutdown_ex(ssl, SSL_SHUTDOWN_FLAG_RAPID, NULL, 0) == 0 &&
           client_wait(ssl, fd, deadline) == 0)
        ;

out:
    if (rc == FWD_E_TIMEOUT && err && errlen && !err[0])
        seterr(err, errlen, "no answer from QUIC server");
    BIO_ADDR_free(peer);
    SSL_free(ssl);
    SSL_CTX_free(cctx);
    fwd_sys_close(fd);
    ERR_clear_error();
    return rc;
}

#else /* !(FWD_ENABLE_CTL && FWD_ENABLE_QUIC) */

int fwd_ctl_listen_quic(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port,
                        const char *cert_file, const char *key_file)
{
    (void)addr, (void)port, (void)cert_file, (void)key_file;
    if (!ctx)
        return FWD_E_INVAL;
    return fwd_err(ctx, FWD_E_NOSYS, "QUIC support not compiled in");
}
void fwd_ctl_close_quic(fwd_ctx *ctx) { (void)ctx; }
void fwd_quic_collect(fwd_ctx *ctx) { (void)ctx; }
int fwd_quic_timeout(fwd_ctx *ctx, int timeout_ms)
{
    (void)ctx;
    return timeout_ms;
}
void fwd_quic_service(fwd_ctx *ctx) { (void)ctx; }
int fwd_client_request_quic(const fwd_addr *server, uint16_t port,
                            const fwd_tls_trust *trust, const char *cmd,
                            char *resp, size_t resplen, int timeout_ms,
                            char *err, size_t errlen)
{
    (void)server, (void)port, (void)trust, (void)cmd, (void)resp,
        (void)resplen, (void)timeout_ms;
    if (err && errlen)
        snprintf(err, errlen, "QUIC support not compiled in");
    return FWD_E_NOSYS;
}
#endif
