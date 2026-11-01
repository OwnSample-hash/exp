/*
 * fwd_sys.c - default (POSIX/Linux) implementation of the syscall wrapper.
 * This is the only file that calls libc networking/IO functions directly.
 */
#include "fwd_sys.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int def_ioctl(int fd, unsigned long req, void *arg)
{
    return ioctl(fd, req, arg);
}

static int def_open(const char *path, int flags)
{
    return open(path, flags);
}

static int def_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int64_t def_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void fwd_sys_ops_default(fwd_sys_ops *o)
{
    memset(o, 0, sizeof *o);
#if FWD_SYS_SOCKET
    o->socket = socket;
#endif
#if FWD_SYS_BIND
    o->bind = bind;
#endif
#if FWD_SYS_LISTEN
    o->listen = listen;
#endif
#if FWD_SYS_ACCEPT
    o->accept = accept;
#endif
#if FWD_SYS_CONNECT
    o->connect = connect;
#endif
#if FWD_SYS_SHUTDOWN
    o->shutdown = shutdown;
#endif
#if FWD_SYS_SEND
    o->send = send;
#endif
#if FWD_SYS_RECV
    o->recv = recv;
#endif
#if FWD_SYS_SENDTO
    o->sendto = sendto;
#endif
#if FWD_SYS_RECVFROM
    o->recvfrom = recvfrom;
#endif
#if FWD_SYS_SETSOCKOPT
    o->setsockopt = setsockopt;
#endif
#if FWD_SYS_GETSOCKOPT
    o->getsockopt = getsockopt;
#endif
#if FWD_SYS_CLOSE
    o->close = close;
#endif
#if FWD_SYS_READ
    o->read = read;
#endif
#if FWD_SYS_WRITE
    o->write = write;
#endif
#if FWD_SYS_OPEN
    o->open = def_open;
#endif
#if FWD_SYS_IOCTL
    o->ioctl = def_ioctl;
#endif
#if FWD_SYS_POLL
    o->poll = poll;
#endif
#if FWD_SYS_NONBLOCK
    o->set_nonblock = def_nonblock;
#endif
#if FWD_SYS_CLOCK
    o->now_ms = def_now_ms;
#endif
}

static fwd_sys_ops g_ops;
static int g_init;

static const fwd_sys_ops *ops(void)
{
    if (!g_init) {
        fwd_sys_ops_default(&g_ops);
        g_init = 1;
    }
    return &g_ops;
}

void fwd_sys_set_ops(const fwd_sys_ops *o)
{
    if (o) {
        g_ops = *o;
        g_init = 1;
    } else {
        g_init = 0;
    }
}

/* Dispatch through the table; a NULL member or a compile time disabled call
 * yields -1 / ENOSYS. */
#define CALL(member, enabled, ...)                                            \
    (((enabled) && ops()->member) ? ops()->member(__VA_ARGS__)                \
                                  : (errno = ENOSYS, -1))

int fwd_sys_socket(int d, int t, int p)
{
    return CALL(socket, FWD_SYS_SOCKET, d, t, p);
}
int fwd_sys_bind(int fd, const struct sockaddr *sa, socklen_t l)
{
    return CALL(bind, FWD_SYS_BIND, fd, sa, l);
}
int fwd_sys_listen(int fd, int backlog)
{
    return CALL(listen, FWD_SYS_LISTEN, fd, backlog);
}
int fwd_sys_accept(int fd, struct sockaddr *sa, socklen_t *l)
{
    return CALL(accept, FWD_SYS_ACCEPT, fd, sa, l);
}
int fwd_sys_connect(int fd, const struct sockaddr *sa, socklen_t l)
{
    return CALL(connect, FWD_SYS_CONNECT, fd, sa, l);
}
int fwd_sys_shutdown(int fd, int how)
{
    return CALL(shutdown, FWD_SYS_SHUTDOWN, fd, how);
}
ssize_t fwd_sys_send(int fd, const void *b, size_t n, int fl)
{
    return CALL(send, FWD_SYS_SEND, fd, b, n, fl);
}
ssize_t fwd_sys_recv(int fd, void *b, size_t n, int fl)
{
    return CALL(recv, FWD_SYS_RECV, fd, b, n, fl);
}
ssize_t fwd_sys_sendto(int fd, const void *b, size_t n, int fl,
                       const struct sockaddr *sa, socklen_t sl)
{
    return CALL(sendto, FWD_SYS_SENDTO, fd, b, n, fl, sa, sl);
}
ssize_t fwd_sys_recvfrom(int fd, void *b, size_t n, int fl,
                         struct sockaddr *sa, socklen_t *sl)
{
    return CALL(recvfrom, FWD_SYS_RECVFROM, fd, b, n, fl, sa, sl);
}
int fwd_sys_setsockopt(int fd, int lvl, int opt, const void *v, socklen_t l)
{
    return CALL(setsockopt, FWD_SYS_SETSOCKOPT, fd, lvl, opt, v, l);
}
int fwd_sys_getsockopt(int fd, int lvl, int opt, void *v, socklen_t *l)
{
    return CALL(getsockopt, FWD_SYS_GETSOCKOPT, fd, lvl, opt, v, l);
}
int fwd_sys_close(int fd)
{
    return CALL(close, FWD_SYS_CLOSE, fd);
}
ssize_t fwd_sys_read(int fd, void *b, size_t n)
{
    return CALL(read, FWD_SYS_READ, fd, b, n);
}
ssize_t fwd_sys_write(int fd, const void *b, size_t n)
{
    return CALL(write, FWD_SYS_WRITE, fd, b, n);
}
int fwd_sys_open(const char *path, int flags)
{
    return CALL(open, FWD_SYS_OPEN, path, flags);
}
int fwd_sys_ioctl(int fd, unsigned long req, void *arg)
{
    return CALL(ioctl, FWD_SYS_IOCTL, fd, req, arg);
}
int fwd_sys_poll(struct pollfd *fds, nfds_t n, int timeout_ms)
{
    return CALL(poll, FWD_SYS_POLL, fds, n, timeout_ms);
}
int fwd_sys_set_nonblock(int fd)
{
    return CALL(set_nonblock, FWD_SYS_NONBLOCK, fd);
}

int64_t fwd_sys_now_ms(void)
{
    if (!FWD_SYS_CLOCK || !ops()->now_ms)
        return -1;
    return ops()->now_ms();
}
