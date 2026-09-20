// clang-format off
#pragma once
#include <interfaces/renderer.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class {CONFIG_NEW_MODULE_NAME} final : public IRenderer {{
public:
  {CONFIG_NEW_MODULE_NAME}() {{}};
  ~{CONFIG_NEW_MODULE_NAME}() override {{}};

  const char *getName() const override {{ return "{CONFIG_NEW_MODULE_NAME}"; }};
  const char *getVersion() const override {{ return "0.0.1"; }};

  void initialize(initArgs &) override;

  void shutdown() override;

  void runLoop() override;

  ModuleType getModuleType() const override {{ return ModuleType::RENDERER; }}
}};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
