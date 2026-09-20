// clang-format off
#pragma once
#include <interfaces/tool_provider.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class {CONFIG_NEW_MODULE_NAME} final : public IToolProvider {{
std::map<std::string, std::shared_ptr<ITool>> Tools;

public:
  {CONFIG_NEW_MODULE_NAME}() = default;
  ~{CONFIG_NEW_MODULE_NAME}() = default;
  const char *getName() const override {{ return "{CONFIG_NEW_MODULE_NAME}"; }}
  const char *getVersion() const override {{ return "0.0.1";}}

  virtual const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override {{ return Tools;}};
  
  void initialize(initArgs &) override;

  void shutdown() override;

  ModuleType getModuleType() const override {{ return ModuleType::TOOLPROVIDER; }}
}};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
