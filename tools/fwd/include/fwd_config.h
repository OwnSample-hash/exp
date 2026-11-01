/*
 * fwd_config.h - compile time configuration of libfwd.
 *
 * Every switch defaults to 1 (enabled), except FWD_ENABLE_QUIC. Override on the
 * compiler command line, e.g.  -DFWD_ENABLE_TUNTAP=0 -DFWD_SYS_ACCEPT=0
 *
 * Two layers of switches exist:
 *
 *  1. FWD_ENABLE_*  - high level features. A disabled feature is compiled
 *     out and its API returns FWD_E_NOSYS.
 *
 *  2. FWD_SYS_*     - individual system calls used through the syscall
 *     wrapper (fwd_sys.h). A disabled call is not linked at all and its
 *     wrapper fails with errno = ENOSYS, which the library reports as
 *     FWD_E_NOSYS. This is how the library is ported: switch off what the
 *     target platform does not have, or replace the implementations at run
 *     time with fwd_sys_set_ops().
 */
#ifndef FWD_CONFIG_H
#define FWD_CONFIG_H

#ifndef FWD_MAJOR
#define FWD_MAJOR 1
#endif
#ifndef FWD_MINOR
#define FWD_MINOR 0
#endif
#ifndef FWD_PATCH
#define FWD_PATCH 3
#endif

/* ---- high level features ------------------------------------------- */
#ifndef FWD_ENABLE_IPV4
#define FWD_ENABLE_IPV4 1
#endif
#ifndef FWD_ENABLE_IPV6
#define FWD_ENABLE_IPV6 1
#endif
#ifndef FWD_ENABLE_PORTFWD
#define FWD_ENABLE_PORTFWD 1 /* user space port forwarding (TCP + UDP)   */
#endif
#ifndef FWD_ENABLE_TCP_FWD
#define FWD_ENABLE_TCP_FWD 1
#endif
#ifndef FWD_ENABLE_UDP_FWD
#define FWD_ENABLE_UDP_FWD 1
#endif
#ifndef FWD_ENABLE_TUNTAP
#define FWD_ENABLE_TUNTAP 1 /* Linux TUN/TAP tunnels                    */
#endif
#ifndef FWD_ENABLE_NETLINK
#define FWD_ENABLE_NETLINK 1 /* Linux rtnetlink routes / addresses       */
#endif
#ifndef FWD_ENABLE_SYSCTL
#define FWD_ENABLE_SYSCTL 1 /* /proc/sys access                         */
#endif
#ifndef FWD_ENABLE_CTL
#define FWD_ENABLE_CTL 1 /* UDP control server + client helper       */
#endif
#ifndef FWD_ENABLE_QUIC
#define FWD_ENABLE_QUIC                                                        \
  0 /* QUIC (TLS 1.3) control channel and tunnels.                            \
       The only switch that defaults to 0: it needs OpenSSL                    \
       >= 3.5 (-lssl -lcrypto). The Makefile turns it                          \
       on when pkg-config finds one.               */
#endif

/* ---- individual system calls (see fwd_sys.h) ----------------------- */
#ifndef FWD_SYS_SOCKET
#define FWD_SYS_SOCKET 1
#endif
#ifndef FWD_SYS_BIND
#define FWD_SYS_BIND 1
#endif
#ifndef FWD_SYS_LISTEN
#define FWD_SYS_LISTEN 1
#endif
#ifndef FWD_SYS_ACCEPT
#define FWD_SYS_ACCEPT 1
#endif
#ifndef FWD_SYS_CONNECT
#define FWD_SYS_CONNECT 1
#endif
#ifndef FWD_SYS_SHUTDOWN
#define FWD_SYS_SHUTDOWN 1
#endif
#ifndef FWD_SYS_SEND
#define FWD_SYS_SEND 1
#endif
#ifndef FWD_SYS_RECV
#define FWD_SYS_RECV 1
#endif
#ifndef FWD_SYS_SENDTO
#define FWD_SYS_SENDTO 1
#endif
#ifndef FWD_SYS_RECVFROM
#define FWD_SYS_RECVFROM 1
#endif
#ifndef FWD_SYS_SETSOCKOPT
#define FWD_SYS_SETSOCKOPT 1
#endif
#ifndef FWD_SYS_GETSOCKOPT
#define FWD_SYS_GETSOCKOPT 1
#endif
#ifndef FWD_SYS_CLOSE
#define FWD_SYS_CLOSE 1
#endif
#ifndef FWD_SYS_READ
#define FWD_SYS_READ 1
#endif
#ifndef FWD_SYS_WRITE
#define FWD_SYS_WRITE 1
#endif
#ifndef FWD_SYS_OPEN
#define FWD_SYS_OPEN 1
#endif
#ifndef FWD_SYS_IOCTL
#define FWD_SYS_IOCTL 1
#endif
#ifndef FWD_SYS_POLL
#define FWD_SYS_POLL 1
#endif
#ifndef FWD_SYS_NONBLOCK
#define FWD_SYS_NONBLOCK 1
#endif
#ifndef FWD_SYS_CLOCK
#define FWD_SYS_CLOCK 1
#endif

/* ---- memory allocation --------------------------------------------- */
#ifndef FWD_MALLOC
#include <stdlib.h>
#define FWD_MALLOC(n) malloc(n)
#define FWD_CALLOC(n, m) calloc((n), (m))
#define FWD_REALLOC(p, n) realloc((p), (n))
#define FWD_FREE(p) free(p)
#endif

#endif /* FWD_CONFIG_H */
