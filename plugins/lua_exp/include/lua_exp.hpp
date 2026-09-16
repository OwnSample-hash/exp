#pragma once
#include <memory>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_lua_exp final : public IPlugin {
  std::shared_ptr<spdlog::logger> logger;

public:
  PL_lua_exp() = default;
  ~PL_lua_exp() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
