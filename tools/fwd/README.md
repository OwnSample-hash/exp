# libfwd

A small stand-alone C library (libc only, no threads, no third-party code –
except OpenSSL for the optional QUIC support) for Linux that can

* **forward ports** – TCP and UDP, `listen addr:port -> remote addr:port`
* **tunnel traffic over TUN/TAP** devices to a peer – over QUIC (encrypted,
  optionally pinned) by default, plain UDP as fallback
* **add / delete / modify routes through netlink** and switch on the kernel
  options forwarding needs (`net.ipv4.ip_forward`,
  `net.ipv6.conf.all.forwarding`)
* handle **IPv4 and IPv6** everywhere
* be **remote controlled over QUIC** (TLS 1.3, encrypted, server
  authenticated, self-signed certificates supported) **or plain UDP** with
  colon separated text commands, including listing of forwards, tunnels and
  routes, and error responses
* run on top of a **syscall wrapper** whose functions can be switched off at
  compile time or replaced at run time

```
make            # build/libfwd.a  build/libfwd.so  build/fwdd  build/fwdctl
make test       # loopback tests, no root required
make QUIC=0     # without QUIC: libc only, no OpenSSL
```

QUIC needs OpenSSL >= 3.5 (its QUIC server API). The Makefile enables it
(`-DFWD_ENABLE_QUIC=1 -lssl -lcrypto`) when `pkg-config` finds one; `QUIC=1`
/ `QUIC=0` force it. Programs linking `libfwd.a` built with QUIC also need
`-lssl -lcrypto`.

## Quick start

```c
#include "fwd.h"

fwd_ctx *ctx = fwd_create(NULL);

fwd_addr any, ctl;
fwd_forward_spec s = { .proto = FWD_PROTO_TCP, .lport = 8080, .rport = 80 };
fwd_addr_from_string(&s.laddr, "0.0.0.0");
fwd_addr_from_string(&s.raddr, "192.168.1.10");

uint32_t id;
fwd_forward_add(ctx, &s, &id);

fwd_addr_from_string(&ctl, "127.0.0.1");
fwd_ctl_listen_quic(ctx, &ctl, 7778, "cert.pem", "key.pem"); /* optional */
fwd_ctl_listen(ctx, &ctl, 7777);      /* optional, plain UDP */

fwd_run_forever(ctx);                 /* or call fwd_run(ctx, ms) from your loop */
```

Command line: start `fwdd`, then

```sh
fwdd -c cert.pem -k key.pem -g -t s3cret   # QUIC on :7778 + UDP on :7777
                                           # -g: create a self-signed cert
                                           # if missing; prints its SHA-256
fwdctl -t s3cret -F <sha256> ping           # QUIC, pinned certificate
fwdctl -t s3cret -C cert.pem ping           # QUIC, certificate file trusted
fwdctl -u -t s3cret ping                    # plain UDP

fwdctl fwd-add tcp 0.0.0.0 8080 192.168.1.10 80
fwdctl fwd-mod 1 tcp 0.0.0.0 8080 192.168.1.11 80
fwdctl route-add 10.9.0.0 24 192.168.1.1 eth0 0 0
fwdctl tun-add tun tun0 192.0.2.1 4000 192.0.2.2 4000 10.77.0.1 24 1400
fwdctl tun-add tun tun0 0.0.0.0 4000 0 0 10.77.0.1 24 1400 listen  # serve many peers
fwdctl list-tun-peers 1                                  # who is connected
fwdctl tun-add tun tun0 0.0.0.0 4000 192.0.2.2 4000 10.77.0.1 24 1400 \
       connect quic <peer sha256>                 # QUIC only, peer pinned
fwdctl list-fwd | list-tun | list-routes
fwdctl -n fwd-add tcp 127.0.0.1 9000 127.0.0.1 9001     # just print the encoding
fwdctl -r list-fwd                                       # raw response line
```

`fwdctl` shows responses decoded (see `fwd_response_decode()` below) and
prints errors on stderr; the exit status is 0 on success and 1 on any error.
Scripts that parse the wire format should pass `-r`.

```sh
$ fwdctl list-fwd
ID  PROTO  LISTEN           REMOTE            ACTIVE
2   udp    [::1]:19002      [2001:db8::5]:53  0
1   tcp    127.0.0.1:19000  127.0.0.1:19001   0
$ fwdctl fwd-del 9
fwd-del: error 5 (no such entry): no forward with id 9
```

## Command protocol

A command is a string `<number>[:<arg>[:<arg>...]]`. Arguments are decimal
numbers, numerically encoded IP addresses, or short words without `:` (interface
names, sysctl keys). Empty strings are written as nothing between two colons.

