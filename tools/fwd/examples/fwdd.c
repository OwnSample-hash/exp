/*
 * fwdd - minimal daemon: runs libfwd with a QUIC and/or UDP control channel.
 *
 *   fwdd [-a address] [-p port] [-t token] [-S] [-T tunnel-ca.pem]
 *        [-c cert.pem -k key.pem [-q port] [-g] [-N name] [-U]]
 *
 * Defaults: 127.0.0.1, plain UDP on 7777, no token. -S turns off automatic
 * sysctl changes.
 *
 * QUIC (needs a build with FWD_ENABLE_QUIC): -c/-k give the certificate and
 * key and turn it on, on port -q (default 7778). -g creates a self-signed
 * certificate for the listen address (and -N name) if the files do not
 * exist yet. The SHA-256 fingerprint is printed at start; clients can pin it
 * (fwdctl -F) or trust the certificate file itself (fwdctl -C cert.pem).
 * -U turns the plain UDP channel off so that only QUIC is served.
 *
 * Tunnels use QUIC by default (plain UDP as fallback). Their TLS identity
 * is the -c/-k certificate too, so a tunnel peer pins the fingerprint
 * printed at start (tun-add ... <pin>). Without -c/-k a throwaway
 * certificate is made at start: tunnels are encrypted, but cannot be
 * pinned. -T: tunnel peers without a pin must have a certificate issued by
 * (or contained in) this PEM file.
 */
#include "fwd.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static fwd_ctx *g_ctx;

static void on_signal(int sig) {
  (void)sig;
  if (g_ctx)
    fwd_stop(g_ctx);
}

int main(int argc, char **argv) {
  const char *addr_s = "127.0.0.1", *cert = NULL, *key = NULL, *name = NULL;
  const char *tun_ca = NULL;
  unsigned port = 7777, qport = 7778;
  int gen = 0, udp = 1;
  char fp[FWD_FINGERPRINT_LEN];
  struct stat st;
  fwd_config cfg;
  fwd_addr addr;
  int opt, rc;

  fwd_config_default(&cfg);
  while ((opt = getopt(argc, argv, "a:p:t:ST:c:k:q:gN:Uh:v")) != -1) {
    switch (opt) {
    case 'a':
      addr_s = optarg;
      break;
    case 'p':
      port = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 't':
      snprintf(cfg.ctl_token, sizeof cfg.ctl_token, "%s", optarg);
      break;
    case 'S':
      cfg.auto_sysctl = 0;
      break;
    case 'T':
      tun_ca = optarg;
      break;
    case 'c':
      cert = optarg;
      break;
    case 'k':
      key = optarg;
      break;
    case 'q':
      qport = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 'g':
      gen = 1;
      break;
    case 'N':
      name = optarg;
      break;
    case 'U':
      udp = 0;
      break;
    case 'v':
      fprintf(stderr, "%s libfwd version %d.%d.%d\n", argv[0], FWD_MAJOR,
              FWD_MINOR, FWD_PATCH);
      return 0;
    default:
      fprintf(stderr,
              "usage: %s [-a addr] [-p port] [-t token] [-S] [-T ca.pem]\n"
              "       [-c cert.pem -k key.pem [-q port] [-g] [-N name] [-v]"
              "[-U]]\n",
              argv[0]);
      return 2;
    }
  }
  if (fwd_addr_from_string(&addr, addr_s) != FWD_OK || port == 0 ||
      port > 65535 || qport == 0 || qport > 65535) {
    fprintf(stderr, "bad address or port\n");
    return 2;
  }
  if (!cert != !key) {
    fprintf(stderr, "-c and -k go together\n");
    return 2;
  }
  if (!cert && !udp) {
    fprintf(stderr, "-U needs QUIC (-c/-k), nothing would listen\n");
    return 2;
  }
  if (cert && gen && stat(cert, &st) < 0 && stat(key, &st) < 0) {
    rc = fwd_tls_make_selfsigned(cert, key, &addr, name, 3650);
    if (rc != FWD_OK) {
      fprintf(stderr, "cannot create certificate: %s\n", fwd_strerror(rc));
      return 1;
    }
    fprintf(stderr, "created self-signed certificate %s (key %s)\n", cert, key);
  }

  g_ctx = fwd_create(&cfg);
  if (!g_ctx) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  if (cert) {
    rc = fwd_ctl_listen_quic(g_ctx, &addr, (uint16_t)qport, cert, key);
    if (rc != FWD_OK) {
      fprintf(stderr, "QUIC listen on %s:%u: %s\n", addr_s, qport,
              fwd_last_error(g_ctx, NULL));
      return 1;
    }
  }
  if ((cert || tun_ca) && fwd_tunnel_tls(g_ctx, cert, key, tun_ca) != FWD_OK) {
    fprintf(stderr, "tunnel TLS: %s\n", fwd_last_error(g_ctx, NULL));
    return 1;
  }
  if (udp && (rc = fwd_ctl_listen(g_ctx, &addr, (uint16_t)port)) != FWD_OK) {
    fprintf(stderr, "listen on %s:%u: %s\n", addr_s, port,
            fwd_last_error(g_ctx, NULL));
    return 1;
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  if (cert) {
    fprintf(stderr, "fwdd %s: QUIC control on %s:%u\n", FWD_VERSION, addr_s,
            qport);
    if (fwd_tls_fingerprint(cert, fp, sizeof fp) == FWD_OK)
      fprintf(stderr, "  certificate SHA-256 %s (control and tunnels)\n",
              fp);
  }
  if (udp)
    fprintf(stderr, "fwdd %s: UDP control on %s:%u%s\n", FWD_VERSION, addr_s,
            port, cert ? " (unencrypted)" : "");

  rc = fwd_run_forever(g_ctx);
  if (rc < 0)
    fprintf(stderr, "event loop: %s\n", fwd_last_error(g_ctx, NULL));
  fwd_destroy(g_ctx);
  return rc < 0 ? 1 : 0;
}
