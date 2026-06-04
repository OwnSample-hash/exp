#pragma once

#include <cpr/cpr.h>
#include <interfaces/tool.hpp>
#include <memory>
#include <spdlog/logger.h>

using namespace explo;

#define _STR(x) #x
#define STR(x) _STR(x)

class CPR : public ITool {
  std::shared_ptr<spdlog::logger> logger;
  std::vector<std::string> tags;
  std::string payload;
  int status = 0;

public:
  CPR() = default;
  CPR(std::shared_ptr<spdlog::logger> logger) : logger(std::move(logger)) {};
  ~CPR() = default;
  const char *getName() const override { return "cpr"; }
  const char *getVersion() const override { return "0.0.1 with libcurl: " LIBCURL_VERSION " and cpr: " CPR_VERSION; }
  void initialize() override;
  void shutdown() override;
  void execute() override;
  void invoke(const std::string &prefix) override;
  void invoke(const char *prefix) override { this->invoke(std::string(prefix)); }
  void suppress() override;
  const std::vector<std::string> &getTags() const override { return tags; }

  bool isShared() const override { return true; }
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
