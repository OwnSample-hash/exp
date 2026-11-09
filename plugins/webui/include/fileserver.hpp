#pragma once

#include <atomic>
#include <cmd/var_snapshot.hpp>
#include <filesystem>
#include <httplib.h>
#include <interfaces/tool.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <uuid.hpp>
#include <webui_config.hpp>

using namespace explo;

namespace fs = std::filesystem;

/**
 * @brief A tool that hosts an HTTP(S) web server for downloading files from a
 * directory.
 *
 * Settings live in their own menu (see plugins/webui/fileserver.yaml), separate
 * from the main web UI settings, and are exposed on the command line via the
 * `fileserver` sub-command, mirroring the other CLI tools.
 */
class FileServer final : public ITool {
  struct Settings {
    std::string bind = CONFIG_WEBUI_FILESERVER_BIND;
    int port = CONFIG_WEBUI_FILESERVER_PORT;
    std::string path = CONFIG_WEBUI_FILESERVER_PATH;
    bool allowSubDirs = CONFIG_WEBUI_FILESERVER_ALLOW_SUBDIRS;
    bool enableAutoindex = CONFIG_WEBUI_FILESERVER_ENABLE_AUTOINDEX;
    std::string cert = CONFIG_WEBUI_FILESERVER_CERT;
    std::string key = CONFIG_WEBUI_FILESERVER_KEY;
  };

  std::shared_ptr<spdlog::logger> logger;
  std::optional<args::Command> fileServerCommand;
  cmd::VarSnapshot varSnap; // preserves published variables across suppress()/invoke()

  Settings config;
  std::unique_ptr<httplib::Server> server;
  std::thread serverThread;
  std::mutex mtx;
  std::atomic<bool> running{false};
  uuids::uuid uuid;

  /// TLS is used only when both a certificate and a key are configured.
  bool tlsEnabled() const { return !config.cert.empty() && !config.key.empty(); }

  /// Build an httplib server (SSLServer when TLS is enabled) with the download
  /// routes installed for `root`. Returns nullptr if the server is invalid
  /// (e.g. the TLS material failed to load or the root does not exist).
  std::unique_ptr<httplib::Server> buildServer(const fs::path &root);

  /// Install the catch-all download/autoindex handler on `svr`.
  void installRoutes(httplib::Server &svr, const fs::path &root);

  // Variable plumbing for the interactive UI.
  void readVars();
  void publishVars();

  // Background (interactive) lifecycle.
  std::string startLocked();
  std::string stopLocked();
  std::string statusString();

  // Foreground (command-line) lifecycle: blocks until interrupted.
  std::string serveBlocking();

public:
  FileServer() = default;
  ~FileServer() override = default;

  const char *getName() const override { return "fileserver"; }
  const SemVer getVersion() const override { return {0, 1, 0}; }
  const std::vector<std::string> &getTags() const override {
    static std::vector<std::string> tags = {"webui", "http", "download", "server"};
    return tags;
  }

  void initialize(initArgs &) override;
  void invoke(std::string_view prefix, bool soft = false) override;
  void suppress() override;
  void execute() override;
  void shutdown() override;

  bool cmdCheck() override;

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  const uuids::uuid getUUID() override;
};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
