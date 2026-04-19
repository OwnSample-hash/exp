#pragma once

#include "plugin_interface.hpp"

namespace explo {
namespace builtin {

class BuiltinPlugin final : public IPlugin {
  std::shared_ptr<spdlog::logger> logger;

public:
  BuiltinPlugin() = default;
  ~BuiltinPlugin() = default;
  const char *getName() const override { return "builtin"; }
  const char *getVersion() const override { return "1.0.0"; }
  void initialize(initArgs &args) override;
};

} // namespace builtin
} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2:
