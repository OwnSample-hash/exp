#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <config.hpp>
#include <ctime>
#include <enums.hpp>
#include <fcntl.h>
#include <future>
#include <inplace_vector>
#include <lib.hpp>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <tuple>
#include <unistd.h>

namespace explo::lib::impl {
static auto &getLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  if (!logger)
    logger = spdlog::get("lib")->clone("lib::async_scan");
  return logger;
}

constexpr std::string resultKeyTemp = "{}:{}://{}:{}";

const char *sock2a(int socktype) {
  switch (socktype) {
  case SOCK_STREAM:
    return "tcp";
  case SOCK_DGRAM:
    return "udp";
  case SOCK_RAW:
    return "raw";
  default:
    return "";
  }
}

const char *dom2a(int domain) {
  switch (domain) {
  case AF_INET:
    return "ipv4";
  case AF_INET6:
    return "ipv6";
  default:
    return "";
  }
}

unsigned int domain2size(int domain) {
  switch (domain) {
  case AF_INET:
    return sizeof(struct sockaddr_in);
  case AF_INET6:
    return sizeof(struct sockaddr_in6);
  default:
    return 0;
  }
}

std::string formatResultKey(std::string_view domain, std::string_view protocol, std::string_view host, int port) {
  return fmt::format(resultKeyTemp, domain, protocol, host, port);
}

std::string formatResultKey(int domain, int socktype, std::string_view host, int port) {
  return formatResultKey(dom2a(domain), sock2a(socktype), host, port);
}

std::atomic<bool> reservingFileDescriptors{false};

bool reserveFileDescriptors(unsigned long int req) {
  static unsigned long int reserved;
  struct rlimit rl = {};
  while (reservingFileDescriptors.exchange(true)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  auto &logger = getLogger();
  logger->trace("Reserving {} file descriptors (currently) reserved: {})", req, reserved);
  if (req & (1UL << (sizeof(req) * 8 - 1))) {
    reserved += req;
    return true;
  }
  if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
    logger->error("Failed to get file descriptor limit: {}", strerror(errno));
    return false;
  }
  if (rl.rlim_cur < req) [[unlikely]] {
    rl.rlim_cur = rl.rlim_max;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1) {
      logger->error("Failed to set file descriptor limit: {}", strerror(errno));
      return false;
    }
  }
  if (reserved + req > rl.rlim_cur) {
    logger->error("Not enough file descriptors available. Requested: {}, Reserved: {}, Available: {}", req, reserved,
                  rl.rlim_cur);
    return false;
  }
  reserved += req;
  reservingFileDescriptors = false;
  return true;
}

struct socketInfo {
  int fd;
  std::string host;
  int port;
  int domain;
  int type;
  int timeOutCount = 0;
};

