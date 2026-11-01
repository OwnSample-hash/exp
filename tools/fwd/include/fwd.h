/*
 * fwd.h - libfwd public API.
 *
 * A small stand-alone library (libc only) that can
 *   - forward TCP/UDP ports (listen addr:port -> remote addr:port)
 *   - tunnel traffic over TUN/TAP devices (Linux)
 *   - add/delete/modify routes through rtnetlink and set the sysctls that
 *     packet forwarding needs (net.ipv4.ip_forward ...)
 *   - do all of the above for IPv4 and IPv6
 *   - be remote controlled with colon separated text commands received over
 *     QUIC (TLS 1.3, optional, needs OpenSSL) or plain UDP, including listing
 *     of forwards, tunnels and routes
 *
 * Threading: a fwd_ctx is single threaded. All work happens inside
 * fwd_run(); no threads are created. Ignore SIGPIPE in your application.
 *
 * Return codes: functions returning int return FWD_OK (0) or a negative
 * FWD_E_* code. Details of the last failure are available through
 * fwd_last_error().
 */
#ifndef FWD_H
#define FWD_H

#include "fwd_config.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FWD_VERSION "1.0.0"

#define FWD_MAX_CMD 512        /* longest accepted command string        */
#define FWD_MAX_RESP 1400      /* longest response (fits one datagram)   */
#define FWD_IFNAME_MAX 16      /* includes the NUL                       */
#define FWD_SYSCTL_KEY_MAX 96
#define FWD_SYSCTL_VAL_MAX 32
#define FWD_MAX_ARGS 16

/* ------------------------------------------------------------------ */
/* Error codes                                                        */
/* ------------------------------------------------------------------ */
enum {
    FWD_OK = 0,
    FWD_E_INVAL = -1,   /* invalid argument / unsupported family        */
    FWD_E_PARSE = -2,   /* malformed command string                      */
    FWD_E_NOSYS = -3,   /* function disabled or unavailable here         */
    FWD_E_SYS = -4,     /* a system call failed (see fwd_last_error)     */
    FWD_E_NOENT = -5,   /* no such forward / tunnel / route              */
    FWD_E_EXIST = -6,   /* already exists                                */
    FWD_E_NOMEM = -7,
    FWD_E_RANGE = -8,   /* number out of range                           */
    FWD_E_BUF = -9,     /* output buffer too small                       */
    FWD_E_AUTH = -10,   /* wrong / missing control token                 */
    FWD_E_TIMEOUT = -11, /* no response from control server              */
    FWD_E_TLS = -12      /* QUIC/TLS handshake or certificate check failed */
};

const char *fwd_strerror(int rc);

/* ------------------------------------------------------------------ */
/* Addresses                                                          */
/* ------------------------------------------------------------------ */
enum { FWD_V4 = 4, FWD_V6 = 6 };

typedef struct fwd_addr {
    uint8_t family;    /* 0 (unset), FWD_V4 or FWD_V6                   */
    uint8_t bytes[16]; /* network byte order; 4 used for IPv4           */
} fwd_addr;

void fwd_addr_from_u32(fwd_addr *a, uint32_t host_order_ipv4);
int fwd_addr_to_u32(const fwd_addr *a, uint32_t *host_order_ipv4);

/* Human notation: "192.168.1.1", "2001:db8::1". */
int fwd_addr_from_string(fwd_addr *a, const char *text);
int fwd_addr_to_string(const fwd_addr *a, char *buf, size_t len);

/* Numeric notation used on the wire:
 *   IPv4: unsigned decimal of the 32 bit value        192.168.1.1 -> 3232235777
 *   IPv6: "0x" + 32 hex digits of the 128 bit value   ::1 -> 0x000...0001
 * fwd_addr_decode also accepts "0x" + up to 8 hex digits as IPv4.
 * Both return the string length (encode) or FWD_OK (decode) / error.   */
