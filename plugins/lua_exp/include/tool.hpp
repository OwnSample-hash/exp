#pragma once

#include <interfaces/tool.hpp>
#include <memory>
#include <spdlog/logger.h>
#include <utils.hpp>

using namespace explo;

class luaTool final : public ITool {
  std::shared_ptr<spdlog::logger> logger;
  std::string file;
  std::string name;
  std::string description;
  std::string version;
  std::vector<std::string> tags;
  LTW lua;

public:
  ~luaTool() {}

  luaTool() = delete;
  luaTool(const luaTool &) = delete;
  luaTool(std::shared_ptr<spdlog::logger> logger, const std::string &file)
      : logger(std::move(logger)), file(file) {
    lua(file);
    name = lua["name"].as<std::string>("Unnamed Lua Tool");
    version = lua["version"].as<std::string>("0.1");
    description =
        lua["description"].as<std::string>("No description provided.");
    this->logger->info("Initialized Lua tool: {} v{}", name, version);
  }

  const char *getName() const override { return name.c_str(); }
  const char *getVersion() const override { return version.c_str(); }
  const std::vector<std::string> &getTags() const override { return tags; }

  void initialize() override;

  void shutdown() override;

  void execute() override;
};
