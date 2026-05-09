#pragma once
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_cpr : public IPlugin {
  std::shared_ptr<spdlog::logger> logger;

public:
  PL_cpr() = default;
  ~PL_cpr() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
