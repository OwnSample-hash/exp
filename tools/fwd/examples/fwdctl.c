/*
 * fwdctl - command line client for a running fwdd.
 *
 *   fwdctl [-s server] [-t token] [-n] [-r]
 *          [-u] [-p udp-port] [-q quic-port] [-C ca.pem] [-F sha256] [-N name]
 *          [-A] <command> [args...]
 *
 * Transport: QUIC on port 7778 when built with FWD_ENABLE_QUIC, otherwise
 * (or with -u) plain UDP on port 7777. -A tries QUIC and falls back to UDP
 * when the QUIC server cannot be reached - never when its certificate is
 * rejected.
 *
 * Trusting the server certificate (QUIC):
 *   -C file    trust the certificates in this PEM file; for a self-signed
 *              server just pass its certificate
 *   -F hex     pin the SHA-256 fingerprint fwdd prints at start (chain and
 *              name are then not checked; with -C both must pass)
 *   -N name    name the certificate must carry (default: the server IP)
 *   neither    the system trust store
 *
 * <command> is a name (fwd-add, route-del, list-fwd ...) or a number. The
 * arguments are the human readable forms; IP addresses are converted to
 * their numerical wire representation. -n only prints the encoded command.
 *
 * The response is shown decoded (fwd_response_decode): tables for the list
 * commands, "ok, id N" for adds, errors on stderr. -r prints the raw
 * response line instead, for scripts.
 *
 *   fwdctl fwd-add tcp 0.0.0.0 8080 192.168.1.10 80
 *   fwdctl route-add 10.9.0.0 24 192.168.1.1 eth0 0 0
 *   fwdctl tun-add tun tun0 0.0.0.0 4000 0 0 10.77.0.1 24 0 listen
 *   fwdctl list-tun-peers 1       (a listen mode tunnel has many peers)
 *   fwdctl list-fwd
 *
 * Tunnels take up to three more arguments: mode (connect, listen),
 * transport (auto = QUIC with plain UDP fallback, the default; quic; udp)
 * and the SHA-256 pin of the peer's certificate ("-" = none):
 *
 *   fwdctl tun-add tun tun0 0.0.0.0 4000 203.0.113.7 4000 10.77.0.1 24 0 \
 *          connect quic AB:CD:...:EF
 */
#include "fwd.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *server_s = "127.0.0.1", *token = NULL;
  unsigned port = 7777, qport = 7778;
  int dry = 0, raw = 0, quic = FWD_ENABLE_QUIC, fallback = 0, opt, num, n;
  char enc[FWD_MAX_CMD + 1], line[FWD_MAX_CMD + 80], resp[FWD_MAX_RESP + 1];
  char err[256], text[FWD_MAX_DECODED];
  fwd_tls_trust trust = {0};
  fwd_addr server;
  fwd_response r;

  while ((opt = getopt(argc, argv, "+s:p:q:t:nruC:F:N:Ah:v")) != -1) {
    switch (opt) {
    case 's':
      server_s = optarg;
      break;
    case 'p':
      port = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 't':
      token = optarg;
      break;
    case 'n':
      dry = 1;
      break;
    case 'r':
      raw = 1;
      break;
    case 'q':
      qport = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 'u':
      quic = 0;
      break;
    case 'C':
      trust.ca_file = optarg;
      break;
    case 'F':
      trust.pin_sha256 = optarg;
      break;
    case 'N':
      trust.server_name = optarg;
      break;
    case 'A':
      fallback = 1;
      break;
    case 'v':
      fprintf(stderr, "%s libfwd version %d.%d.%d\n", argv[0], FWD_MAJOR,
              FWD_MINOR, FWD_PATCH);
      return 0;
    default:
      goto usage;
    }
  }
  if (optind >= argc)
    goto usage;

  num = fwd_cmd_by_name(argv[optind]);
  if (num < 0 && argv[optind][0] >= '0' && argv[optind][0] <= '9')
    num = atoi(argv[optind]);
  if (num < 0 || !fwd_cmd_name(num)) {
    fprintf(stderr, "unknown command '%s'\n", argv[optind]);
    return 2;
  }
  n = fwd_cmd_encode_strs(num, (const char *const *)&argv[optind + 1],
                          argc - optind - 1, enc, sizeof enc);
  if (n < 0) {
    fprintf(stderr, "cannot encode command: %s\n", fwd_strerror(n));
    return 2;
  }
  if (token)
    snprintf(line, sizeof line, "%s@%s", token, enc);
  else
    snprintf(line, sizeof line, "%s", enc);

  if (dry) {
    puts(line);
    return 0;
  }
  if (!quic && (trust.ca_file || trust.pin_sha256 || trust.server_name)) {
    fprintf(stderr, FWD_ENABLE_QUIC
                        ? "-C/-F/-N only apply to QUIC, not with -u\n"
                        : "-C/-F/-N need QUIC, built without it\n");
    return 2;
  }
  if (fwd_addr_from_string(&server, server_s) != FWD_OK || port == 0 ||
      port > 65535 || qport == 0 || qport > 65535) {
    fprintf(stderr, "bad server address or port\n");
    return 2;
  }
  n = FWD_E_NOSYS;
  if (quic) {
    n = fwd_client_request_quic(&server, (uint16_t)qport, &trust, line, resp,
                                sizeof resp, 3000, err, sizeof err);
    if (n < 0) {
      fprintf(stderr, "QUIC request failed: %s%s%s\n", fwd_strerror(n),
              err[0] ? ": " : "", err);
      /* only an unreachable server may fall back, a rejected
       * certificate must not be downgraded to plain UDP */
      if (!fallback || n == FWD_E_TLS || n == FWD_E_INVAL)
        return 1;
      fprintf(stderr, "falling back to unencrypted UDP\n");
    }
  }
  if (n < 0)
    n = fwd_client_request(&server, (uint16_t)port, line, resp, sizeof resp,
                           2000);
  if (n < 0) {
    fprintf(stderr, "request failed: %s\n", fwd_strerror(n));
    return 1;
  }
  if (fwd_response_parse(resp, &r) != FWD_OK) {
    puts(resp);
    return 1;
  }
  if (raw || fwd_response_decode(resp, text, sizeof text) < 0)
    puts(resp); /* raw requested, or not decodable: show it as is */
  else
    fputs(text, r.status ? stderr : stdout);
  return r.status ? 1 : 0;

usage:
  fprintf(stderr,
          "usage: %s [-v] [-s server] [-t token] [-n] [-r] [-u] [-p udp-port] "
          "[-q quic-port]\n"
          "       [-C ca.pem] [-F sha256] [-N name] [-A] <command> "
          "[args...]\n",
          argv[0]);
  return 2;
}