int fwd_addr_encode(const fwd_addr *a, char *buf, size_t len);
int fwd_addr_decode(fwd_addr *a, const char *numeric);

/* ------------------------------------------------------------------ */
/* Specifications                                                     */
/* ------------------------------------------------------------------ */
enum { FWD_PROTO_TCP = 1, FWD_PROTO_UDP = 2 };
enum { FWD_TUN_TUN = 1, FWD_TUN_TAP = 2 };
enum { FWD_TUN_CONNECT = 0, FWD_TUN_LISTEN = 1 };
/* tunnel transport: what the spec asks for ... */
enum { FWD_TT_AUTO = 0, FWD_TT_QUIC = 1, FWD_TT_UDP = 2 };
/* ... and what a tunnel uses right now (fwd_tunnel_info.via) */
enum {
    FWD_VIA_NONE = 0,     /* nothing yet: no peer / QUIC handshake running */
    FWD_VIA_QUIC = 1,
    FWD_VIA_UDP = 2,
    FWD_VIA_REJECTED = 3  /* the peer speaks QUIC but the TLS handshake
                             failed (certificate / pin); no UDP fallback,
                             retried with backoff                      */
};
#define FWD_PIN_MAX 96    /* tunnel pin text incl. NUL ("AB:CD:..." form) */
#define FWD_TUN_MAX_PEERS 64     /* peers of one listen mode tunnel      */
#define FWD_TUN_MAX_ROUTES 256   /* learned inner addresses per tunnel   */
#define FWD_TUN_PEER_IDLE_MS 120000 /* plain UDP peer forgotten after this
                                       much silence (unless it is the only
                                       one)                               */

typedef struct fwd_forward_spec {
    uint8_t proto; /* FWD_PROTO_* */
    fwd_addr laddr;
    uint16_t lport;
    fwd_addr raddr;
    uint16_t rport;
} fwd_forward_spec;

/*
 * A tunnel carries the packets (TUN) or ethernet frames (TAP) of a local
 * TUN/TAP interface to/from a peer over UDP port laddr:lport.
 *
 * mode FWD_TUN_CONNECT: the peer is raddr:rport, fixed. Datagrams from any
 *   other source are dropped. Both ends can run this with local/remote
 *   swapped.
 * mode FWD_TUN_LISTEN: the tunnel waits on laddr:lport and serves any number
 *   of peers (up to FWD_TUN_MAX_PEERS) at once, each in connect mode towards
 *   laddr:lport. A peer is a plain UDP source address or a QUIC connection
 *   that passes the filter raddr:rport, where an unset or all-zero raddr
 *   means any address and rport 0 any port. Until a peer has shown up,
 *   packets from the interface are dropped.
 *   Which peer gets a packet from the interface: the tunnel learns the inner
 *   source address of what each peer sends (TUN: IPv4/IPv6 source, TAP:
 *   source MAC; the most recent sender of an address owns it, up to
 *   FWD_TUN_MAX_ROUTES addresses) and sends a packet to the peer that owns
 *   its destination. Broadcast, multicast and unknown destinations go to
 *   every peer, like a switch does; with a single peer everything goes to
 *   it. A peer that changes its address or port (NAT rebinding, restart)
 *   becomes a new peer and takes its addresses along with its first packet.
 *   Plain UDP peers are forgotten after FWD_TUN_PEER_IDLE_MS of silence
 *   (except the last one), QUIC peers when their connection ends. When the
 *   table is full the peer silent for longest makes room. Peers are not
 *   isolated from each other: one may claim another's inner address by
 *   sending from it.
 *
 * transport (needs FWD_ENABLE_QUIC for anything but FWD_TT_UDP):
 *   FWD_TT_AUTO (default) - QUIC (TLS 1.3, ALPN "fwdtun/1"), plain UDP as
 *     fallback. A connect mode tunnel opens a QUIC connection to its peer;
 *     if nothing QUIC comes back within 3 s it falls back to plain UDP and
 *     tries QUIC again later (10 s, doubling up to 5 min). A peer that
 *     answers in QUIC but fails the TLS handshake is NOT fallen back from
 *     (FWD_VIA_REJECTED). A listen mode tunnel accepts both on the same
 *     port; plain UDP peers are only served while no QUIC peer is connected
 *     (the first QUIC peer drops them), and on TUN plain datagrams are only
 *     taken if they look like IPv4/IPv6 packets.
 *     Opportunistic: someone who can block QUIC on the path can force
 *     plain UDP. Use FWD_TT_QUIC where that matters.
 *   FWD_TT_QUIC - QUIC only; packets are dropped while it is not up.
 *   FWD_TT_UDP - plain UDP only, one datagram per packet, NOT encrypted
 *     or authenticated (the format of tunnels before QUIC).
 *
 * Over QUIC every packet is sent as a 2 byte big endian length plus the
 * packet on one bidirectional stream that the connecting side opens (OpenSSL
 * has no unreliable QUIC datagrams), so a lost packet delays the following
 * ones instead of being lost. Both ends of every QUIC tunnel are TLS server
 * and client at once, with the identity set by fwd_tunnel_tls(), so two
 * connect mode ends also work.
 *
 * Authentication: pin is the SHA-256 fingerprint of the PEER's certificate
 * (hex, ':' optional, as printed by fwd_tls_fingerprint()). With a pin, or
 * without one but with a CA file in fwd_tunnel_tls(), the peer must present
 * a matching certificate in both directions - the listening end then only
 * accepts clients that have the pinned certificate. With neither, QUIC
 * encrypts but authenticates nobody (anyone who reaches the port and passes
 * the filter can be the peer, as with plain UDP).
 */
