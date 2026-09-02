#pragma once

#include <tuple>
#define EXPLO_LIB

#ifdef __linux__
#include <lib/linux/lib.hpp>
#endif

namespace explo {
namespace lib {

inline void sleep(std::chrono::milliseconds dur) { impl::sleep(dur); }

inline socket_t socket(int domain, int type, int protocol, int nonblock = SOCK_NONBLOCK) noexcept {
  return impl::socket(domain, type, protocol, nonblock);
}

inline int connect(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen) noexcept {
  return impl::connect(sockfd, addr, addrlen);
}

inline int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) noexcept {
  return impl::select(nfds, readfds, writefds, exceptfds, timeout);
}

inline int getsockopt(socket_t sockfd, int level, int optname, void *optval, socklen_t *optlen) noexcept {
  return impl::getsockopt(sockfd, level, optname, optval, optlen);
}

inline int write(socket_t sockfd, const void *buf, size_t len) noexcept { return impl::write(sockfd, buf, len); }

inline int read(socket_t sockfd, void *buf, size_t len) noexcept { return impl::read(sockfd, buf, len); }

inline int close(socket_t sockfd) noexcept { return impl::close(sockfd); }

inline uint64_t clock() noexcept { return impl::clock(); }

inline std::unordered_map<std::string, ConnectionStatus::Type>
async_scan(int max, std::function<std::tuple<std::string, int, int, int>(void)> generator) {
  return impl::async_scan(max, generator);
}

inline int getaddrinfo(const char *node, const char *service, const struct ::addrinfo *hints,
                       struct ::addrinfo **res) noexcept {
  return impl::getaddrinfo(node, service, hints, res);
}

inline void freeaddrinfo(struct ::addrinfo *res) noexcept { impl::freeaddrinfo(res); }

} // namespace lib
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
