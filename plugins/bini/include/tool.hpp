#pragma once

#include <interfaces/tool.hpp>
#include <memory>
#include <spdlog/spdlog.h>

using namespace explo;

class bini final : public ITool {
  std::shared_ptr<spdlog::logger> logger;

public:
  bini() = default;
  bini(std::shared_ptr<spdlog::logger> logger) : logger(std::move(logger)) {}
  ~bini() = default;
  const char *getName() const override { return "bini"; }
  const char *getVersion() const override { return "0.0.1"; }
  const std::vector<std::string> &getTags() const override {
    static std::vector<std::string> tags = {"utility"};
    return tags;
  }

  void initialize() override;

  void invoke(const std::string &prefix) override;

  void invoke(const char *prefix) override { this->invoke(std::string(prefix)); }

  void shutdown() override;

  void suppress() override;

  void execute() override;
};