typedef struct fwd_tunnel_spec {
    uint8_t type;                 /* FWD_TUN_TUN / FWD_TUN_TAP            */
    char name[FWD_IFNAME_MAX];    /* interface name, "" = kernel chooses  */
    fwd_addr laddr;               /* local UDP endpoint                   */
    uint16_t lport;
    fwd_addr raddr;               /* peer UDP endpoint                    */
    uint16_t rport;
    fwd_addr ifaddr;              /* address for the interface            */
    uint8_t prefix;               /* its prefix length, 0 = no address    */
    uint16_t mtu;                 /* 0 = leave default                    */
    uint8_t mode;                 /* FWD_TUN_CONNECT / FWD_TUN_LISTEN     */
    uint8_t transport;            /* FWD_TT_AUTO / FWD_TT_QUIC / FWD_TT_UDP */
    char pin[FWD_PIN_MAX];        /* peer certificate SHA-256, "" = none  */
} fwd_tunnel_spec;

/* A route. gateway all-zero = none (on-link). table 0 = main. */
typedef struct fwd_route_spec {
    fwd_addr dst;
    uint8_t prefix;
    fwd_addr gateway;
    char ifname[FWD_IFNAME_MAX]; /* "" = not specified */
    uint32_t metric;             /* 0 = kernel default */
    uint32_t table;
} fwd_route_spec;

typedef struct fwd_forward_info {
    uint32_t id;
    fwd_forward_spec spec;
    uint32_t active; /* live TCP connections / UDP sessions */
} fwd_forward_info;

typedef struct fwd_tunnel_info {
    uint32_t id;
    fwd_tunnel_spec spec; /* name holds the real interface name */
    uint64_t tx_packets;
    uint64_t rx_packets;
    fwd_addr peer;        /* where packets currently go: raddr in connect
                             mode, the learned peer in listen mode
                             (family 0 = none yet)                     */
    uint16_t peer_port;
    uint8_t via;          /* FWD_VIA_*: transport in use right now      */
    uint32_t peers;       /* connected peers; in listen mode peer/via are
                             those of the one heard from last          */
} fwd_tunnel_info;

