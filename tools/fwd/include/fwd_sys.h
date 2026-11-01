/*
 * fwd_sys.h - syscall wrapper layer.
 *
 * The library never calls socket(), accept(), ioctl() ... directly. All
 * system interaction goes through the fwd_sys_*() functions below, which
 * dispatch through a table of function pointers (fwd_sys_ops).
 *
 * Porting / disabling:
 *   - compile time:  -DFWD_SYS_ACCEPT=0 (see fwd_config.h). The call is not
 *                    linked, the wrapper returns -1 / ENOSYS.
 *   - run time:      fwd_sys_set_ops() installs a table with replacement
 *                    implementations. A NULL member disables that call
 *                    (-1 / ENOSYS).
 *
 * The ops table is process global and not thread safe to modify; install it
 * before creating any fwd_ctx.
 */
#ifndef FWD_SYS_H
#define FWD_SYS_H

#include "fwd_config.h"

#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fwd_sys_ops {
    int (*socket)(int domain, int type, int protocol);
    int (*bind)(int fd, const struct sockaddr *sa, socklen_t len);
    int (*listen)(int fd, int backlog);
    int (*accept)(int fd, struct sockaddr *sa, socklen_t *len);
    int (*connect)(int fd, const struct sockaddr *sa, socklen_t len);
    int (*shutdown)(int fd, int how);
    ssize_t (*send)(int fd, const void *buf, size_t len, int flags);
    ssize_t (*recv)(int fd, void *buf, size_t len, int flags);
    ssize_t (*sendto)(int fd, const void *buf, size_t len, int flags,
                      const struct sockaddr *sa, socklen_t slen);
    ssize_t (*recvfrom)(int fd, void *buf, size_t len, int flags,
                        struct sockaddr *sa, socklen_t *slen);
    int (*setsockopt)(int fd, int level, int opt, const void *val,
                      socklen_t len);
    int (*getsockopt)(int fd, int level, int opt, void *val, socklen_t *len);
    int (*close)(int fd);
    ssize_t (*read)(int fd, void *buf, size_t len);
    ssize_t (*write)(int fd, const void *buf, size_t len);
    int (*open)(const char *path, int flags);
    int (*ioctl)(int fd, unsigned long req, void *arg);
    int (*poll)(struct pollfd *fds, nfds_t nfds, int timeout_ms);
    int (*set_nonblock)(int fd);   /* put fd into non blocking mode */
    int64_t (*now_ms)(void);       /* monotonic milliseconds        */
} fwd_sys_ops;

/* Fill *ops with the default (libc / POSIX) implementations. Calls that were
 * compiled out with FWD_SYS_xxx=0 are left NULL. */
void fwd_sys_ops_default(fwd_sys_ops *ops);

/* Install a replacement table (copied). NULL restores the defaults. */
void fwd_sys_set_ops(const fwd_sys_ops *ops);

/* The wrappers. Same semantics as the POSIX call they are named after:
 * -1 and errno on failure, errno == ENOSYS if the call is disabled. */
int fwd_sys_socket(int domain, int type, int protocol);
int fwd_sys_bind(int fd, const struct sockaddr *sa, socklen_t len);
int fwd_sys_listen(int fd, int backlog);
int fwd_sys_accept(int fd, struct sockaddr *sa, socklen_t *len);
int fwd_sys_connect(int fd, const struct sockaddr *sa, socklen_t len);
int fwd_sys_shutdown(int fd, int how);
ssize_t fwd_sys_send(int fd, const void *buf, size_t len, int flags);
ssize_t fwd_sys_recv(int fd, void *buf, size_t len, int flags);
ssize_t fwd_sys_sendto(int fd, const void *buf, size_t len, int flags,
                       const struct sockaddr *sa, socklen_t slen);
ssize_t fwd_sys_recvfrom(int fd, void *buf, size_t len, int flags,
                         struct sockaddr *sa, socklen_t *slen);
int fwd_sys_setsockopt(int fd, int level, int opt, const void *val,
                       socklen_t len);
int fwd_sys_getsockopt(int fd, int level, int opt, void *val, socklen_t *len);
int fwd_sys_close(int fd);
ssize_t fwd_sys_read(int fd, void *buf, size_t len);
ssize_t fwd_sys_write(int fd, const void *buf, size_t len);
int fwd_sys_open(const char *path, int flags);
int fwd_sys_ioctl(int fd, unsigned long req, void *arg);
int fwd_sys_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);
int fwd_sys_set_nonblock(int fd);
int64_t fwd_sys_now_ms(void); /* -1 if no clock is available */

#ifdef __cplusplus
}
#endif
#endif /* FWD_SYS_H */
