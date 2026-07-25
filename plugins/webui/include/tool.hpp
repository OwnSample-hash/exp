#pragma once

#include "webui.hpp"
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
  std::shared_ptr<spdlog::logger> logger;
  std::unique_ptr<httplib::Server> server;
  const PL_webui &plugin;

  void ws_loop(const httplib::Request &req, httplib::ws::WebSocket &ws);

  void convert(json &response);

public:
  webui(std::shared_ptr<spdlog::logger> logger, const PL_webui &plugin) : logger(logger), plugin(plugin) {};
  ~webui() override = default;

  const char *getName() const override { return "webui"; }
  const char *getVersion() const override { return "0.1.0"; }

  void initialize() override;
  void shutdown() override;

  void runLoop() override;
};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