**IP addresses are never dotted notation on the wire**

| family | encoding | example |
|---|---|---|
| IPv4 | decimal value of the 32 bit address | `192.168.1.1` → `3232235777` |
| IPv6 | `0x` + 32 hex digits of the 128 bit value | `::1` → `0x00000000000000000000000000000001` |

(`0x` + up to 8 hex digits is also accepted as IPv4 when decoding.) An unused
address is `0`.

| no | name | arguments |
|---|---|---|
| 0 | ping | – |
| 1 | fwd-add | proto laddr lport raddr rport |
| 2 | fwd-del | id |
| 3 | fwd-mod | id proto laddr lport raddr rport |
| 4 | tun-add | type name laddr lport raddr rport ifaddr prefix mtu [mode [transport [pin]]] |
| 5 | tun-del | id |
| 6 | tun-mod | id type name laddr lport raddr rport ifaddr prefix mtu [mode [transport [pin]]] |
| 7 | route-add | dst prefix gateway ifname metric table |
| 8 | route-del | dst prefix gateway ifname metric table |
| 9 | route-mod | dst prefix gateway ifname metric table |
| 10 | sysctl-set | key value |
| 11 | sysctl-get | key |
| 12 | list-fwd | [offset] |
| 13 | list-tun | [offset] |
| 14 | list-routes | [offset] |
| 15 | list-tun-peers | id [offset] |

`proto`: 1 = tcp, 2 = udp. `type`: 1 = tun, 2 = tap. `mode`: 0 = connect
(default when omitted), 1 = listen; `fwdctl` also takes `connect` / `listen`.
`transport`: 0 = auto (QUIC, plain UDP fallback; default), 1 = quic, 2 = udp;
`fwdctl` also takes the names. `pin`: SHA-256 of the peer's certificate as 64
hex digits, empty = none (`fwdctl` also accepts `AB:CD:...` and `-`).
Trailing arguments that have their default are left out by the encoder, so a
connect mode tunnel with default transport encodes as before.
In listen mode `raddr`/`rport` are a filter, 0 = any. `gateway` all zero = none.
`table` 0 = main. `prefix`/`mtu` 0 in `tun-add` = no address / default MTU.

### Responses

```
success   <cmd>:0[:<payload>]
failure   <cmd>:<code>:<errno>:<message>        code = -FWD_E_xxx  (1..11)
```

A command that cannot be parsed answers with `cmd = -1` (or the number if it
could be read), e.g. `-1:2:0:malformed command`. Codes: 1 invalid argument,
2 malformed command, 3 function disabled/unavailable, 4 system call failed
(`errno` field set), 5 no such entry, 6 already exists, 7 out of memory,
8 out of range, 9 buffer too small, 10 authentication failed (11 timeout and
12 TLS failure are client-side only).

Payloads: `*-add` return the id (`tun-add`: `id:ifname`), `sysctl-get` the value.
List commands return `total:count:<count records, flattened>` – `total` is the
number of entries, `count` how many fit into this datagram; ask again with
`offset` to page. Record layouts:

`peers` is the number of connected peers; in listen mode `peer`/`via` are
those of the peer heard from last. `via` is what a tunnel uses right now: 0 = nothing (no peer yet / QUIC
handshake running), 1 = QUIC, 2 = plain UDP, 3 = rejected (the peer answers in
QUIC, but its certificate or ours was not accepted; retried with backoff).

```
list-fwd     id:proto:laddr:lport:raddr:rport:active
list-tun     id:type:name:laddr:lport:raddr:rport:ifaddr:prefix:mtu:tx_packets:rx_packets:mode:peer:peer_port:transport:via:peers
list-tun-peers  peer:peer_port:via:tx_packets:rx_packets:idle_seconds
list-routes  dst:prefix:gateway:ifname:metric:table
```

### Encoding in C

```c
/* from human readable strings (IPs are converted to numbers) */
const char *argv[] = {"tcp", "127.0.0.1", "9000", "127.0.0.1", "9001"};
char line[FWD_MAX_CMD + 1];
fwd_cmd_encode_strs(FWD_CMD_FWD_ADD, argv, 5, line, sizeof line);
/* -> "1:1:2130706433:9000:2130706433:9001" */

/* from a struct */
fwd_cmd c = { .num = FWD_CMD_FWD_DEL, .id = 3 };
fwd_cmd_encode(&c, line, sizeof line);                /* -> "2:3" */

/* decode, execute, parse the answer */
fwd_cmd_decode(line, &c);
char resp[FWD_MAX_RESP + 1];
fwd_execute(ctx, line, resp, sizeof resp);
fwd_response r; fwd_response_parse(resp, &r);

/* or turn it into readable text (tables for the list commands) */
char text[FWD_MAX_DECODED];
fwd_response_decode(resp, text, sizeof text);
```

