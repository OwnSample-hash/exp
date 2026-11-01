/**
 * @file fwd.hpp
 * @brief The `fwd` plugin tool: a front-end for libfwd's port forwarding,
 * TUN/TAP tunnelling and routing, driven through a local `fwdd` daemon.
 *
 * The tool manages the lifetime of a local `fwdd` process (spawned preferably
 * as root, so routes/tunnels/sysctl work) and talks to it over the plain-UDP
 * control channel on loopback, authenticated with a per-session token. The
 * daemon is started lazily the first time a command needs it, or explicitly
 * with `start`; if a reachable `fwdd` is already running on the configured
 * address the tool controls that one instead of spawning its own. Every libfwd
 * control command is exposed as a framework command (fwd-add, tun-add,
 * route-add, list-fwd, ...).
 */
#pragma once

#include <cstdint>
#include <interfaces/tool.hpp>
#include <memory>
#include <mutex>
#include <spdlog/logger.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

using namespace explo;

class Fwd final : public ITool {
  struct Config {
    std::string addr = "127.0.0.1"; ///< control channel bind/connect address (keep on loopback)
    uint16_t port = 7777;           ///< UDP control port
    std::string token;              ///< control token; generated on first start
    bool preferRoot = true;         ///< escalate via sudo -n when not already root
  };

  std::shared_ptr<spdlog::logger> logger;
  std::string prefix; ///< variable prefix, set by invoke()

  std::mutex mtx;        ///< guards the daemon state below
  Config config;         //
  pid_t daemonPid = -1;  ///< pid of the process we spawned (fwdd, or sudo) ; -1 = none
  bool spawned = false;  ///< true when this tool owns the daemon's lifetime
  bool usedRoot = false; ///< the spawned daemon runs with root privileges
  std::string logPath;   ///< file fwdd's stderr is redirected to

  // --- daemon lifecycle (all assume `mtx` is held) ---
  static std::string randomToken();
  static pid_t spawnProcess(const std::vector<std::string> &argv, const std::string &logPath);
  bool pingDaemonLocked(int timeout_ms);  ///< reachable on the control channel?
  bool ownedAliveLocked();                ///< our spawned process still exists
  std::string startDaemonLocked();        ///< start fwdd if needed; returns status text
  std::string stopDaemonLocked();         ///< stop a daemon we own
  void ensureDaemonLocked();              ///< start lazily; throws on failure

  // --- control channel (assume `mtx` is held) ---
  std::string sendRawLocked(const std::string &encoded);
  std::string runLocked(int cmdNum, const std::vector<std::string> &humanArgs);

  // --- command handlers ---
  std::string cmdStart();
  std::string cmdStop();
  std::string cmdStatus();
  std::string run(int cmdNum, const std::vector<std::string> &humanArgs); ///< locks `mtx`

  void publishVars();
  void readVars(); ///< pull addr/port/token/sudo from the variable store

public:
  Fwd() = default;
  ~Fwd() override { shutdown(); }

  const char *getName() const override { return "fwd"; }
  const SemVer getVersion() const override { return {0, 0, 1}; }

  void initialize(initArgs &) override;
  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;
  void suppress() override;

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  const std::vector<std::string> &getTags() const override {
    static std::vector<std::string> tags = {"network", "pivot", "tunnel", "routing", "utility"};
    return tags;
  }

  const uuids::uuid getUUID() override { return uuids::uuid::from_string(FWD_UUID).value(); }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
