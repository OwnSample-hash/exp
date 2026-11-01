/**
 * @file encoder_tool.hpp
 * @brief An ITool wrapper around one YAML-described encoder.
 *
 * Each EncoderTool surfaces its encoder's inputs as namespaced command
 * variables (set via `use`/`set`) and runs the encoder's pipeline on `run`.
 */
#pragma once

#include <encoder_def.hpp>

#include <filesystem>
#include <interfaces/tool.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

using namespace explo;

class EncoderTool final : public ITool {
  enc::EncoderDef def;
  std::shared_ptr<spdlog::logger> logger;
  std::filesystem::path workDir;
  std::string prefix;
  std::vector<std::string> tags;
  std::vector<std::string> missingRequirements;
  uuids::uuid uuid;
  std::string lastResult;
  bool valid = false;

public:
  EncoderTool(enc::EncoderDef definition, std::shared_ptr<spdlog::logger> logger, std::filesystem::path workDir);
  ~EncoderTool() override = default;

  const char *getName() const override { return def.name.c_str(); }
  const SemVer getVersion() const override { return {def.version}; }
  const std::vector<std::string> &getTags() const override { return tags; }
  const uuids::uuid getUUID() override { return uuid; }

  void initialize(initArgs &) override;
  void invoke(std::string_view prefix, bool soft = false) override;
  void suppress() override;
  void execute() override;
  void shutdown() override;

  ModuleType getModuleType() const override { return ModuleType::TOOL; }

  bool isValid() const { return valid; }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