typedef struct fwd_tunnel_peer {
    fwd_addr addr;
    uint16_t port;
    uint8_t via;          /* FWD_VIA_QUIC / FWD_VIA_UDP                 */
    uint64_t tx_packets;  /* to this peer (flooded packets included)    */
    uint64_t rx_packets;
    uint32_t idle_ms;     /* since the last packet from it              */
} fwd_tunnel_peer;

/* ------------------------------------------------------------------ */
/* Context                                                            */
/* ------------------------------------------------------------------ */
typedef struct fwd_ctx fwd_ctx;

typedef struct fwd_config {
    int auto_sysctl;       /* enable ip forwarding when adding routes or
                              addressed tunnels (default 1)              */
    unsigned max_conns;    /* TCP connections + UDP sessions (def. 1024)  */
    unsigned udp_timeout_ms; /* idle UDP session timeout (def. 60000)     */
    char ctl_token[64];    /* if non empty, control datagrams must start
                              with "<token>@" (default empty)            */
} fwd_config;

void fwd_config_default(fwd_config *cfg);
fwd_ctx *fwd_create(const fwd_config *cfg); /* cfg may be NULL */
void fwd_destroy(fwd_ctx *ctx);

/* Message and errno of the most recent failure on this context. */
const char *fwd_last_error(const fwd_ctx *ctx, int *sys_errno);

/* ------------------------------------------------------------------ */
/* Port forwards                                                      */
/* ------------------------------------------------------------------ */
int fwd_forward_add(fwd_ctx *ctx, const fwd_forward_spec *spec, uint32_t *id);
int fwd_forward_del(fwd_ctx *ctx, uint32_t id);
/* Replace the specification of forward <id>. If the listen endpoint is
 * unchanged only the remote side changes. Failure leaves it untouched. */
int fwd_forward_mod(fwd_ctx *ctx, uint32_t id, const fwd_forward_spec *spec);
/* cb returns non zero to stop iterating. */
int fwd_forward_list(fwd_ctx *ctx,
                     int (*cb)(const fwd_forward_info *, void *), void *ud);

/* ------------------------------------------------------------------ */
/* Tunnels (TUN/TAP)                                                  */
/* ------------------------------------------------------------------ */
int fwd_tunnel_add(fwd_ctx *ctx, const fwd_tunnel_spec *spec, uint32_t *id);
int fwd_tunnel_del(fwd_ctx *ctx, uint32_t id);
/* Type and name cannot change. Mode, peer (filter), local endpoint,
 * interface address and MTU can. A listen mode tunnel drops all peers when
 * the local endpoint or mode changes, and those that no longer pass a new
 * filter. */
int fwd_tunnel_mod(fwd_ctx *ctx, uint32_t id, const fwd_tunnel_spec *spec);
int fwd_tunnel_get(fwd_ctx *ctx, uint32_t id, fwd_tunnel_info *info);
/* TLS identity of QUIC tunnels: PEM certificate (chain) and key, both NULL
 * = an in-memory self-signed certificate made on first use (fine for
 * encryption, but it changes on every start, so the peer cannot pin it).
 * ca_file (may be NULL): PEM certificates that tunnel peers without a pin
 * must chain to (any certificate in it is an anchor); then every peer must
 * present a certificate. Running QUIC tunnels reconnect with the new
 * settings. FWD_E_NOSYS without FWD_ENABLE_QUIC. */
int fwd_tunnel_tls(fwd_ctx *ctx, const char *cert_file, const char *key_file,
                   const char *ca_file);
int fwd_tunnel_list(fwd_ctx *ctx,
                    int (*cb)(const fwd_tunnel_info *, void *), void *ud);
/* The peers of tunnel <id> (connect mode: the one peer, if up). */
int fwd_tunnel_peers(fwd_ctx *ctx, uint32_t id,
                     int (*cb)(const fwd_tunnel_peer *, void *), void *ud);

