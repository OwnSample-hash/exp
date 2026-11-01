/*
 * fwd_tls.c - OpenSSL helpers shared by the QUIC control channel and QUIC
 * tunnels: the datagram BIO, error text, fingerprints and self-signed
 * certificates.
 *
 * Datagrams still go through the syscall wrapper: OpenSSL is given a custom
 * BIO whose sendmmsg/recvmmsg call fwd_sys_sendto()/fwd_sys_recvfrom().
 * Reading certificate and key files is left to OpenSSL (stdio).
 */
#include "fwd_tls.h"

#if FWD_ENABLE_QUIC

#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>

/* ------------------------------------------------------------------ */
/* BIO on top of fwd_sys_sendto / fwd_sys_recvfrom                     */
/* ------------------------------------------------------------------ */
static BIO_METHOD *g_bio_meth[2]; /* [rx] */

static int bio_fd(BIO *b)
{
    return (int)(intptr_t)BIO_get_data(b);
}

int fwd_tls_bioaddr_to_sockaddr(const BIO_ADDR *a, struct sockaddr_storage *ss,
                                socklen_t *sl)
{
    fwd_addr fa;
    size_t n = sizeof fa.bytes;

    memset(&fa, 0, sizeof fa);
    switch (BIO_ADDR_family(a)) {
    case AF_INET:
        fa.family = FWD_V4;
        break;
    case AF_INET6:
        fa.family = FWD_V6;
        break;
    default:
        return -1;
    }
    if (!BIO_ADDR_rawaddress(a, fa.bytes, &n))
        return -1;
    return fwd_addr_to_sockaddr(&fa, ntohs(BIO_ADDR_rawport(a)), ss, sl) ==
                   FWD_OK
               ? 0
               : -1;
}

int fwd_tls_sockaddr_to_bioaddr(BIO_ADDR *a, const struct sockaddr_storage *ss)
{
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *s = (const struct sockaddr_in *)ss;
        return BIO_ADDR_rawmake(a, AF_INET, &s->sin_addr, 4, s->sin_port);
    }
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *s = (const struct sockaddr_in6 *)ss;
        return BIO_ADDR_rawmake(a, AF_INET6, &s->sin6_addr, 16, s->sin6_port);
    }
    return 0;
}

/* Report errno so that BIO_err_is_non_fatal() recognises EAGAIN. */
static int bio_fail(size_t *done, size_t i)
{
    *done = i;
    if (i > 0)
        return 1;
    ERR_raise(ERR_LIB_SYS, errno);
    return 0;
}

static int bio_sendmmsg(BIO *b, BIO_MSG *msg, size_t stride, size_t num,
                        uint64_t flags, size_t *done)
{
    size_t i;
    (void)flags;

    for (i = 0; i < num; i++) {
        BIO_MSG *m = (BIO_MSG *)((char *)msg + i * stride);
        struct sockaddr_storage ss;
        socklen_t sl = 0;
        ssize_t n;

        if (m->peer && BIO_ADDR_family(m->peer) != AF_UNSPEC) {
            if (fwd_tls_bioaddr_to_sockaddr(m->peer, &ss, &sl) < 0) {
                errno = EAFNOSUPPORT;
                return bio_fail(done, i);
            }
            n = fwd_sys_sendto(bio_fd(b), m->data, m->data_len, 0,
                               (struct sockaddr *)&ss, sl);
        } else {
            n = fwd_sys_send(bio_fd(b), m->data, m->data_len, 0);
        }
        if (n < 0)
            return bio_fail(done, i);
    }
    *done = i;
    return 1;
}

static int bio_recvmmsg(BIO *b, BIO_MSG *msg, size_t stride, size_t num,
                        uint64_t flags, size_t *done)
{
    size_t i;
    (void)flags;

    for (i = 0; i < num; i++) {
        BIO_MSG *m = (BIO_MSG *)((char *)msg + i * stride);
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        ssize_t n;

        memset(&ss, 0, sizeof ss);
        n = fwd_sys_recvfrom(bio_fd(b), m->data, m->data_len, 0,
                             (struct sockaddr *)&ss, &sl);
        if (n < 0)
            return bio_fail(done, i);
        m->data_len = (size_t)n;
        m->flags = 0;
        if (m->peer && !fwd_tls_sockaddr_to_bioaddr(m->peer, &ss))
            BIO_ADDR_clear(m->peer);
        if (m->local)
            BIO_ADDR_clear(m->local);
    }
    *done = i;
    return 1;
}

/* Transmit-only BIO: received datagrams arrive by SSL_inject_net_dgram(). */
static int bio_norecv(BIO *b, BIO_MSG *msg, size_t stride, size_t num,
                      uint64_t flags, size_t *done)
{
    (void)b, (void)msg, (void)stride, (void)num, (void)flags;
    errno = EAGAIN;
    return bio_fail(done, 0);
}

static long bio_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    (void)num;
    switch (cmd) {
    case BIO_CTRL_DGRAM_GET_EFFECTIVE_CAPS:
        /* recvfrom tells us the sender, sendto takes the destination */
        return BIO_DGRAM_CAP_PROVIDES_SRC_ADDR | BIO_DGRAM_CAP_HANDLES_DST_ADDR;
    case BIO_CTRL_DGRAM_SET_CAPS:
    case BIO_CTRL_FLUSH:
        return 1;
    case BIO_CTRL_GET_RPOLL_DESCRIPTOR:
    case BIO_CTRL_GET_WPOLL_DESCRIPTOR: {
        BIO_POLL_DESCRIPTOR *d = ptr;
        d->type = BIO_POLL_DESCRIPTOR_TYPE_SOCK_FD;
        d->value.fd = bio_fd(b);
        return 1;
    }
    }
    return 0;
}

static int bio_create(BIO *b)
{
    BIO_set_init(b, 1);
    return 1;
}

BIO *fwd_tls_bio_new(int fd, int rx)
{
    BIO *b;

    rx = !!rx;
    if (!g_bio_meth[rx]) {
        BIO_METHOD *m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK,
                                     rx ? "fwd-sys-dgram" : "fwd-sys-dgram-tx");
        if (!m || !BIO_meth_set_sendmmsg(m, bio_sendmmsg) ||
            !BIO_meth_set_recvmmsg(m, rx ? bio_recvmmsg : bio_norecv) ||
            !BIO_meth_set_ctrl(m, bio_ctrl) ||
            !BIO_meth_set_create(m, bio_create)) {
            BIO_meth_free(m);
            return NULL;
        }
        g_bio_meth[rx] = m;
    }
    b = BIO_new(g_bio_meth[rx]);
    if (b)
        BIO_set_data(b, (void *)(intptr_t)fd);
    return b;
}

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
void fwd_tls_seterr(char *buf, size_t len, const char *fmt, ...)
{
    va_list ap;

    if (!buf || !len)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, len, fmt, ap);
    va_end(ap);
}

void fwd_tls_errmsg(char *buf, size_t len, const char *what)
{
    unsigned long e = ERR_peek_last_error();
    char tmp[200];

    if (!buf || !len)
        return;
    if (e) {
        ERR_error_string_n(e, tmp, sizeof tmp);
        snprintf(buf, len, "%s: %s", what, tmp);
    } else {
        snprintf(buf, len, "%s", what);
    }
}

void fwd_tls_format_fingerprint(const unsigned char *md, unsigned n,
                                char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned i;

    for (i = 0; i < n; i++) {
        out[i * 3] = hex[md[i] >> 4];
        out[i * 3 + 1] = hex[md[i] & 15];
        out[i * 3 + 2] = i + 1 < n ? ':' : '\0';
    }
}

int fwd_tls_cert_sha256(X509 *x, unsigned char md[32])
{
    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned n = 0;

    if (!x || !X509_digest(x, EVP_sha256(), buf, &n) || n != 32)
        return -1;
    memcpy(md, buf, 32);
    return 0;
}

/* ------------------------------------------------------------------ */
/* certificates                                                       */
/* ------------------------------------------------------------------ */
int fwd_tls_fingerprint(const char *cert_file, char *out, size_t outlen)
{
    unsigned char md[32];
    X509 *x = NULL;
    BIO *in;
    int rc = FWD_E_TLS;

    if (!cert_file || !out)
        return FWD_E_INVAL;
    if (outlen < FWD_FINGERPRINT_LEN)
        return FWD_E_BUF;
    if ((in = BIO_new_file(cert_file, "r")) &&
        (x = PEM_read_bio_X509(in, NULL, NULL, NULL)) &&
        fwd_tls_cert_sha256(x, md) == 0) {
        fwd_tls_format_fingerprint(md, 32, out);
        rc = FWD_OK;
    }
    X509_free(x);
    BIO_free(in);
    ERR_clear_error();
    return rc;
}

static int add_ext(X509 *x, int nid, const char *value)
{
    X509V3_CTX v3;
    X509_EXTENSION *ext;
    int ok;

    X509V3_set_ctx_nodb(&v3);
    X509V3_set_ctx(&v3, x, x, NULL, NULL, 0);
    ext = X509V3_EXT_conf_nid(NULL, &v3, nid, value);
    if (!ext)
        return 0;
    ok = X509_add_ext(x, ext, -1);
    X509_EXTENSION_free(ext);
    return ok;
}

