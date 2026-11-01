/* fwd_tls.h - OpenSSL helpers shared by the QUIC control channel
 * (fwd_quic.c) and QUIC tunnels (fwd_tunq.c). Only for FWD_ENABLE_QUIC. */
#ifndef FWD_TLS_H
#define FWD_TLS_H

#include "fwd_internal.h"

#if FWD_ENABLE_QUIC

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#if OPENSSL_VERSION_NUMBER < 0x30500000L
#error "FWD_ENABLE_QUIC needs OpenSSL 3.5 or newer (QUIC server API)"
#endif

/* Datagram BIO on top of fwd_sys_sendto()/fwd_sys_recvfrom(). It does not
 * own the socket. rx = 0: the BIO never reads, the owner reads the socket
 * itself and hands QUIC datagrams over with SSL_inject_net_dgram(). */
BIO *fwd_tls_bio_new(int fd, int rx);

int fwd_tls_bioaddr_to_sockaddr(const BIO_ADDR *a, struct sockaddr_storage *ss,
                                socklen_t *sl);
int fwd_tls_sockaddr_to_bioaddr(BIO_ADDR *a, const struct sockaddr_storage *ss);

void fwd_tls_seterr(char *buf, size_t len, const char *fmt, ...)
    FWD_PRINTF(3, 4);
/* "what: <last OpenSSL error>" */
void fwd_tls_errmsg(char *buf, size_t len, const char *what);

/* SHA-256 of the DER certificate; 0 on success. */
int fwd_tls_cert_sha256(X509 *x, unsigned char md[32]);
/* "AB:CD:..." (FWD_FINGERPRINT_LEN bytes) */
void fwd_tls_format_fingerprint(const unsigned char *md, unsigned n,
                                char *out);

/* New self-signed EC P-256 certificate, usable for server and client
 * authentication. ip and/or name become SANs. 1 on success. */
int fwd_tls_make_cert(const fwd_addr *ip, const char *name, unsigned days,
                      EVP_PKEY **key, X509 **cert);

#endif /* FWD_ENABLE_QUIC */
#endif /* FWD_TLS_H */
