// clang-format off
#pragma once
#include <interfaces/renderer.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_%s final : public IPlugin {
public:
  PL_%s() = default;
  ~PL_%s() = default;
  const char* getName() const override;
  const char* getVersion() const override;
  void initialize(initArgs &args) override;
};

class %s final : public IRenderer {
public:
  %s();
  ~%s() override;

  const char *getName() const override;
  const char *getVersion() const override;

  void initialize() override;
  void shutdown() override;

  void runLoop() override;

};
// Vim: set expandtab tabstop=2 shiftwidth=2:
