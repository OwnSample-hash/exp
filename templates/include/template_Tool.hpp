// clang-format off
#pragma once
#include <interfaces/tool.hpp>
#include <module.hpp>
#include <plugin_interface.hpp>

using namespace explo;

class {CONFIG_NEW_MODULE_NAME} final : public ITool {{
public:
  {CONFIG_NEW_MODULE_NAME}() = default;
  ~{CONFIG_NEW_MODULE_NAME}() = default;
  const char *getName() const override {{ return "{CONFIG_NEW_MODULE_NAME}"; }}
  const char *getVersion() const override {{ return "0.0.1";}}
  const std::vector<std::string> &getTags() const override {{
    static std::vector<std::string> tags = {{"utility"}};
    return tags;
  }}

  void initialize(initArgs &) override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void shutdown() override;

  void suppress() override;

  void execute() override;

  ModuleType getModuleType() const override {{ return ModuleType::TOOL; }}
}};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
