#define EXPLO_IMPL

#include <arpa/inet.h>
#include <chrono>
#include <lib.hpp>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace explo::lib {

using socket_t = int;

namespace impl {

void sleep(std::chrono::milliseconds dur) { std::this_thread::sleep_for(dur); }

socket_t socket(int domain, int type, int protocol, int nonblock) noexcept {
  return ::socket(domain, type | nonblock, protocol);
}

int connect(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen) noexcept {
  return ::connect(sockfd, addr, addrlen);
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) noexcept {
  return ::select(nfds, readfds, writefds, exceptfds, timeout);
}

int getsockopt(socket_t sockfd, int level, int optname, void *optval, socklen_t *optlen) noexcept {
  return ::getsockopt(sockfd, level, optname, optval, optlen);
}

int write(socket_t sockfd, const void *buf, size_t len) noexcept { return ::write(sockfd, buf, len); }

int read(socket_t sockfd, void *buf, size_t len) noexcept { return ::read(sockfd, buf, len); }

int close(socket_t sockfd) noexcept { return ::close(sockfd); }

uint64_t clock() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

int getaddrinfo(const char *node, const char *service, const struct ::addrinfo *hints,
                struct ::addrinfo **res) noexcept {
  return ::getaddrinfo(node, service, hints, res);
}

void freeaddrinfo(struct ::addrinfo *res) noexcept { ::freeaddrinfo(res); }

} // namespace impl
} // namespace explo::lib
