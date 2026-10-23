#define EXPLO_IMPL

#include <arpa/inet.h>
#include <chrono>
#include <lib.hpp>
#include <netdb.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace explo::lib {

using socket_t = int;
using sockaddr_t = struct sockaddr;
using addrinfo_t = struct ::addrinfo;
using socketlen_t = socklen_t;

namespace impl {

void sleep(std::chrono::milliseconds dur) { std::this_thread::sleep_for(dur); }

socket_t socket(int domain, int type, int protocol, int nonblock) noexcept {
  return ::socket(domain, type | nonblock, protocol);
}

int connect(socket_t sockfd, const sockaddr_t *addr, socketlen_t addrlen) noexcept {
  return ::connect(sockfd, addr, addrlen);
}

int bind(socket_t sockfd, const sockaddr_t *addr, socketlen_t addrlen) noexcept {
  return ::bind(sockfd, addr, addrlen);
}

int accept(socket_t sockfd, sockaddr_t *addr, socketlen_t *addrlen) noexcept { return ::accept(sockfd, addr, addrlen); }

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) noexcept {
  return ::select(nfds, readfds, writefds, exceptfds, timeout);
}

int getsockopt(socket_t sockfd, int level, int optname, void *optval, socketlen_t *optlen) noexcept {
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

int getaddrinfo(const char *node, const char *service, const addrinfo_t *hints, addrinfo_t **res) noexcept {
  return ::getaddrinfo(node, service, hints, res);
}

void freeaddrinfo(addrinfo_t *res) noexcept { ::freeaddrinfo(res); }

int getnameinfo(const sockaddr_t *sa, socketlen_t salen, char *host, socketlen_t hostlen, char *serv,
                socketlen_t servlen, int flags) noexcept {
  return ::getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}

int getpeername(socket_t sockfd, sockaddr_t *addr, socketlen_t *addrlen) noexcept {
  return ::getpeername(sockfd, addr, addrlen);
}

std::vector<fs::directory_entry> listDir(const std::string_view path) {
  std::vector<fs::directory_entry> files;
  for (const auto &entry : std::filesystem::directory_iterator(path)) {
    files.push_back(entry);
  }
  return files;
}

int read_block(int fd, char *buf, size_t count) noexcept {
  struct pollfd pfd[1] = {};
  pfd[0].fd = fd;
  pfd[0].events = POLLIN;
  if (::poll(pfd, 1, -1) > 0) {
    return ::read(fd, buf, count);
  }
  return -1;
}

} // namespace impl
} // namespace explo::lib