/* ------------------------------------------------------------------ */
/* Routes (rtnetlink) and kernel parameters                           */
/* ------------------------------------------------------------------ */
int fwd_route_add(fwd_ctx *ctx, const fwd_route_spec *spec);
int fwd_route_del(fwd_ctx *ctx, const fwd_route_spec *spec);
/* "ip route change": replaces the route with the same dst/prefix/table/
 * metric; fails with an error if there is none. */
int fwd_route_mod(fwd_ctx *ctx, const fwd_route_spec *spec);
/* Lists unicast routes of all tables, read from the kernel. */
int fwd_route_list(fwd_ctx *ctx,
                   int (*cb)(const fwd_route_spec *, void *), void *ud);

/* Only keys below net.ipv4. and net.ipv6. are accepted. */
int fwd_sysctl_set(fwd_ctx *ctx, const char *key, const char *value);
int fwd_sysctl_get(fwd_ctx *ctx, const char *key, char *value, size_t len);

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */
/* One iteration: waits up to timeout_ms for activity and services it.
 * Returns the number of ready descriptors (>= 0) or a negative error. */
int fwd_run(fwd_ctx *ctx, int timeout_ms);
/* Loops fwd_run() until fwd_stop() is called (signal handler safe). */
int fwd_run_forever(fwd_ctx *ctx);
void fwd_stop(fwd_ctx *ctx);

/* ------------------------------------------------------------------ */
/* Command protocol                                                   */
/* ------------------------------------------------------------------ */
/*
 * A command is   <number>[:<arg>[:<arg>...]]
 * All arguments are decimal numbers, numerically encoded IP addresses (see
 * fwd_addr_encode) or short words without ':' such as interface names.
 *
 *  no  name         arguments
 *   0  ping         -
 *   1  fwd-add      proto laddr lport raddr rport
 *   2  fwd-del      id
 *   3  fwd-mod      id proto laddr lport raddr rport
 *   4  tun-add      type name laddr lport raddr rport ifaddr prefix mtu
 *                   [mode [transport [pin]]]
 *   5  tun-del      id
 *   6  tun-mod      id type name laddr lport raddr rport ifaddr prefix mtu
 *                   [mode [transport [pin]]]
 *   7  route-add    dst prefix gateway ifname metric table
 *   8  route-del    dst prefix gateway ifname metric table
 *   9  route-mod    dst prefix gateway ifname metric table
 *  10  sysctl-set   key value
 *  11  sysctl-get   key
 *  12  list-fwd     [offset]
 *  13  list-tun     [offset]
 *  14  list-routes  [offset]
 *  15  list-tun-peers  id [offset]
 *
 * proto: 1 tcp, 2 udp.  type: 1 tun, 2 tap.  mode: 0 connect (default),
 * 1 listen.  transport: 0 auto (default), 1 quic, 2 udp.  pin: 64 hex
 * digits without ':' or empty.  Unused address = 0.
 * Empty strings (e.g. ifname) are written as nothing between colons.
 *
 * Responses:
 *   success   <cmd>:0[:<payload>]
 *   failure   <cmd>:<code>:<errno>:<message>      (code > 0, see FWD_E_*,
 *                                                   code = -FWD_E_xxx)
 * add commands return the new id as payload (tun-add: id:ifname), sysctl-get
 * the value. List commands return  total:count:<count records>  where the
 * record layout is fixed per command (see README) and offset allows paging.
 */
typedef enum fwd_cmd_num {
    FWD_CMD_PING = 0,
    FWD_CMD_FWD_ADD = 1,
    FWD_CMD_FWD_DEL = 2,
    FWD_CMD_FWD_MOD = 3,
    FWD_CMD_TUN_ADD = 4,
    FWD_CMD_TUN_DEL = 5,
    FWD_CMD_TUN_MOD = 6,
    FWD_CMD_ROUTE_ADD = 7,
    FWD_CMD_ROUTE_DEL = 8,
    FWD_CMD_ROUTE_MOD = 9,
    FWD_CMD_SYSCTL_SET = 10,
    FWD_CMD_SYSCTL_GET = 11,
    FWD_CMD_LIST_FWD = 12,
    FWD_CMD_LIST_TUN = 13,
    FWD_CMD_LIST_ROUTES = 14,
    FWD_CMD_LIST_TUN_PEERS = 15,
    FWD_CMD__COUNT
} fwd_cmd_num;

