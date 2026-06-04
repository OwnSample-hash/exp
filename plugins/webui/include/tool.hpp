#pragma once

#include <interfaces/renderer.hpp>
#include <memory>

using namespace explo;

class webui final : public IRenderer {
  std::shared_ptr<spdlog::logger> logger;

public:
  webui(std::shared_ptr<spdlog::logger> logger) : logger(logger) {};
  ~webui() override = default;

  const char *getName() const override { return "webui"; }
  const char *getVersion() const override { return "0.1.0"; }

  void initialize() override;
  void shutdown() override;

  void runLoop() override;
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
