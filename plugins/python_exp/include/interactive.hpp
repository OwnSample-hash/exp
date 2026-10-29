#pragma once

#include <interfaces/tool.hpp>
#include <python3.14/Python.h>

using namespace explo;

class InteractivePythonTool final : public ITool {
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<args::Group> parser;
  std::vector<std::string> tags;
  bool ownsInterpreter = false;
  bool makeCommand;

  std::optional<args::Command> cmd;

public:
  const char *getName() const override { return "python"; }

  const SemVer getVersion() const override { return {1, 0, 0, "", "with-python-" PY_VERSION}; }

  void initialize(initArgs &) override;

  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void suppress() override;

  const std::vector<std::string> &getTags() const override { return tags; }

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  bool cmdCheck() override;

  const uuids::uuid getUUID() override { return uuids::uuid::from_string(INTERACTIVE_UUID).value(); }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