typedef struct fwd_cmd {
    int num;      /* fwd_cmd_num                                   */
    uint32_t id;  /* del/mod: target id; list: starting offset
                     (list-tun-peers: the tunnel)                    */
    uint32_t offset; /* list-tun-peers: starting offset             */
    union {
        fwd_forward_spec fwd;
        fwd_tunnel_spec tun;
        fwd_route_spec route;
        struct {
            char key[FWD_SYSCTL_KEY_MAX];
            char value[FWD_SYSCTL_VAL_MAX];
        } sysctl;
    } u;
} fwd_cmd;

const char *fwd_cmd_name(int num);       /* NULL if unknown */
int fwd_cmd_by_name(const char *name);   /* number, or -1   */

/* struct -> encoded string. Returns the string length or a negative error. */
int fwd_cmd_encode(const fwd_cmd *cmd, char *out, size_t outlen);

/* strings -> encoded string. The arguments are the human readable forms in
 * the order of the table above (IPs as "10.0.0.1" / "fe80::1", proto as
 * "tcp"/"udp" or a number, type as "tun"/"tap" or a number). IP addresses
 * are converted to their numerical representation. The result is validated.
 * Returns the string length or a negative error. */
int fwd_cmd_encode_strs(int num, const char *const *argv, int argc,
                        char *out, size_t outlen);

/* encoded string -> struct. FWD_OK or a negative error. */
int fwd_cmd_decode(const char *line, fwd_cmd *cmd);

/* Execute an encoded command on the context and produce the response string.
 * Never fails because of the command itself - failures become error
 * responses. Returns the response length, or FWD_E_BUF if resp is too small
 * (give it at least FWD_MAX_RESP + 1). */
int fwd_execute(fwd_ctx *ctx, const char *line, char *resp, size_t resplen);

typedef struct fwd_response {
    int cmd;
    int status;     /* 0 = success, otherwise positive error code */
    int sys_errno;  /* errno of the failure, if any               */
    char message[128];
    const char *payload; /* success: text after "<cmd>:0:" (points into the
                            parsed string; "" if none)                  */
} fwd_response;

int fwd_response_parse(const char *text, fwd_response *out);

/* Response string -> readable text, one or more '\n' terminated lines:
 *   errors        "fwd-add: error 4 (system call failed): bind: ... [errno 98]"
 *   add commands  "ok, id 3"  (tun-add: "ok, id 3, interface tun0")
 *   sysctl-get    the value
 *   lists         a table with a header line, addresses in human notation
 *                 ("192.168.1.10:80", "[2001:db8::1]:80", "10.0.0.0/8"),
 *                 followed by "(n of total entries ...)" if paged
 *   others        "ok"
 * Returns the text length, FWD_E_PARSE if the response is not well formed
 * for its command, or FWD_E_BUF if out is too small (FWD_MAX_DECODED is
 * always enough). */
#define FWD_MAX_DECODED 16384 /* widest table one response can hold */
int fwd_response_decode(const char *text, char *out, size_t outlen);

/* ------------------------------------------------------------------ */
/* UDP control channel (secondary when QUIC is available)             */
/* ------------------------------------------------------------------ */
/* Start listening for command datagrams. Each datagram holds one command
 * (optionally prefixed with "<token>@") and is answered with one response
 * datagram to the sender. Serviced from fwd_run(). Bind to a loopback or
 * management address: commands can change routes and kernel parameters. */
int fwd_ctl_listen(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port);
void fwd_ctl_close(fwd_ctx *ctx);

/* Blocking client helper: send one command, wait for the response. Returns
 * the response length or a negative error (FWD_E_TIMEOUT). */
