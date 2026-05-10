// clang-format off
#pragma once
#include <interfaces/tool.hpp>
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


class %s final : public ITool {
public:
  %s() = default;
  ~%s() = default;
  const char *getName() const override { return "%s"; }
  const char *getVersion() const override { return "%s";}
  const std::vector<std::string> &getTags() const override { 
    static std::vector<std::string> tags = {"utility"};
    return tags;
  }

  void initialize() override;

  void invoke(const std::string &prefix) override;

  void invoke(const char *prefix) override {
    this->invoke(std::string(prefix));
  }

  void shutdown() override;

  void suppress() override;

  void execute() override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
