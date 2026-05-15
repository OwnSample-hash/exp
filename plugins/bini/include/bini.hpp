#pragma once
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_bini final : public IPlugin {
public:
  PL_bini() = default;
  ~PL_bini() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
