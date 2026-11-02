/*
 * fwd_config.h - compile time configuration of libfwd.
 *
 * The switch values live in config.yaml and are emitted, by the project's
 * configuration system, into the generated and git-ignored fwd_config.hpp as
 * CONFIG_FWD_* defines. This header maps those onto the library's documented
 * FWD_* macro names. Each macro stays overridable from the compiler command
 * line (e.g. -DFWD_ENABLE_TUNTAP=0 -DFWD_SYS_ACCEPT=0): a -D wins because the
 * mapping is guarded by #ifndef.
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
 *
 * Edit config.yaml to change defaults; never edit the generated header.
 */
#ifndef FWD_CONFIG_H
#define FWD_CONFIG_H

#include "fwd_config.hpp" /* generated from config.yaml: CONFIG_FWD_* */

/* ---- version ------------------------------------------------------- */
#ifndef FWD_MAJOR
#define FWD_MAJOR CONFIG_FWD_MAJOR
#endif
#ifndef FWD_MINOR
#define FWD_MINOR CONFIG_FWD_MINOR
#endif
#ifndef FWD_PATCH
#define FWD_PATCH CONFIG_FWD_PATCH
#endif

/* ---- high level features ------------------------------------------- */
#ifndef FWD_ENABLE_IPV4
#define FWD_ENABLE_IPV4 CONFIG_FWD_ENABLE_IPV4
#endif
#ifndef FWD_ENABLE_IPV6
#define FWD_ENABLE_IPV6 CONFIG_FWD_ENABLE_IPV6
#endif
#ifndef FWD_ENABLE_PORTFWD
#define FWD_ENABLE_PORTFWD CONFIG_FWD_ENABLE_PORTFWD
#endif
#ifndef FWD_ENABLE_TCP_FWD
#define FWD_ENABLE_TCP_FWD CONFIG_FWD_ENABLE_TCP_FWD
#endif
#ifndef FWD_ENABLE_UDP_FWD
#define FWD_ENABLE_UDP_FWD CONFIG_FWD_ENABLE_UDP_FWD
#endif
#ifndef FWD_ENABLE_TUNTAP
#define FWD_ENABLE_TUNTAP CONFIG_FWD_ENABLE_TUNTAP
#endif
#ifndef FWD_ENABLE_NETLINK
#define FWD_ENABLE_NETLINK CONFIG_FWD_ENABLE_NETLINK
#endif
#ifndef FWD_ENABLE_SYSCTL
#define FWD_ENABLE_SYSCTL CONFIG_FWD_ENABLE_SYSCTL
#endif
#ifndef FWD_ENABLE_CTL
#define FWD_ENABLE_CTL CONFIG_FWD_ENABLE_CTL
#endif
#ifndef FWD_ENABLE_QUIC
#define FWD_ENABLE_QUIC CONFIG_FWD_ENABLE_QUIC
#endif

/* ---- individual system calls (see fwd_sys.h) ----------------------- */
#ifndef FWD_SYS_SOCKET
#define FWD_SYS_SOCKET CONFIG_FWD_SYS_SOCKET
#endif
#ifndef FWD_SYS_BIND
#define FWD_SYS_BIND CONFIG_FWD_SYS_BIND
#endif
#ifndef FWD_SYS_LISTEN
#define FWD_SYS_LISTEN CONFIG_FWD_SYS_LISTEN
#endif
#ifndef FWD_SYS_ACCEPT
#define FWD_SYS_ACCEPT CONFIG_FWD_SYS_ACCEPT
#endif
#ifndef FWD_SYS_CONNECT
#define FWD_SYS_CONNECT CONFIG_FWD_SYS_CONNECT
#endif
#ifndef FWD_SYS_SHUTDOWN
#define FWD_SYS_SHUTDOWN CONFIG_FWD_SYS_SHUTDOWN
#endif
#ifndef FWD_SYS_SEND
#define FWD_SYS_SEND CONFIG_FWD_SYS_SEND
#endif
#ifndef FWD_SYS_RECV
#define FWD_SYS_RECV CONFIG_FWD_SYS_RECV
#endif
#ifndef FWD_SYS_SENDTO
#define FWD_SYS_SENDTO CONFIG_FWD_SYS_SENDTO
#endif
#ifndef FWD_SYS_RECVFROM
#define FWD_SYS_RECVFROM CONFIG_FWD_SYS_RECVFROM
#endif
#ifndef FWD_SYS_SETSOCKOPT
#define FWD_SYS_SETSOCKOPT CONFIG_FWD_SYS_SETSOCKOPT
#endif
#ifndef FWD_SYS_GETSOCKOPT
#define FWD_SYS_GETSOCKOPT CONFIG_FWD_SYS_GETSOCKOPT
#endif
#ifndef FWD_SYS_CLOSE
#define FWD_SYS_CLOSE CONFIG_FWD_SYS_CLOSE
#endif
#ifndef FWD_SYS_READ
#define FWD_SYS_READ CONFIG_FWD_SYS_READ
#endif
#ifndef FWD_SYS_WRITE
#define FWD_SYS_WRITE CONFIG_FWD_SYS_WRITE
#endif
#ifndef FWD_SYS_OPEN
#define FWD_SYS_OPEN CONFIG_FWD_SYS_OPEN
#endif
#ifndef FWD_SYS_IOCTL
#define FWD_SYS_IOCTL CONFIG_FWD_SYS_IOCTL
#endif
#ifndef FWD_SYS_POLL
#define FWD_SYS_POLL CONFIG_FWD_SYS_POLL
#endif
#ifndef FWD_SYS_NONBLOCK
#define FWD_SYS_NONBLOCK CONFIG_FWD_SYS_NONBLOCK
#endif
#ifndef FWD_SYS_CLOCK
#define FWD_SYS_CLOCK CONFIG_FWD_SYS_CLOCK
#endif

/* ---- memory allocation --------------------------------------------- */
/* Function-like hooks rather than plain values, so they are configured here
 * directly rather than through config.yaml. */
#ifndef FWD_MALLOC
#include <stdlib.h>
#define FWD_MALLOC(n) malloc(n)
#define FWD_CALLOC(n, m) calloc((n), (m))
#define FWD_REALLOC(p, n) realloc((p), (n))
#define FWD_FREE(p) free(p)
#endif

#endif /* FWD_CONFIG_H */
