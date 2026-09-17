#pragma once

#include <arc.hpp>
#include <cmd.hpp>
#include <httplib.h>
#include <interfaces/renderer.hpp>
#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

using namespace explo;

class PL_webui;

#define ACC_ARG(var) *(plugin.var.value())

class webui final : public IRenderer {
  std::shared_ptr<args::Group> group;
  std::optional<args::ValueFlag<std::string>> host;
  std::optional<args::ValueFlag<int>> port;
  std::optional<args::Flag> enableTLS;
  std::optional<args::ValueFlag<std::string>> certFile;
  std::optional<args::ValueFlag<std::string>> keyFile;

  std::shared_ptr<spdlog::logger> logger;
  std::unique_ptr<httplib::Server> server;
  std::thread fuse_thread, nouse_thread;
  arc::Arc arc;
  std::atomic<int> activeWebSocketConnections{0};
  std::atomic<bool> running{true};
  fs::path temp_dir = fs::temp_directory_path() / "webui_temp";
  bool basicInitDone = false;

  void ws_loop(const httplib::Request &req, httplib::ws::WebSocket &ws);

  void convert(json &response);

public:
  webui() {};
  ~webui() override = default;

  const char *getName() const override { return "webui"; }
  const char *getVersion() const override { return "0.1.0"; }

  void initialize(initArgs &) override;
  void shutdown() override;

  void runLoop() override;

  ModuleType getModuleType() const override { return ModuleType::RENDERER; }
};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