int fwd_client_request(const fwd_addr *server, uint16_t port, const char *cmd,
                       char *resp, size_t resplen, int timeout_ms);

/* ------------------------------------------------------------------ */
/* QUIC control channel (FWD_ENABLE_QUIC, OpenSSL >= 3.5)              */
/* ------------------------------------------------------------------ */
/*
 * The same commands and responses as the UDP channel, carried over QUIC
 * (ALPN "fwdctl/1"): the client opens one bidirectional stream per command,
 * sends "[<token>@]<command>" and finishes the stream; the server answers
 * with the response and finishes its side. Traffic is encrypted and the
 * server is authenticated by its certificate, so the token is no longer
 * sent in the clear. The UDP channel stays available next to it (e.g. on
 * another port) as the secondary, unencrypted transport.
 *
 * Without FWD_ENABLE_QUIC all of these return FWD_E_NOSYS.
 */
#define FWD_QUIC_ALPN "fwdctl/1"
#define FWD_TUN_ALPN "fwdtun/1"   /* QUIC tunnels, see fwd_tunnel_spec    */
#define FWD_FINGERPRINT_LEN 96 /* "AB:CD:..." SHA-256 + NUL             */

/* Serve commands over QUIC. cert_file is a PEM certificate (chain), key_file
 * its PEM private key. Serviced from fwd_run(). */
int fwd_ctl_listen_quic(fwd_ctx *ctx, const fwd_addr *addr, uint16_t port,
                        const char *cert_file, const char *key_file);
void fwd_ctl_close_quic(fwd_ctx *ctx);

/*
 * How a client decides to trust the server certificate. All members may be
 * NULL; a NULL trust pointer is the same as all NULL.
 *
 *  ca_file      PEM file with trusted certificates. For a self-signed
 *               server, this is simply the server's own certificate. Any
 *               certificate in the file is a trust anchor (partial chains
 *               are accepted). NULL = the system trust store.
 *  pin_sha256   SHA-256 fingerprint of the server certificate, hex with or
 *               without ':' (as printed by fwd_tls_fingerprint() / fwdd /
 *               "openssl x509 -fingerprint -sha256"). If set and ca_file is
 *               NULL, the pin alone decides: chain and name are not checked,
 *               which is what makes an unmodified self-signed certificate
 *               usable. With ca_file set, both must pass.
 *  server_name  Name the certificate must be valid for (DNS SAN), also sent
 *               as SNI. NULL = the server IP address must be in the
 *               certificate's IP SANs. Not checked in pin-only mode.
 */
typedef struct fwd_tls_trust {
    const char *ca_file;
    const char *pin_sha256;
    const char *server_name;
} fwd_tls_trust;

/* Blocking client helper over QUIC: connect, verify the server, send one
 * command, wait for the response. Returns the response length or a negative
 * error: FWD_E_TIMEOUT (no answer), FWD_E_TLS (handshake / certificate
 * rejected), FWD_E_SYS ... If err is not NULL it receives a readable reason
 * on failure. */
int fwd_client_request_quic(const fwd_addr *server, uint16_t port,
                            const fwd_tls_trust *trust, const char *cmd,
                            char *resp, size_t resplen, int timeout_ms,
                            char *err, size_t errlen);

/* SHA-256 fingerprint of the first certificate in a PEM file as upper case
 * hex pairs separated by ':' (needs FWD_FINGERPRINT_LEN bytes). */
int fwd_tls_fingerprint(const char *cert_file, char *out, size_t outlen);

/* Create a self-signed certificate (EC P-256, SHA-256) and its key. The
 * certificate carries ip (if not NULL) as IP SAN and name (if not NULL) as
 * DNS SAN and common name, and is valid for days days. The key file is
 * created with mode 0600. Existing files are overwritten. */
int fwd_tls_make_selfsigned(const char *cert_file, const char *key_file,
                            const fwd_addr *ip, const char *name,
                            unsigned days);

#ifdef __cplusplus
}
#endif
#endif /* FWD_H */
