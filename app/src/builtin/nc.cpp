#define _GNU_SOURCE 1
#include <arpa/inet.h>
#include <builtin/nc.hpp>
#include <cmd.hpp>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

namespace explo {
namespace builtin {

void NC::initialize() {
  auto ctx = std::make_shared<cmd::Context>(this->getName());

  {
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run the nc tool with specified arguments";
      c.variadic = true;
      c.addDynamic("<host>", R"([^\s]+)", "Target host");
      c.addDynamic("<port>", R"(\d+)", "Target port");
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        if (ec.args.size() < 3)
          throw std::runtime_error("Usage: run <host> <port>");
        std::string host = ec.args[1];
        uint16_t port = static_cast<uint16_t>(std::stoi(ec.args[2]));
        this->config.host = host;
        this->config.port = htons(port);
        spdlog::debug("Configured nc to connect to {}:{}", host, port);
        this->execute();
        return "nc execution completed";
      };
      ctx->registerCommand(c);
    }
  }

  cmd::CommandProcessor::instance().registerContext(ctx);
}

void NC::invoke(const std::string &prefix) {
  this->prefix = prefix;
  // Code to run when the nc tool is selected
}

void NC::shutdown() {
  // Cleanup code for the nc tool
}

void NC::suppress() {
  // Code to run when the nc tool is deselected
}

extern struct termios origTermios;

void NC::execute() {
  struct termios oldt;
  tcgetattr(STDIN_FILENO, &oldt);
  tcsetattr(STDIN_FILENO, TCSANOW, &origTermios);

  const uint16_t port_host = ntohs(config.port); // back to host order

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
  hints.ai_socktype = config.isTcp ? SOCK_STREAM : SOCK_DGRAM;

  addrinfo *res = nullptr;
  const std::string port_str = std::to_string(port_host);

  if (getaddrinfo(config.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
    logger->error("Failed to resolve host {}:{}", config.host, port_host);
    throw std::runtime_error("getaddrinfo failed for host: " + config.host);
  }

  // RAII wrapper so we never leak the addrinfo list
  struct AddrGuard {
    addrinfo *p;
    ~AddrGuard() { freeaddrinfo(p); }
  } guard{res};

  int sockfd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd < 0) {
    logger->error("Failed to create socket for {}:{} - {}", config.host, port_host, strerror(errno));
    throw std::runtime_error(std::string("socket(): ") + strerror(errno));
  }

  if (::connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
    ::close(sockfd);
    logger->error("Failed to connect to {}:{} - {}", config.host, port_host, strerror(errno));
    throw std::runtime_error(std::string("connect(): ") + strerror(errno));
  }

  set_nonblocking(STDIN_FILENO);

  std::atomic<bool> done{false};

  constexpr std::size_t BUF = 4096;
  char buf[BUF];

  // Two poll descriptors: [0] = stdin, [1] = socket
  pollfd fds[2];
  fds[0] = {STDIN_FILENO, POLLIN, 0};
  fds[1] = {sockfd, POLLIN, 0};
  std::cout << "Connected to " << config.host << ":" << port_host << ". Type Ctrl+D to end input and close connection."
            << std::endl;

  while (!done) {
    int n = poll(fds, 2, -1 /*block forever*/);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    // --- stdin → socket ---
    if (fds[0].revents & POLLIN) {
      ssize_t r = ::read(STDIN_FILENO, buf, BUF);
      if (r <= 0) {
        // EOF on stdin: signal remote that we're done sending
        ::shutdown(sockfd, SHUT_WR);
        fds[0].fd = -1; // stop polling stdin
      } else {
        send_all(sockfd, buf, static_cast<std::size_t>(r));
      }
    }
    if (fds[0].revents & (POLLHUP | POLLERR)) {
      ::shutdown(sockfd, SHUT_WR);
      fds[0].fd = -1;
      logger->error("Error on stdin: {}", strerror(errno));
    }

    // --- socket → stdout ---
    if (fds[1].revents & POLLIN) {
      ssize_t r = ::read(sockfd, buf, BUF);
      if (r <= 0) {
        done = true; // server closed connection
        std::cout << "Connection closed by remote host." << std::endl;
      } else {
        write_all(STDOUT_FILENO, buf, static_cast<std::size_t>(r));
      }
    }
    if (fds[1].revents & (POLLHUP | POLLERR)) {
      done = true;
      if (errno == 0) {
        std::cout << "Connection closed by remote host." << std::endl;
      } else {
        logger->error("Error on socket: {}", strerror(errno));
      }
    }
  }

  ::close(sockfd);

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK) < 0) {
    logger->error("Failed to restore blocking mode on stdin: {}", strerror(errno));
  }
  logger->debug("Restored original terminal settings");
}

void NC::send_all(int fd, const char *buf, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
    ssize_t r = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (r <= 0)
      return; // connection dropped
    sent += static_cast<std::size_t>(r);
  }
}

void NC::write_all(int fd, const char *buf, std::size_t len) {
  std::size_t written = 0;
  while (written < len) {
    ssize_t r = ::write(fd, buf + written, len - written);
    if (r <= 0)
      return;
    written += static_cast<std::size_t>(r);
  }
}

void NC::set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace builtin
} // namespace explo