`fwd_response_decode()` prints `ok`, `ok, id N` (`tun-add`: `, interface X`),
the `sysctl-get` value, or `<cmd>: error <code> (<meaning>): <message>
[errno N]`. List responses become an aligned table with addresses in human
notation, ports and prefixes joined to them (`192.168.1.10:80`,
`[2001:db8::1]:53`, `10.0.0.0/8`), unset values as `-`, and a
`(n of total entries ...)` line when the answer is paged. A response that is
not well formed for its command gives `FWD_E_PARSE`.

Over the network use `fwd_ctl_listen_quic()` / `fwd_ctl_listen()` on the
server and `fwd_client_request_quic()` / `fwd_client_request()` (or any UDP
client) on the other side. Both channels can run at the same time on
different ports and share the token and the command set.

## QUIC control channel

QUIC is the primary transport; plain UDP stays available as the secondary one
(turn it off with `fwdd -U`). Over QUIC (ALPN `fwdctl/1`) every command is one
client-opened bidirectional stream: the client writes `[<token>@]<command>` and
finishes the stream, the server writes the response and finishes its side. A
connection may carry several commands. The server keeps up to 64 connections
with up to 8 open command streams each; idle connections time out.

### Certificates and trusting self-signed ones

The server needs a PEM certificate and key. `fwdd -g` (or
`fwd_tls_make_selfsigned()`) creates a self-signed EC P-256 certificate with
the listen address as IP SAN (plus `-N name` as DNS SAN), key mode 0600. With
OpenSSL:

```
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 3650 \
  -keyout key.pem -out cert.pem -subj /CN=fwd \
  -addext "subjectAltName=IP:192.0.2.1,DNS:fwd.example"
```

The client (`fwd_tls_trust`, `fwdctl` flags) decides what it trusts:

| fwdctl | `fwd_tls_trust` | trusted when |
|---|---|---|
| `-F <sha256>` | `pin_sha256` | the server certificate's SHA-256 fingerprint matches. Chain and name are not checked, so a self-signed certificate works unchanged. Get the value from fwdd's start message, `fwd_tls_fingerprint()` or `openssl x509 -in cert.pem -noout -fingerprint -sha256`; colons and case don't matter |
| `-C file` | `ca_file` | the certificate chains to a certificate in the PEM file. For a self-signed server pass its own certificate. Any certificate in the file is an anchor (partial chains allowed) |
| `-C file -F <sha256>` | both | both checks pass |
| neither | all NULL | the system trust store accepts it (a self-signed certificate is rejected) |
| `-N name` | `server_name` | with `-C` / system store: the certificate must be valid for `name` (also sent as SNI); without it, the server IP must be in its IP SANs |

The pin is checked after the handshake and before the command is sent, so a
mismatch never reveals the command or the token. A rejected certificate
fails with error `FWD_E_TLS` (12).

`fwdctl -A` tries QUIC and falls back to UDP only if the QUIC server cannot be
reached (timeout / socket error), never after a certificate or handshake
failure. Note that an attacker who can drop packets can force that fallback,
so leave `-A` off where the UDP channel is reachable by untrusted parties.

## Tunnel transport

`transport` per tunnel (needs a QUIC build for anything but `udp`):

| transport | behaviour |
|---|---|
| `auto` (default) | QUIC; plain UDP when the peer does not speak QUIC |
| `quic` | QUIC only – packets are dropped while it is not up |
| `udp` | plain UDP only: one packet per datagram, **not encrypted or authenticated** (the format of tunnels before QUIC) |

* **QUIC** (ALPN `fwdtun/1`, TLS 1.3) shares the tunnel's UDP port. Every end
  is TLS server and client at once: a connect mode tunnel connects to its peer
  and also accepts, so two connect mode ends work too. The connecting side
  opens one bidirectional stream; packets travel on it as a 2 byte big endian
  length plus the packet (length 0 = keepalive, every 5 s). OpenSSL has no
  unreliable QUIC datagrams, so a lost packet is retransmitted and delays the
  ones after it rather than being lost; when the stream backs up, new packets
  are dropped. A peer that goes away without closing (restart) is noticed
  after 15 s idle, then the tunnel reconnects.
