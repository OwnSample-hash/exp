#include <arpa/inet.h>
#include <bits/types/sigset_t.h>
#include <cassert>
#include <ctime>
#include <fcntl.h>
#include <future>
#include <inplace_vector>
#include <lib.hpp>
#include <lua.h>
#include <lua_exp_config.hpp>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <utils.hpp>

static auto &getLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  if (!logger)
    logger = spdlog::get("lua_exp")->clone("lua_exp::lua::alib");
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

std::tuple<int, int, std::string, int, int> analyzeFd(int fd, bool timedOut = true) {
  int domain = -1, socktype = -1, port = -1, last_socket_error = -1;
  std::string host = "unknown";
  socklen_t domain_len = sizeof(domain);
  socklen_t socktype_len = sizeof(socktype);
  if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &domain_len) < 0 ||
      getsockopt(fd, SOL_SOCKET, SO_TYPE, &socktype, &socktype_len) < 0) {
    if (domain == -1) {
      perror("getsockopt(SO_DOMAIN) failed");
    }
    if (socktype == -1) {
      perror("getsockopt(SO_TYPE) failed");
    }
  }
  struct sockaddr_storage addr;
  addr.ss_family = domain;
  socklen_t addrlen = domain2size(domain);
  getpeername(fd, (struct sockaddr *)&addr, &addrlen);
  if (domain == AF_INET6) {
    char str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr, str, sizeof(str));
    host = str;
  } else {
    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, str, sizeof(str));
    host = str;
  }
  port = ntohs(((struct sockaddr_in *)&addr)->sin_port);
  socklen_t optlen = sizeof(last_socket_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &last_socket_error, &optlen) < 0) {
  }
  auto &logger = getLogger();
  while (!timedOut) {
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
      perror("getsockopt(SO_ERROR) failed");
      return std::make_tuple(domain, socktype, host, port, last_socket_error);
    }
    if (err == EINPROGRESS) {
      logger->info("Connection still in progress on fd {}", fd);
    } else if (err != 0) {
      perror("Connection failed");
      return std::make_tuple(domain, socktype, host, port, last_socket_error);
    } else {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return std::make_tuple(domain, socktype, host, port, last_socket_error);
}

struct socketInfo {
  int fd;
  std::string host;
  int port;
  int domain;
  int type;
};

int async_scan(lua_State *L) {
  auto &logger = getLogger();
  std::unordered_map<std::string, ConnectionStatus::Type> connected;
  std::mutex connected_mutex;
  std::inplace_vector<socketInfo, CONFIG_LUA_EXP_SOCKET_LIMIT> fds;
  int fd_index = 0;

  int max = luaL_checkinteger(L, 1);

  luaL_checktype(L, 2, LUA_TTHREAD);
  lua_State *co = lua_tothread(L, 2);
  int nargs = lua_gettop(L) - 2;
  lua_xmove(L, co, nargs);

  int step = CONFIG_LUA_EXP_SOCKET_LIMIT;

  const struct timespec timeout{
      .tv_sec = 0,
      .tv_nsec = 250'000'000,
  };
  int epoll_fd = epoll_create1(0);
  std::vector<std::future<void>> futures;
  for (int i = 0; i < max; i += step) {
    for (int j = 0; j < step; ++j) {
      std::string host;
      int port, domain, type;
      int nres;
      int status = lua_resume(co, L, nargs, &nres);

      if (status == LUA_YIELD) {
        if (nres != 4) {
          logger->error("Lua coroutine yielded with insufficient results: {} excepted 4", nres);
          return luaL_error(L, "Lua coroutine yielded with insufficient results: %d excepted 4", nres);
        }
        host = lua_tostring(co, -4);
        port = lua_tointeger(co, -3);
        type = lua_tointeger(co, -2);
        domain = lua_tointeger(co, -1);
        lua_pop(co, nres);
        nargs = 0; // Reset nargs after the first resume
      } else if (status == LUA_OK) {
        logger->info("Lua coroutine completed successfully");
        break;
      } else {
        const char *err_msg = lua_tostring(co, -1);
        logger->error("Lua coroutine error: {}", err_msg ? err_msg : "Unknown error");
        break;
      }

      int fd = socket(domain, type, 0);
      if (fd < 0) {
        logger->error("Failed to create socket: {}", strerror(errno));
        return luaL_error(L, "Failed to create socket: %s", strerror(errno));
        break;
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
          return luaL_error(L, "Invalid IPv6 address: %s", host.c_str());
          continue;
        }
      } else {
        auto *adr = (struct sockaddr_in *)&base_addr;
        adr->sin_family = AF_INET;
        adr->sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &(adr->sin_addr)) <= 0) {
          logger->error("Invalid address: {}", host);
          close(fd);
          return luaL_error(L, "Invalid address: %s", host.c_str());
          continue;
        }
      }

      int ret = connect(fd, (struct sockaddr *)&base_addr, domain2size(domain));
      if (ret < 0 && errno != EINPROGRESS) {
        logger->error("Failed to connect to {}:{}: {}", host, port, strerror(errno));
        close(fd);
        return luaL_error(L, "Failed to connect to %s:%d: %s", host.c_str(), port, strerror(errno));
        continue;
      }

      fds.push_back({fd, host, port, domain, type});
      struct epoll_event ev;
      ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
      ev.data.fd = fd;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        logger->error("Failed to add socket to epoll: {}", strerror(errno));
        close(fd);
        return luaL_error(L, "Failed to add socket to epoll: %s", strerror(errno));
        break;
      }
    }

    int remaining = fds.size();
    int tries = 0;

    while (remaining > 0) {
      struct epoll_event events[CONFIG_LUA_EXP_SOCKET_LIMIT] = {};

      int nfds = epoll_pwait2(epoll_fd, events, CONFIG_LUA_EXP_SOCKET_LIMIT, &timeout, nullptr);

      if (nfds < 0) {
        logger->error("epoll_wait failed: {}", strerror(errno));
        return luaL_error(L, "epoll_wait failed: %s", strerror(errno));
        break;
      }
      if (nfds == 0) {
        if (tries != CONFIG_LUA_EXP_TIMEOUT_LIMIT) {
          logger->info("epoll_wait timed out with {} sockets and will retry it {} times", remaining,
                       CONFIG_LUA_EXP_TIMEOUT_LIMIT - tries++);
        } else {
          logger->error("epoll_wait timed out with {} sockets and reached the maximum retry limit of {}", remaining,
                        CONFIG_LUA_EXP_TIMEOUT_LIMIT);
          for (const auto &si : fds) {
            std::lock_guard<std::mutex> lock(connected_mutex);
            connected[formatResultKey(si.domain, si.type, si.host, si.port)] = ConnectionStatus::Timeout;
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

  lua_createtable(L, 0, connected.size());

  for (const auto &[key, status] : connected) {
    lua_pushnumber(L, status);
    lua_setfield(L, -2, key.c_str());
  }

  return 1;
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
