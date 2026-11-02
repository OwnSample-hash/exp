#include <fwd.hpp>

#include "cmd/variable.hpp"
#include <cerrno>
#include <cmd.hpp>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fwd.h>
#include <plugin_interface.hpp>
#include <random>
#include <spdlog/spdlog.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Absolute path to the fwdd binary, injected by CMake ($<TARGET_FILE:fwdd>).
// Falls back to a PATH lookup if the build did not define it.
#ifndef FWDD_PATH
#define FWDD_PATH "fwdd"
#endif

using namespace explo;

namespace {

/// Non-blocking reap: true if the child has already exited.
bool reapIfExited(pid_t pid) {
  int st = 0;
  return ::waitpid(pid, &st, WNOHANG) == pid;
}

/// Ask a child to stop (SIGTERM, which sudo forwards to fwdd), then reap it,
/// escalating to SIGKILL after ~2s.
void killAndReap(pid_t pid) {
  if (pid <= 0)
    return;
  ::kill(pid, SIGTERM);
  for (int i = 0; i < 20; ++i) {
    int st = 0;
    pid_t r = ::waitpid(pid, &st, WNOHANG);
    if (r == pid || (r < 0 && errno == ECHILD))
      return;
    ::usleep(100000);
  }
  ::kill(pid, SIGKILL);
  int st = 0;
  ::waitpid(pid, &st, 0);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------

void Fwd::initialize(initArgs &args) {
  this->logger = args.logger;

  auto ctx = std::make_shared<cmd::Context>(this->getName());

  // --- daemon control -------------------------------------------------------
  {
    cmd::CommandDef c;
    c.name = "start";
    c.description = "Start the local fwdd daemon (preferably as root) if it is not already running";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->cmdStart(); };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "stop";
    c.description = "Stop the fwdd daemon this tool started";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->cmdStop(); };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "status";
    c.description = "Show the daemon and control channel status";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->cmdStatus(); };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "ping";
    c.description = "Ping the control channel";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->run(FWD_CMD_PING, {}); };
    ctx->registerCommand(c);
  }

  // --- libfwd commands mapped into the framework ----------------------------
  // Each takes libfwd's human readable arguments (see `fwd_cmd_encode_strs`)
  // straight from the command line; the encoder validates them, the daemon
  // executes and the response is decoded back to readable text / tables.
  struct Mapped {
    const char *name;
    int num;
    const char *desc;
  };
  static const Mapped mapped[] = {
      {"fwd-add", FWD_CMD_FWD_ADD, "Add a port forward: fwd-add <tcp|udp> <laddr> <lport> <raddr> <rport>"},
      {"fwd-del", FWD_CMD_FWD_DEL, "Delete a forward: fwd-del <id>"},
      {"fwd-mod", FWD_CMD_FWD_MOD, "Modify a forward: fwd-mod <id> <tcp|udp> <laddr> <lport> <raddr> <rport>"},
      {"tun-add", FWD_CMD_TUN_ADD,
       "Add a tunnel: tun-add <tun|tap> <name> <laddr> <lport> <raddr> <rport> <ifaddr> <prefix> <mtu> "
       "[mode [transport [pin]]]"},
      {"tun-del", FWD_CMD_TUN_DEL, "Delete a tunnel: tun-del <id>"},
      {"tun-mod", FWD_CMD_TUN_MOD,
       "Modify a tunnel: tun-mod <id> <tun|tap> <name> <laddr> <lport> <raddr> <rport> <ifaddr> <prefix> <mtu> "
       "[mode [transport [pin]]]"},
      {"route-add", FWD_CMD_ROUTE_ADD, "Add a route: route-add <dst> <prefix> <gateway> <ifname> <metric> <table>"},
      {"route-del", FWD_CMD_ROUTE_DEL, "Delete a route: route-del <dst> <prefix> <gateway> <ifname> <metric> <table>"},
      {"route-mod", FWD_CMD_ROUTE_MOD, "Replace a route: route-mod <dst> <prefix> <gateway> <ifname> <metric> <table>"},
      {"sysctl-set", FWD_CMD_SYSCTL_SET, "Set a net.* sysctl: sysctl-set <key> <value>"},
      {"sysctl-get", FWD_CMD_SYSCTL_GET, "Read a net.* sysctl: sysctl-get <key>"},
      {"list-fwd", FWD_CMD_LIST_FWD, "List port forwards"},
      {"list-tun", FWD_CMD_LIST_TUN, "List tunnels"},
      {"list-routes", FWD_CMD_LIST_ROUTES, "List routes"},
      {"list-tun-peers", FWD_CMD_LIST_TUN_PEERS, "List the peers of a tunnel: list-tun-peers <id>"},
  };
  for (const auto &m : mapped) {
    cmd::CommandDef c;
    c.name = m.name;
    c.description = m.desc;
    c.variadic = true; // arguments are validated by libfwd's encoder, not the parser
    const int num = m.num;
    c.handler = [this, num](const cmd::ExecutionContext &ec) -> std::string {
      return this->run(num, {ec.args.begin() + 1, ec.args.end()});
    };
    ctx->registerCommand(c);
  }

  // Escape hatch: send an already wire-encoded command string.
  {
    cmd::CommandDef c;
    c.name = "raw";
    c.description = "Send a pre-encoded command string to the daemon: raw <encoded>";
    c.variadic = true;
    c.handler = [this](const cmd::ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 2)
        throw std::runtime_error("Usage: raw <encoded-command>");
      this->readVars();
      std::lock_guard<std::mutex> lk(this->mtx);
      this->ensureDaemonLocked();
      return this->sendRawLocked(ec.args[1]);
    };
    ctx->registerCommand(c);
  }

  cmd::CommandProcessor::instance().registerContext(ctx);
}

