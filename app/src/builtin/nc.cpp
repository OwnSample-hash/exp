#include "cmd/variable.hpp"
#include <arpa/inet.h>
#include <builtin/nc.hpp>
#include <cctype>
#include <cerrno>
#include <cmd.hpp>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <lib.hpp>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

namespace explo {
namespace builtin {

extern struct termios origTermios;

namespace {

/// Puts the terminal into "cbreak" mode while a channel is in the foreground so single key presses (the
/// background key, Ctrl+D) are seen immediately, and makes stdin non-blocking. Restores everything on scope exit.
class TerminalGuard {
  bool tty = false;
  termios saved{};
  int savedFlags = -1;

public:
  TerminalGuard() {
    tty = isatty(STDIN_FILENO) == 1;
    if (tty && tcgetattr(STDIN_FILENO, &saved) == 0) {
      termios t = origTermios;
      t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ISIG | IEXTEN); // keep ECHO, ICRNL as in the original state
      t.c_cc[VMIN] = 1;
      t.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else {
      tty = false;
    }
    savedFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (savedFlags >= 0)
      fcntl(STDIN_FILENO, F_SETFL, savedFlags | O_NONBLOCK);
  }
  ~TerminalGuard() {
    if (savedFlags >= 0)
      fcntl(STDIN_FILENO, F_SETFL, savedFlags);
    if (tty)
      tcsetattr(STDIN_FILENO, TCSANOW, &saved);
  }
  bool isTty() const { return tty; }
  TerminalGuard(const TerminalGuard &) = delete;
  TerminalGuard &operator=(const TerminalGuard &) = delete;
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------

void NC::initialize(initArgs &args) {
  this->logger = args.logger;

  auto ctx = std::make_shared<cmd::Context>(this->getName());
  {
    cmd::CommandDef c;
    c.name = "run";
    c.description = "Open a new connection and attach to it (background it with the bgch key)";
    c.variadic = true;
    c.addDynamic("<host>", R"([^\s]+)", "Target host");
    c.addDynamic("<port>", R"(\d+)", "Target port");
    c.handler = [this](const cmd::ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 3)
        throw std::runtime_error("Usage: run <host> <port>");
      this->config.host = ec.args[1];
      this->config.port = htons(static_cast<uint16_t>(std::stoi(ec.args[2])));
      spdlog::debug("Configured nc to connect to {}:{}", this->config.host, ntohs(this->config.port));
      int id = this->openChannel(this->config);
      return this->runForeground(id);
    };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "channels";
    c.description = "List open channels";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->listChannels(); };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "fg";
    c.description = "Bring a background channel to the foreground";
    c.variadic = true;
    c.addDynamic("<id>", R"(\d+)", "Channel id (see 'channels')");
    c.handler = [this](const cmd::ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 2)
        throw std::runtime_error("Usage: fg <id>");
      return this->runForeground(std::stoi(ec.args[1]));
    };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "close";
    c.description = "Close a channel";
    c.variadic = true;
    c.addDynamic("<id>", R"(\d+)", "Channel id (see 'channels')");
    c.handler = [this](const cmd::ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 2)
        throw std::runtime_error("Usage: close <id>");
      return this->closeChannel(std::stoi(ec.args[1]));
    };
    ctx->registerCommand(c);
  }
  cmd::CommandProcessor::instance().registerContext(ctx);

