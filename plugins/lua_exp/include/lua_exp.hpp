#pragma once
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_lua_exp : public IPlugin {
public:
  PL_lua_exp() = default;
  ~PL_lua_exp() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
