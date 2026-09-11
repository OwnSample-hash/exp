#pragma once
#include <args.hxx>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <tool.hpp>

using namespace explo;

class PL_bini final : public IPlugin {
  std::shared_ptr<bini> biniInstance;
  std::optional<args::Command> biniCommand;
  std::optional<args::PositionalList<std::string>> biniArgs_;
  std::vector<std::string> biniArgs;
  std::string type;

public:
  PL_bini() = default;
  ~PL_bini() = default;
  const char *getName() const override;
  const char *getVersion() const override;
  void initialize(initArgs &args) override;
  bool cmdCheck() override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