void Fwd::shutdown() {
  std::lock_guard<std::mutex> lk(mtx);
  stopDaemonLocked();
}

void Fwd::invoke(std::string_view prefix, bool /*soft*/) {
  this->prefix = prefix;
  readVars(); // adopt any values the user already set
  publishVars();
}

void Fwd::suppress() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const char *n : {"addr", "port", "token", "sudo", "status"})
    vars.unset(this->prefix + n);
}

void Fwd::execute() {
  // Default action when the tool is run directly: bring the daemon up.
  try {
    cmdStart();
  } catch (const std::exception &e) {
    if (logger)
      logger->warn("fwd: {}", e.what());
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Variables
// ---------------------------------------------------------------------------------------------------------------------

void Fwd::publishVars() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  std::lock_guard<std::mutex> lk(mtx);
  vars.set(this->prefix + "addr", cmd::VarValue{config.addr});
  vars.set(this->prefix + "port", cmd::VarValue{static_cast<long long>(config.port)});
  vars.set(this->prefix + "token", cmd::VarValue{config.token});
  vars.set(this->prefix + "sudo", cmd::VarValue{config.preferRoot});
  vars.set(this->prefix + "status", cmd::VarValue{std::string(spawned ? (usedRoot ? "running (root)" : "running")
                                                                      : "stopped")});
}

void Fwd::readVars() {
  if (this->prefix.empty())
    return;
  auto &vars = cmd::CommandProcessor::instance().vars();
  std::lock_guard<std::mutex> lk(mtx);
  // Endpoint and privilege changes only take effect while we do not already
  // own a running daemon, so a restart is never implicit.
  if (!(spawned && daemonPid > 0)) {
    if (auto v = vars.get(this->prefix + "addr"))
      config.addr = v->toString();
    if (auto v = vars.get(this->prefix + "port")) {
      long long p = v->toInt();
      if (p > 0 && p <= 65535)
        config.port = static_cast<uint16_t>(p);
    }
    if (auto v = vars.get(this->prefix + "token"))
      config.token = v->toString();
    if (auto v = vars.get(this->prefix + "sudo"))
      config.preferRoot = v->toBool();
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Daemon lifecycle
// ---------------------------------------------------------------------------------------------------------------------

std::string Fwd::randomToken() {
  static const char hex[] = "0123456789abcdef";
  std::random_device rd;
  std::string t;
  t.reserve(32);
  for (int i = 0; i < 32; ++i)
    t.push_back(hex[rd() & 0xF]);
  return t;
}

pid_t Fwd::spawnProcess(const std::vector<std::string> &argv, const std::string &logPath) {
  if (argv.empty())
    return -1;
  // Build the C argv in the parent (no allocation in the child between fork and exec).
  std::vector<char *> cargv;
  cargv.reserve(argv.size() + 1);
  for (const auto &s : argv)
    cargv.push_back(const_cast<char *>(s.c_str()));
  cargv.push_back(nullptr);

  pid_t pid = ::fork();
  if (pid < 0)
    return -1;
  if (pid == 0) {
    ::setsid(); // detach from the TUI's controlling terminal / signals
    int devnull = ::open("/dev/null", O_RDONLY);
    if (devnull >= 0) {
      ::dup2(devnull, STDIN_FILENO);
      if (devnull > STDERR_FILENO)
        ::close(devnull);
    }
    int lf = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (lf >= 0) {
      ::dup2(lf, STDOUT_FILENO);
      ::dup2(lf, STDERR_FILENO);
      if (lf > STDERR_FILENO)
        ::close(lf);
    }
    ::execvp(cargv[0], cargv.data());
    ::_exit(127); // exec failed
  }
  return pid;
}

bool Fwd::pingDaemonLocked(int timeout_ms) {
  fwd_addr srv;
  if (fwd_addr_from_string(&srv, config.addr.c_str()) != FWD_OK)
    return false;
  std::string line = config.token.empty() ? "0" : (config.token + "@0"); // ping == command 0
  char resp[FWD_MAX_RESP + 1];
  int rc = fwd_client_request(&srv, config.port, line.c_str(), resp, sizeof resp, timeout_ms);
  if (rc < 0)
    return false;
  resp[rc] = '\0';
  fwd_response r;
  return fwd_response_parse(resp, &r) == FWD_OK && r.status == 0;
}

bool Fwd::ownedAliveLocked() {
  if (!spawned || daemonPid <= 0)
    return false;
  if (reapIfExited(daemonPid)) {
    spawned = false;
    daemonPid = -1;
    return false;
  }
  if (::kill(daemonPid, 0) != 0 && errno == ESRCH) {
    spawned = false;
    daemonPid = -1;
    return false;
  }
  return true;
}

std::string Fwd::startDaemonLocked() {
  if (ownedAliveLocked())
    return "fwdd already running (pid " + std::to_string(daemonPid) + ")";
  if (pingDaemonLocked(300))
    return "a fwdd is already reachable on " + config.addr + ":" + std::to_string(config.port) +
           " (not managed by this tool)";

  if (config.token.empty())
    config.token = randomToken();
  if (logPath.empty())
    logPath = (std::filesystem::temp_directory_path() / ("explo-fwdd-" + std::to_string(config.port) + ".log"))
                  .string();

  const std::string portStr = std::to_string(config.port);
  const std::vector<std::string> base = {FWDD_PATH, "-a", config.addr, "-p", portStr, "-t", config.token};

  // Spawn `argv`, wait (~3s) for the control channel to answer, and reap it if
  // it never does. Returns the pid on success or -1.
  auto tryStart = [this](const std::vector<std::string> &argv) -> pid_t {
    pid_t pid = spawnProcess(argv, logPath);
    if (pid <= 0)
      return -1;
    for (int i = 0; i < CONFIG_FWD_READY_RETRIES; ++i) {
      if (reapIfExited(pid))
        return -1; // exec failed or the daemon died at startup
      if (pingDaemonLocked(CONFIG_FWD_READY_INTERVAL_MS))
        return pid;
    }
    killAndReap(pid);
    return -1;
  };

  pid_t pid = -1;
  bool asRoot = false;
  const bool amRoot = (::geteuid() == 0);

  if (amRoot) {
    pid = tryStart(base);
    asRoot = (pid > 0);
  } else {
    if (config.preferRoot) {
      std::vector<std::string> sudoArgv = {"sudo", "-n"};
      sudoArgv.insert(sudoArgv.end(), base.begin(), base.end());
      pid = tryStart(sudoArgv);
      if (pid > 0)
        asRoot = true;
      else if (logger)
        logger->warn("fwd: could not start fwdd as root (passwordless sudo unavailable); "
                     "starting unprivileged — routes, tunnels and sysctl will fail");
    }
    if (pid < 0)
      pid = tryStart(base); // unprivileged fallback
  }

  if (pid < 0)
    throw std::runtime_error("failed to start fwdd (see " + logPath + ")");

  daemonPid = pid;
  spawned = true;
  usedRoot = asRoot;
  if (logger)
    logger->info("fwd: fwdd started (pid {}){} on {}:{}", pid, asRoot ? " as root" : " unprivileged", config.addr,
                 config.port);
  return "fwdd started (pid " + std::to_string(pid) + ")" + (asRoot ? " as root" : " unprivileged, limited caps") +
         " on " + config.addr + ":" + portStr;
}

std::string Fwd::stopDaemonLocked() {
  if (!spawned || daemonPid <= 0) {
    daemonPid = -1;
    spawned = false;
    return "no managed fwdd to stop";
  }
  pid_t pid = daemonPid;
  killAndReap(pid);
  daemonPid = -1;
  spawned = false;
  usedRoot = false;
  if (logger)
    logger->info("fwd: fwdd (pid {}) stopped", pid);
  return "fwdd (pid " + std::to_string(pid) + ") stopped";
}

void Fwd::ensureDaemonLocked() {
  if (ownedAliveLocked())
    return;
  if (pingDaemonLocked(300))
    return; // an already-running (possibly external) daemon answers
  startDaemonLocked();
}

// ---------------------------------------------------------------------------------------------------------------------
// Control channel
// ---------------------------------------------------------------------------------------------------------------------

std::string Fwd::sendRawLocked(const std::string &encoded) {
  fwd_addr srv;
  if (fwd_addr_from_string(&srv, config.addr.c_str()) != FWD_OK)
    throw std::runtime_error("invalid control address: " + config.addr);

  const std::string line = config.token.empty() ? encoded : (config.token + "@" + encoded);
  char resp[FWD_MAX_RESP + 1];
  int rc = fwd_client_request(&srv, config.port, line.c_str(), resp, sizeof resp, CONFIG_FWD_CONTROL_TIMEOUT_MS);
  if (rc < 0)
    throw std::runtime_error(std::string("control channel: ") + fwd_strerror(rc));
  resp[rc] = '\0';

  char text[FWD_MAX_DECODED];
  int tl = fwd_response_decode(resp, text, sizeof text);
  if (tl < 0)
    return std::string(resp, static_cast<size_t>(rc)); // not decodable: show the raw line
  std::string out(text, static_cast<size_t>(tl));
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    out.pop_back();
  return out;
}

std::string Fwd::runLocked(int cmdNum, const std::vector<std::string> &humanArgs) {
  std::vector<const char *> cargv;
  cargv.reserve(humanArgs.size());
  for (const auto &a : humanArgs)
    cargv.push_back(a.c_str());

  char line[FWD_MAX_CMD + 1];
  int rc = fwd_cmd_encode_strs(cmdNum, cargv.data(), static_cast<int>(humanArgs.size()), line, sizeof line);
  if (rc < 0) {
    const char *nm = fwd_cmd_name(cmdNum);
    throw std::runtime_error(std::string(nm ? nm : "command") + ": " + fwd_strerror(rc));
  }
  return sendRawLocked(line);
}

std::string Fwd::run(int cmdNum, const std::vector<std::string> &humanArgs) {
  readVars();
  std::lock_guard<std::mutex> lk(mtx);
  ensureDaemonLocked();
  return runLocked(cmdNum, humanArgs);
}

// ---------------------------------------------------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------------------------------------------------

std::string Fwd::cmdStart() {
  readVars();
  std::string msg;
  {
    std::lock_guard<std::mutex> lk(mtx);
    msg = startDaemonLocked();
  }
  publishVars();
  return msg;
}

std::string Fwd::cmdStop() {
  std::string msg;
  {
    std::lock_guard<std::mutex> lk(mtx);
    msg = stopDaemonLocked();
  }
  publishVars();
  return msg;
}

std::string Fwd::cmdStatus() {
  std::lock_guard<std::mutex> lk(mtx);
  std::ostringstream os;
  os << "control : " << config.addr << ":" << config.port << " (udp)\n";
  os << "token   : " << (config.token.empty() ? "(none)" : config.token) << "\n";
  if (ownedAliveLocked())
    os << "daemon  : managed, pid " << daemonPid << (usedRoot ? ", root" : ", unprivileged") << "\n";
  else if (pingDaemonLocked(300))
    os << "daemon  : external (reachable, not started by this tool)\n";
  else
    os << "daemon  : not running\n";
  os << "reachable: " << (pingDaemonLocked(300) ? "yes" : "no");
  if (!logPath.empty())
    os << "\nlog     : " << logPath;
  return os.str();
}

// Register the tool with the framework (picked up when the plugin .so loads).
static ToolRegistry::Add<Fwd> fwdToolRegister("fwd");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
