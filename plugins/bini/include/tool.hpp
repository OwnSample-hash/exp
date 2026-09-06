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
  void invoke(std::string_view prefix, bool soft = false) override;

  void shutdown() override;

  void suppress() override;

  void execute() override;
};