* **Fallback (`auto`)**: a connect mode tunnel tries QUIC first and drops
  interface packets meanwhile. If nothing QUIC comes back within 3 s it sends
  plain UDP and tries QUIC again after 10 s, doubling up to 5 min, while plain
  UDP keeps working. A peer that answers in QUIC but fails the TLS handshake
  is *not* fallen back from (`VIA` = `rejected`). A listen mode tunnel serves
  both on the same port: QUIC long header packets cannot be confused with IP
  packets; plain UDP peers are served only while no QUIC peer is connected
  (the first QUIC peer drops them, so nothing is sent in clear next to
  encrypted peers), and on a TUN device plain datagrams are only taken if
  they look like IPv4/IPv6. Serve a mix of old plain-UDP-only and QUIC peers
  with two tunnels (one `udp`, one `quic`). This is opportunistic:
  someone who can drop QUIC packets on the path can force plain UDP. Use
  `quic` where that matters. Peers that only do plain UDP should use `udp`
  explicitly; `auto` sends them a few QUIC packets now and then, which a TUN
  device rejects but a TAP bridge would see as junk frames.
* **Identity**: `fwd_tunnel_tls(ctx, cert, key, ca)`; `fwdd` uses its `-c/-k`
  certificate for this (the fingerprint it prints) and `-T ca.pem` for `ca`.
  Without a certificate the library makes a throwaway self-signed one at
  start: the tunnel is encrypted, but nobody can pin it.