/* PEM writer through a file created with the given mode. */
static int write_pem(const char *path, int mode, int (*fn)(BIO *, void *),
                     void *obj)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    BIO *b;
    int ok;

    if (fd < 0)
        return 0;
    if (fchmod(fd, (mode_t)mode) < 0 || !(b = BIO_new_fd(fd, BIO_CLOSE))) {
        close(fd);
        return 0;
    }
    ok = fn(b, obj) && BIO_flush(b) > 0;
    BIO_free(b);
    return ok;
}

static int pem_key(BIO *b, void *k)
{
    return PEM_write_bio_PrivateKey(b, k, NULL, NULL, 0, NULL, NULL);
}

static int pem_cert(BIO *b, void *x)
{
    return PEM_write_bio_X509(b, x);
}

int fwd_tls_make_cert(const fwd_addr *ip, const char *name, unsigned days,
                      EVP_PKEY **keyp, X509 **certp)
{
    char san[200], ipstr[64], cn[64];
    unsigned char serial[16];
    EVP_PKEY *key = NULL;
    X509 *x = NULL;
    X509_NAME *subj;
    BIGNUM *bn = NULL;
    int ok = 0;

    if (days == 0 || (!ip && !name))
        return 0;
    san[0] = '\0';
    if (ip) {
        if (fwd_addr_to_string(ip, ipstr, sizeof ipstr) < 0)
            return 0;
        snprintf(san, sizeof san, "IP:%s", ipstr);
    }
    if (name) {
        size_t l = strlen(san);
        if (strlen(name) >= sizeof cn || strpbrk(name, ",:"))
            return 0;
        snprintf(san + l, sizeof san - l, "%sDNS:%s", l ? "," : "", name);
    }
    snprintf(cn, sizeof cn, "%s", name ? name : ipstr);

    if (!(key = EVP_EC_gen("P-256")) || !(x = X509_new()) ||
        !RAND_bytes(serial, sizeof serial))
        goto out;
    serial[0] &= 0x7f; /* positive */
    if (!(bn = BN_bin2bn(serial, sizeof serial, NULL)) ||
        !BN_to_ASN1_INTEGER(bn, X509_get_serialNumber(x)) ||
        !X509_set_version(x, X509_VERSION_3) ||
        !X509_gmtime_adj(X509_getm_notBefore(x), -3600) ||
        !X509_time_adj_ex(X509_getm_notAfter(x), (int)days, 0, NULL) ||
        !X509_set_pubkey(x, key))
        goto out;
    subj = X509_get_subject_name(x);
    if (!X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
                                    (const unsigned char *)cn, -1, -1, 0) ||
        !X509_set_issuer_name(x, subj) ||
        !add_ext(x, NID_subject_alt_name, san) ||
        !add_ext(x, NID_basic_constraints, "critical,CA:FALSE") ||
        !add_ext(x, NID_key_usage, "critical,digitalSignature") ||
        /* clientAuth too: tunnel peers authenticate each other */
        !add_ext(x, NID_ext_key_usage, "serverAuth,clientAuth") ||
        !add_ext(x, NID_subject_key_identifier, "hash") ||
        !X509_sign(x, key, EVP_sha256()))
        goto out;
    *keyp = key;
    *certp = x;
    key = NULL;
    x = NULL;
    ok = 1;
out:
    BN_free(bn);
    X509_free(x);
    EVP_PKEY_free(key);
    return ok;
}

int fwd_tls_make_selfsigned(const char *cert_file, const char *key_file,
                            const fwd_addr *ip, const char *name,
                            unsigned days)
{
    EVP_PKEY *key = NULL;
    X509 *x = NULL;
    int rc = FWD_E_TLS;

    if (!cert_file || !key_file || days == 0 || (!ip && !name))
        return FWD_E_INVAL;
    ERR_clear_error();
    if (!fwd_tls_make_cert(ip, name, days, &key, &x))
        goto out;
    if (!write_pem(key_file, 0600, pem_key, key) ||
        !write_pem(cert_file, 0644, pem_cert, x)) {
        rc = FWD_E_SYS;
        goto out;
    }
    rc = FWD_OK;
out:
    X509_free(x);
    EVP_PKEY_free(key);
    ERR_clear_error();
    return rc;
}

#else /* !FWD_ENABLE_QUIC */

int fwd_tls_fingerprint(const char *cert_file, char *out, size_t outlen)
{
    (void)cert_file, (void)out, (void)outlen;
    return FWD_E_NOSYS;
}
int fwd_tls_make_selfsigned(const char *cert_file, const char *key_file,
                            const fwd_addr *ip, const char *name,
                            unsigned days)
{
    (void)cert_file, (void)key_file, (void)ip, (void)name, (void)days;
    return FWD_E_NOSYS;
}

#endif
