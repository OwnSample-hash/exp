#pragma once

#include "tool.hpp"
#include <args.hxx>
#include <module.hpp>
#include <optional>
#include <plugin_interface.hpp>

using namespace explo;

class PL_webui final : public IPlugin {
  friend class webui;

  std::shared_ptr<args::Group> group;
  std::optional<args::ValueFlag<std::string>> host;
  std::optional<args::ValueFlag<int>> port;
  std::optional<args::Flag> enableTLS;
  std::optional<args::ValueFlag<std::string>> certFile;
  std::optional<args::ValueFlag<std::string>> keyFile;

public:
  PL_webui() = default;
  ~PL_webui() = default;
  const char *getName() const override { return "webui"; }
  const char *getVersion() const override { return "0.1.0"; }
  void initialize(initArgs &args) override;
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
