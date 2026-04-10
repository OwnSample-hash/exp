// clang-format off
#pragma once
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_%s : public IPlugin {
public:
  PL_%s() = default;
  ~PL_%s() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