* **Authentication**: with a `pin` (the *peer's* certificate fingerprint) the
  peer must present exactly that certificate – the listening end then accepts
  only clients holding it (all its peers share that one certificate), which
  also keeps strangers out of listen mode. Without a pin but with a `ca` file, every peer needs a certificate
  that chains to it (EKU must allow server and client use; `fwdd -g` creates
  such certificates). With neither, QUIC encrypts but anybody can be the peer.

```
# A: listen, peers must hold B's certificate
fwdctl tun-add tun tun0 0.0.0.0 4000 0 0 10.77.0.1 24 0 listen quic <B's sha256>
# B: connect to A, A must hold the pinned certificate
fwdctl -s <B> tun-add tun tun0 0.0.0.0 4000 <A> 4000 10.77.0.2 24 0 connect quic <A's sha256>
```

## Behaviour notes

* **Single threaded.** Everything runs inside `fwd_run()`. Application should
  ignore `SIGPIPE`.
* **Port forwards** are user space proxies. TCP connections get a non-blocking
  upstream connect, buffering and proper half-close propagation. UDP keeps one
  upstream socket per client address and expires idle sessions
  (`udp_timeout_ms`). IPv6 listeners are `IPV6_V6ONLY`; create one forward per
  family. Remote hosts are IP addresses (no DNS resolution in the library).
  `fwd-mod` keeps existing TCP connections if only the listen endpoint changes.
* **Tunnels** carry the packets/frames of a TUN/TAP device to a peer over one
  UDP port (`laddr:lport`), over QUIC or as plain UDP datagrams (see
  [Tunnel transport](#tunnel-transport)).
  * *connect mode* (default): the peer is `raddr:rport`, fixed (the socket is
    `connect()`ed, other sources are dropped).
  * *listen mode*: the tunnel serves any number of peers on `laddr:lport`
    at once (up to `FWD_TUN_MAX_PEERS` = 64), each running in connect mode
    towards it. Every source address (plain UDP) or QUIC connection that
    passes the filter `raddr:rport` (zero = any) is a peer. Until one shows
    up, packets from the interface are dropped.
    * *Which peer gets a packet*: the tunnel learns the inner source address
      of what each peer sends (TUN: IPv4/IPv6 source, TAP: source MAC; up to
      `FWD_TUN_MAX_ROUTES` = 256, the latest sender of an address owns it)
      and sends interface packets to the owner of their destination.
      Broadcast, multicast and unknown destinations go to all peers, like a
      switch; with one peer everything goes to it. So route the peers'
      networks to the tunnel interface and have each peer send first (any
      traffic will do).
    * *Coming and going*: a peer that changes address or port (NAT, restart)
      becomes a new peer and takes its inner addresses along with its first
      packet. Plain UDP peers are forgotten after 2 min of silence (except
      the last one), QUIC peers when their connection ends (15 s idle at the
      latest). A full table drops the peer that has been silent longest.
    * *Trust*: peers are not isolated from each other – any peer can claim
      another's inner address by sending from it, and gets flooded traffic.
      Without authentication (see below) anyone who can reach the port and
      pass the filter can become a peer; set `raddr` (and `rport`), or better
      a pin.
  * `list-tun` shows the mode, the peer heard from last (`-` if none yet),
    the transport, what is in use (`VIA`) and the number of peers;
    `list-tun-peers <id>` lists the peers with their counters. `tun-mod` can
    switch modes and transports; it drops all peers when the mode or local
    endpoint changes, and those that no longer pass a changed filter. Optional interface address
    (netlink), MTU and link-up are applied on creation. The interface exists
    as long as the tunnel does.
* **Routes** use `RTM_NEWROUTE/DELROUTE` (`route-mod` = replace of the route with
  the same dst/prefix/table/metric, error if absent) and `RTM_GETROUTE` for
  listing. Adding/changing a route or giving a tunnel an address turns on the
  matching forwarding sysctl first if it is not on (`auto_sysctl`).
* **sysctl** access is restricted to keys below `net.ipv4.` and `net.ipv6.`
  through `/proc/sys`. Interface names containing a dot are not addressable.
* Needs root / `CAP_NET_ADMIN` for routes, tunnels and sysctl writes.

## Security

The control channel can change routes and kernel parameters. Bind it to
loopback or a management network. If `fwd_config.ctl_token` is set, every
command must be `"<token>@<command>"`. Over QUIC the token travels encrypted
to an authenticated server, which makes it a real credential (clients are not
authenticated by certificate, so always set a token). On plain UDP the token is
a shared secret in clear text (no encryption, no replay protection) and only
guards against casual access – prefer QUIC and run `fwdd -U` where possible.

## Porting and disabling functions

All system interaction goes through `fwd_sys_*()` in `include/fwd_sys.h`, the
only file calling libc/OS functions is `src/fwd_sys.c`.

* **Compile time:** `-DFWD_SYS_ACCEPT=0` (also `SOCKET BIND LISTEN CONNECT SHUTDOWN
  SEND RECV SENDTO RECVFROM SETSOCKOPT GETSOCKOPT CLOSE READ WRITE OPEN IOCTL
  POLL NONBLOCK CLOCK`) leaves the call out of the binary; the wrapper fails
  with `ENOSYS`, which surfaces as `FWD_E_NOSYS` (error code 3).
* **Feature level:** `-DFWD_ENABLE_TUNTAP=0 -DFWD_ENABLE_NETLINK=0
  -DFWD_ENABLE_SYSCTL=0 -DFWD_ENABLE_PORTFWD=0 -DFWD_ENABLE_CTL=0
  -DFWD_ENABLE_IPV6=0 ...` compile whole modules (and their Linux-only headers)
  out; their API returns `FWD_E_NOSYS`. Example: `make DEFS="-DFWD_ENABLE_TUNTAP=0
  -DFWD_ENABLE_NETLINK=0"` for a BSD-style port with just forwarding + control.
* **Run time:** `fwd_sys_set_ops()` installs a table of replacement functions
  (an RTOS socket layer, a test double, a sandbox). A `NULL` member disables
  that call.
* Memory goes through `FWD_MALLOC/CALLOC/REALLOC/FREE` (`fwd_config.h`).
* **QUIC:** datagrams still go through `fwd_sys_sendto/recvfrom` (OpenSSL gets
  a custom BIO; tunnels read their socket themselves and hand QUIC datagrams to
  OpenSSL with `SSL_inject_net_dgram()`). OpenSSL itself reads the certificate/key files and
  allocates its own memory; `fwd_tls_make_selfsigned()` writes its files with
  `open()` directly (it needs a file mode). `FWD_ENABLE_QUIC` is the only
  switch that defaults to 0 in `fwd_config.h`.

## Layout

```
include/fwd.h          public API, command table, error codes
include/fwd_sys.h      syscall wrapper + ops table
include/fwd_config.h   compile time switches
src/fwd_proto.c        address + command codec (table driven), response parser
src/fwd_core.c         context, event loop, errors, sysctl
src/fwd_forward.c      TCP/UDP forwarding
src/fwd_tun.c          TUN/TAP tunnels, plain UDP transport
src/fwd_tunq.c         QUIC transport of tunnels
src/fwd_netlink.c      routes + interface addresses
src/fwd_exec.c         command execution / responses / listing
src/fwd_ctl.c          UDP control server + client helper
src/fwd_quic.c         QUIC control server + client
src/fwd_tls.c          OpenSSL helpers: BIO, fingerprints, certificates
src/fwd_sys.c          default syscall implementation
examples/              fwdd (daemon), fwdctl (client)
tests/test_fwd.c       codec, forwarding, tunnels over UDP and QUIC (fake
                       /dev/net/tun via fwd_sys_set_ops), control channel
                       (UDP + QUIC) tests
```
