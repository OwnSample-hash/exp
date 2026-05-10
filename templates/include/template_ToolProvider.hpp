// clang-format off
#pragma once
#include <interfaces/tool_provider.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class PL_%s final : public IPlugin {
public:
  PL_%s() = default;
  ~PL_%s() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
};


class %s final : public IToolProvider {
public:
  %s() = default;
  ~%s() = default;
  const char *getName() const override { return "%s"; }
  const char *getVersion() const override { return "%s";}

  virtual const std::map<std::string, std::shared_ptr<ITool>> & 
  getTools() const = 0;
  
  void initialize() override;

  void shutdown() override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