  // Start the background reader. It lives until shutdown(), regardless of invoke()/suppress().
  if (::pipe(wakePipe) < 0)
    throw std::runtime_error(std::string("pipe(): ") + strerror(errno));
  for (int fd : wakePipe) {
    set_nonblocking(fd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  running = true;
  reader = std::thread(&NC::backgroundLoop, this);
}

void NC::shutdown() {
  // Idempotent: also called from the destructor.
  if (running.exchange(false)) {
    wakeReader();
    if (reader.joinable())
      reader.join();
  }

  {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto &[id, ch] : channels) {
      if (ch.fd >= 0) {
        ::shutdown(ch.fd, SHUT_RDWR);
        explo::lib::close(ch.fd);
        ch.fd = -1;
      }
    }
    channels.clear();
  }

  for (int &fd : wakePipe) {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }
  if (logger)
    logger->debug("nc: all channels closed");
}

void NC::invoke(std::string_view prefix, bool soft) {
  // May be called many times. Only (re)publishes the variables; channels and the reader are untouched.
  this->prefix = prefix;
  auto &vars = cmd::CommandProcessor::instance().vars();
  vars.set(this->prefix + "bgch", cmd::VarValue{bgKeyNotation}); // caret notation, default ^T
}

void NC::suppress() {
  // May be called many times. Background channels keep being read by the reader thread.
  auto &vars = cmd::CommandProcessor::instance().vars();
  vars.unset(this->prefix + "bgch");
}

void NC::execute() {
  int id = openChannel(config);
  runForeground(id);
}

// ---------------------------------------------------------------------------------------------------------------------
// Background key (caret notation)
// ---------------------------------------------------------------------------------------------------------------------

bool NC::parseCaret(std::string_view s, unsigned char &out) {
  if (s.size() != 2 || s[0] != '^')
    return false;
  unsigned char c = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(s[1])));
  if (c == '?') {
    out = 0x7F; // ^? = DEL
    return true;
  }
  if (c >= '@' && c <= '_') {
    out = static_cast<unsigned char>(c - '@'); // ^@ = 0 ... ^_ = 31
    return true;
  }
  return false;
}

void NC::setBackgroundKey(std::string_view notation) {
  unsigned char b;
  if (parseCaret(notation, b)) {
    bgKeyNotation = std::string(notation);
    bgKeyByte = b;
  } else if (logger) {
    logger->warn("nc: invalid bgch '{}', keeping '{}'", notation, bgKeyNotation);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Channel management
// ---------------------------------------------------------------------------------------------------------------------

int NC::openChannel(const NCConfig &cfg) {
  const uint16_t port_host = ntohs(cfg.port);

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = cfg.isTcp ? SOCK_STREAM : SOCK_DGRAM;

  addrinfo *res = nullptr;
  const std::string port_str = std::to_string(port_host);
  if (getaddrinfo(cfg.host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) {
    logger->error("Failed to resolve host {}:{}", cfg.host, port_host);
    throw std::runtime_error("getaddrinfo failed for host: " + cfg.host);
  }
  struct AddrGuard {
    addrinfo *p;
    ~AddrGuard() { freeaddrinfo(p); }
  } guard{res};

  int sockfd = -1;
  int lastErr = 0;
  for (addrinfo *ai = res; ai; ai = ai->ai_next) {
    sockfd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sockfd < 0) {
      lastErr = errno;
      continue;
    }
    if (::connect(sockfd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    lastErr = errno;
    ::close(sockfd);
    sockfd = -1;
  }
  if (sockfd < 0) {
    logger->error("Failed to connect to {}:{} - {}", cfg.host, port_host, strerror(lastErr));
    throw std::runtime_error(std::string("connect(): ") + strerror(lastErr));
  }

  set_nonblocking(sockfd);
  fcntl(sockfd, F_SETFD, FD_CLOEXEC);

  int id;
  {
    std::lock_guard<std::mutex> lock(mtx);
    id = nextId++;
    Channel &ch = channels[id];
    ch.id = id;
    ch.fd = sockfd;
    ch.host = cfg.host;
    ch.port = port_host;
    ch.tcp = cfg.isTcp;
    ch.state = ChannelState::Background; // runForeground() promotes it
  }
  logger->debug("nc: channel {} opened to {}:{}", id, cfg.host, port_host);
  return id;
}

std::string NC::closeChannel(int id) {
  {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = channels.find(id);
    if (it == channels.end())
      throw std::runtime_error("No such channel: " + std::to_string(id));
    if (it->second.fd >= 0)
      explo::lib::close(it->second.fd);
    channels.erase(it);
  }
  wakeReader();
  return "Channel " + std::to_string(id) + " closed";
}

std::string NC::listChannels() {
  std::lock_guard<std::mutex> lock(mtx);
  if (channels.empty())
    return "No open channels";
  std::ostringstream os;
  for (auto &[id, ch] : channels) {
    os << "[" << id << "] " << ch.host << ":" << ch.port << (ch.tcp ? " tcp " : " udp ")
       << (ch.state == ChannelState::Foreground ? "foreground" : "background") << ", " << ch.buffer.size()
       << " bytes unread";
    if (ch.closed)
      os << ", closed" << (ch.error.empty() ? "" : " (" + ch.error + ")");
    os << "\n";
  }
  std::string s = os.str();
  if (!s.empty())
    s.pop_back();
  return s;
}

// ---------------------------------------------------------------------------------------------------------------------
// Foreground session
// ---------------------------------------------------------------------------------------------------------------------

std::string NC::runForeground(int id) {
  int fd = -1;
  std::string pending;
  std::string label;
  bool wasClosed = false;
  bool isTcp = true;
  std::string closeErr;

  {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = channels.find(id);
    if (it == channels.end())
      throw std::runtime_error("No such channel: " + std::to_string(id));
    for (auto &[oid, och] : channels)
      if (och.state == ChannelState::Foreground)
        throw std::runtime_error("Channel " + std::to_string(oid) + " is already in the foreground");

    Channel &ch = it->second;
    label = ch.host + ":" + std::to_string(ch.port);
    isTcp = ch.tcp;
    pending.swap(ch.buffer); // data collected while in background
    if (ch.closed) {
      wasClosed = true;
      closeErr = ch.error;
      if (ch.fd >= 0)
        explo::lib::close(ch.fd);
      channels.erase(it);
    } else {
      ch.state = ChannelState::Foreground; // reader thread stops touching this fd (checked under the lock)
      fd = ch.fd;
    }
  }
  wakeReader();

  if (!pending.empty())
    write_all(STDOUT_FILENO, pending.data(), pending.size());

  if (wasClosed) {
    return closeErr.empty() ? "Connection closed by remote host." : "Connection closed: " + closeErr;
  }

  TerminalGuard term; // restored on every exit path
  const bool tty = term.isTty();

  std::cout << "Channel " << id << " (" << label << ") in foreground. "
            << "Press " << bgKeyNotation << " to background it, Ctrl+D to end input." << std::endl;

  constexpr std::size_t BUF = 4096;
  char buf[BUF];
  pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {fd, POLLIN, 0}};

  bool remoteClosed = false;
  bool detach = false;
  std::string remoteErr;

  while (!remoteClosed && !detach) {
    int n = ::poll(fds, 2, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      logger->error("poll(): {}", strerror(errno));
      detach = true; // leave the channel alive in the background
      break;
    }

    // --- socket -> stdout (handled first so remote output is shown before a detach) ---
    if (fds[1].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
      ssize_t r = ::read(fd, buf, BUF);
      if (r > 0) {
        write_all(STDOUT_FILENO, buf, static_cast<std::size_t>(r));
      } else if (r == 0) {
        if (isTcp || (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)))
          remoteClosed = true;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        remoteClosed = true;
        remoteErr = strerror(errno);
      }
      if (remoteClosed)
        break;
    }

    // --- stdin -> socket ---
    if (fds[0].fd >= 0 && (fds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      ssize_t r = ::read(STDIN_FILENO, buf, BUF);
      if (r > 0) {
        std::size_t start = 0;
        const std::size_t len = static_cast<std::size_t>(r);
        for (std::size_t i = 0; i < len; ++i) {
          const unsigned char c = static_cast<unsigned char>(buf[i]);
          if (tty && c == bgKeyByte) { // background key
            if (i > start)
              send_all(fd, buf + start, i - start);
            detach = true;
            start = len; // discard anything typed after the key
            break;
          }
          if (tty && c == 0x04) { // Ctrl+D: raw mode doesn't generate EOF for us
            if (i > start)
              send_all(fd, buf + start, i - start);
            ::shutdown(fd, SHUT_WR);
            fds[0].fd = -1;
            start = len;
            break;
          }
        }
        if (start < len)
          send_all(fd, buf + start, len - start);
      } else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        // EOF / error on stdin: tell the remote we're done sending, keep reading
        ::shutdown(fd, SHUT_WR);
        fds[0].fd = -1;
      }
    }
  }

  std::string result;
  {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = channels.find(id);
    if (it != channels.end()) {
      if (remoteClosed) {
        explo::lib::close(it->second.fd);
        channels.erase(it);
      } else {
        it->second.state = ChannelState::Background; // reader thread takes over again
      }
    }
  }
  wakeReader();

  if (remoteClosed) {
    result = remoteErr.empty() ? "Connection closed by remote host." : "Connection closed: " + remoteErr;
  } else {
    result = "\nChannel " + std::to_string(id) + " (" + label + ") moved to background.";
  }
  logger->debug("nc: leaving foreground of channel {}", id);
  return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Background reader thread
// ---------------------------------------------------------------------------------------------------------------------

void NC::wakeReader() {
  if (wakePipe[1] >= 0) {
    const char b = 1;
    ssize_t r = ::write(wakePipe[1], &b, 1); // EAGAIN (pipe full) is fine: a wake-up is already pending
    (void)r;
  }
}

void NC::backgroundLoop() {
  std::vector<pollfd> fds;
  std::vector<int> ids;
  char tmp[4096];

  while (running) {
    fds.clear();
    ids.clear();
    fds.push_back({wakePipe[0], POLLIN, 0});
    {
      std::lock_guard<std::mutex> lock(mtx);
      for (auto &[id, ch] : channels) {
        if (ch.state == ChannelState::Background && !ch.closed && ch.fd >= 0 && ch.buffer.size() < kMaxBuffered) {
          fds.push_back({ch.fd, POLLIN, 0});
          ids.push_back(id);
        }
      }
    }

    int n = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      logger->error("nc reader: poll(): {}", strerror(errno));
      break;
    }

    if (fds[0].revents & POLLIN) { // drain wake-ups
      char d[64];
      while (::read(wakePipe[0], d, sizeof d) > 0) {
      }
    }
    if (!running)
      break;

    std::lock_guard<std::mutex> lock(mtx);
    for (std::size_t i = 1; i < fds.size(); ++i) {
      if (!fds[i].revents)
        continue;
      auto it = channels.find(ids[i - 1]);
      // Re-validate under the lock: the channel may have been closed or moved to the foreground meanwhile.
      if (it == channels.end() || it->second.fd != fds[i].fd || it->second.state != ChannelState::Background)
        continue;
      Channel &ch = it->second;

      ssize_t r = ::read(ch.fd, tmp, sizeof tmp);
      if (r > 0) {
        ch.buffer.append(tmp, static_cast<std::size_t>(r));
      } else if (r == 0) {
        if (ch.tcp || (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))) {
          ch.closed = true;
          logger->debug("nc: background channel {} closed by remote", ch.id);
        }
      } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        ch.closed = true;
        ch.error = strerror(errno);
        logger->debug("nc: background channel {} error: {}", ch.id, ch.error);
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------------------------------------------------

bool NC::send_all(int fd, const char *buf, std::size_t len) {
  std::size_t sent = 0;
  while (sent < len) {
    ssize_t r = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) { // sockets are non-blocking: wait until writable
        pollfd p{fd, POLLOUT, 0};
        ::poll(&p, 1, -1);
        continue;
      }
      return false;
    }
    if (r == 0)
      return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

void NC::write_all(int fd, const char *buf, std::size_t len) {
  std::size_t written = 0;
  while (written < len) {
    ssize_t r = ::write(fd, buf + written, len - written);
    if (r < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) { // stdout may share stdin's O_NONBLOCK description
        pollfd p{fd, POLLOUT, 0};
        ::poll(&p, 1, -1);
        continue;
      }
      return;
    }
    if (r == 0)
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
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
