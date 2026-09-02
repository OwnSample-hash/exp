#pragma once

#if !(defined(EXPLO_LIB) || defined(EXPLO_IMPL))
#error "This file shouldn't be included directly. Include 'lib.hpp' instead."
#endif

#ifdef EXPLO_IMPL
#define EXPLO_LIB_API
#elifdef EXPLO_LIB
#define EXPLO_LIB_API extern
#else
#define EXPLO_LIB_API
#error "EXPLO_LIB or EXPLO_IMPL must be defined before including this file."
#endif

#include <arpa/inet.h>
#include <chrono>
#include <enums.hpp>
#include <functional>
#include <unordered_map>

namespace explo::lib {

using socket_t = int;

namespace impl {

EXPLO_LIB_API void sleep(std::chrono::milliseconds dur);

EXPLO_LIB_API socket_t socket(int domain, int type, int protocol, int nonblock = SOCK_NONBLOCK) noexcept;

EXPLO_LIB_API int connect(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen) noexcept;

EXPLO_LIB_API int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                         struct timeval *timeout) noexcept;

EXPLO_LIB_API int getsockopt(socket_t sockfd, int level, int optname, void *optval, socklen_t *optlen) noexcept;

EXPLO_LIB_API int write(socket_t sockfd, const void *buf, size_t len) noexcept;

EXPLO_LIB_API int read(socket_t sockfd, void *buf, size_t len) noexcept;

EXPLO_LIB_API int close(socket_t sockfd) noexcept;

EXPLO_LIB_API uint64_t clock() noexcept;

EXPLO_LIB_API std::unordered_map<std::string, ConnectionStatus::Type>
async_scan(int max, std::function<std::tuple<std::string, int, int, int>(void)> generator);

} // namespace impl
} // namespace explo::lib
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