std::unordered_map<std::string, ConnectionStatus::Type>
async_scan(int max, std::function<std::tuple<std::string, int, int, int>(void)> generator) noexcept(false) {
  if (!reserveFileDescriptors(CONFIG_LIB_SOCKET_LIMIT)) {
    throw std::runtime_error("Failed to reserve file descriptors for async_scan");
  }
  auto &logger = getLogger();
  std::unordered_map<std::string, ConnectionStatus::Type> connected;
  std::mutex connected_mutex;
  std::inplace_vector<socketInfo, CONFIG_LIB_SOCKET_LIMIT> fds;
  std::vector<std::future<void>> futures;
  int step = CONFIG_LIB_SOCKET_LIMIT;

  const struct timespec timeout{
      .tv_sec = CONFIG_LIB_TIMEOUT_SEC,
      .tv_nsec = CONFIG_LIB_TIMEOUT_NSEC,
  };
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    logger->error("Failed to create epoll instance: {}", strerror(errno));
    throw std::runtime_error(fmt::format("Failed to create epoll instance: {}", strerror(errno)));
  }
  logger->info("Starting async_scan with max={} and step={}", max, step);
  for (int i = 0; i < max; i += step) {
    for (int j = 0; j < step && (j + i) < max; j++) {
      auto [host, port, domain, type] = generator();
      if (host.empty() || port <= 0 || domain <= 0 || type <= 0) {
        logger->error("Invalid parameters for async_scan({}:{})): host='{}', port={}, domain={}, type={}", i, j, host,
                      port, domain, type);
        throw std::invalid_argument(
            fmt::format("Invalid parameters for async_scan({}:{}): host='{}', port={}, domain={}, type={}", i, j, host,
                        port, domain, type));
      }

      int fd = socket(domain, type, 0);
      if (fd < 0) {
        logger->error("Failed to create socket: {}", strerror(errno));
        throw std::runtime_error(fmt::format("Failed to create socket: {}", strerror(errno)));
      }
      int oflags = fcntl(fd, F_GETFL, 0);
      fcntl(fd, F_SETFL, O_NONBLOCK | oflags);

      struct sockaddr_storage base_addr;
      if (domain == AF_INET6) {
        auto *addr = (struct sockaddr_in6 *)&base_addr;
        addr->sin6_family = AF_INET6;
        addr->sin6_port = htons(port);
        if (inet_pton(AF_INET6, host.c_str(), &addr->sin6_addr) <= 0) {
          logger->error("Invalid IPv6 address: {}", host);
          close(fd);
          throw std::runtime_error(fmt::format("Invalid IPv6 address: {}", host));
        }
      } else {
        auto *adr = (struct sockaddr_in *)&base_addr;
        adr->sin_family = AF_INET;
        adr->sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &(adr->sin_addr)) <= 0) {
          logger->error("Invalid address: {}", host);
          close(fd);
          throw std::runtime_error(fmt::format("Invalid address: {}", host));
        }
      }

      int ret = explo::lib::connect(fd, (struct sockaddr *)&base_addr, domain2size(domain));
      if (ret < 0 && errno != EINPROGRESS) {
        logger->error("Failed to connect to {}:{}: {}", host, port, strerror(errno));
        close(fd);
        throw std::runtime_error(fmt::format("Failed to connect to {}:{}: {}", host, port, strerror(errno)));
      }

      fds.push_back({fd, host, port, domain, type});
      struct epoll_event ev;
      ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
      ev.data.fd = fd;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        logger->error("Failed to add socket to epoll: {}", strerror(errno));
        close(fd);
        throw std::runtime_error(fmt::format("Failed to add socket to epoll: {}", strerror(errno)));
      }
    }

    int remaining = fds.size();
    int tries = 0;

    while (remaining > 0) {
      struct epoll_event events[CONFIG_LIB_SOCKET_LIMIT] = {};

      int nfds = epoll_pwait2(epoll_fd, events, CONFIG_LIB_SOCKET_LIMIT, &timeout, nullptr);

      if (nfds < 0) {
        logger->error("epoll_wait failed: {}", strerror(errno));
        throw std::runtime_error(fmt::format("epoll_wait failed: {}", strerror(errno)));
        break;
      }
      if (nfds == 0) {
        if (tries != CONFIG_LIB_TIMEOUT_LIMIT) {
          logger->info("epoll_wait timed out with {} sockets and will retry it {} times", remaining,
                       CONFIG_LIB_TIMEOUT_LIMIT - tries++);
        } else {
          logger->error("epoll_wait timed out with {} sockets and reached the maximum retry limit of {}", remaining,
                        CONFIG_LIB_TIMEOUT_LIMIT);
          std::lock_guard<std::mutex> lock(connected_mutex);
          for (const auto &si : fds) {
            int serr = 0;
            socklen_t len = sizeof(serr);
            if (getsockopt(si.fd, SOL_SOCKET, SO_ERROR, &serr, &len) < 0) {
              logger->error("asnyc:getsockopt(SO_ERROR) failed on fd {}: {}", si.fd, strerror(errno));
              connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::Error;
            } else {
              if (serr == ECONNREFUSED) {
                connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::Refused;
              } else if (serr == ETIMEDOUT) {
                connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::Timeout;
              } else if (serr == EHOSTUNREACH) {
                connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::HostUnreachable;
              } else if (serr == ENETUNREACH) {
                connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::NetworkUnreachable;
              } else {
                connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::Error;
              }
            }
            close(si.fd);
          }
          fds.clear();
          remaining = 0;
          break;
        }
      }
      if (nfds > 0) {
        for (int i = 0; i < nfds; ++i) {
          auto si =
              std::find_if(fds.begin(), fds.end(), [&](const socketInfo &s) { return s.fd == events[i].data.fd; });
          if (si == fds.end()) {
            logger->error("Received event for unknown fd: {}", events[i].data.fd);
            continue;
          }
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, si->fd, nullptr)) {
            logger->error("Failed to remove socket({}) from epoll: {}", si->fd, strerror(errno));
          }
          if (events[i].events & EPOLLOUT) {
            socketInfo si_copy = *si; // Make a copy of socketInfo to avoid dangling reference
            std::future<void> fut = std::async(std::launch::async, [&, si_copy]() {
              int serr = 0;
              socklen_t len = sizeof(serr);
              std::lock_guard<std::mutex> lock(connected_mutex);
              if (getsockopt(si_copy.fd, SOL_SOCKET, SO_ERROR, &serr, &len) < 0) {
                logger->error("asnyc:getsockopt(SO_ERROR) failed on fd {}: {}", si_copy.fd, strerror(errno));
                connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                    ConnectionStatus::Error;
                return;
              }
              if (serr == 0) {
                connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                    ConnectionStatus::Open;
              } else {
                if (serr == ECONNREFUSED) {
                  connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                      ConnectionStatus::Refused;
                } else if (serr == ETIMEDOUT) {
                  connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                      ConnectionStatus::Timeout;
                } else if (serr == EHOSTUNREACH) {
                  connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                      ConnectionStatus::HostUnreachable;
                } else if (serr == ENETUNREACH) {
                  connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                      ConnectionStatus::NetworkUnreachable;
                } else {
                  connected[formatResultKey(si_copy.domain, si_copy.type, si_copy.host, si_copy.port)] =
                      ConnectionStatus::Error;
                }
              }
              close(si_copy.fd);
            });
            futures.push_back(std::move(fut));
            fds.erase(si);
          } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
            std::lock_guard<std::mutex> lock(connected_mutex);
            int serr = 0;
            socklen_t len = sizeof(serr);
            if (getsockopt(si->fd, SOL_SOCKET, SO_ERROR, &serr, &len) < 0) {
              logger->error("getsockopt(SO_ERROR) failed on fd {}: {}", si->fd, strerror(errno));
              connected[formatResultKey(si->domain, si->type, si->host, si->port)] = ConnectionStatus::Error;
            }
            close(si->fd);
            fds.erase(si);
          } else {
            logger->error("!Connection failed on fd {}: {}", si->fd, strerror(errno));
          }
          --remaining;
        }
      }
    }
  }

  for (auto &fut : futures) {
    fut.get();
  }
  futures.clear();
  close(epoll_fd);

  reserveFileDescriptors(CONFIG_LIB_SOCKET_LIMIT * -1); // release reserved file descriptors
  return connected;
}
} // namespace explo::lib::impl
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
