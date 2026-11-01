/**
 * @file encoder.hpp
 * @brief Payload encoder tool provider.
 *
 * Scans a directory of YAML encoder definitions and exposes each as an
 * individual tool. See the `encoders/` directory for the definition schema and
 * worked examples.
 */
#pragma once

#include <encoder_config.hpp>
#include <encoder_tool.hpp>

#include <interfaces/tool_provider.hpp>
#include <map>
#include <memory>
#include <module.hpp>
#include <optional>
#include <plugin_interface.hpp>
#include <spdlog/spdlog.h>

using namespace explo;

class encoder final : public IToolProvider {
  std::map<std::string, std::shared_ptr<ITool>> Tools;
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<args::Group> parser;
  std::optional<args::ValueFlag<std::string>> directory;
  std::optional<args::ValueFlag<std::string>> workDir;

public:
  encoder() = default;
  ~encoder() = default;

  const char *getName() const override { return "encoder"; }
  const SemVer getVersion() const override { return {"1.0.0"}; }

  const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override { return Tools; }

  void initialize(initArgs &) override;
  void shutdown() override;

  ModuleType getModuleType() const override { return ModuleType::TOOLPROVIDER; }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
