#pragma once
#include <interfaces/tool_provider.hpp>
#include <memory>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <python3.14/Python.h>
#include <python_exp_config.hpp>
#include <spdlog/spdlog.h>

using namespace explo;

class python_exp final : public IToolProvider {
  std::map<std::string, std::shared_ptr<ITool>> Tools;
  std::shared_ptr<spdlog::logger> logger;
  std::shared_ptr<args::Group> parser;
  std::optional<args::ValueFlag<std::string>> directory;
  std::optional<args::ValueFlag<std::string>> lib_dir;

public:
  python_exp() = default;
  ~python_exp() = default;
  const char *getName() const override { return "python_exp"; }
  const SemVer getVersion() const override { return {1, 0, 0, "", "with-python-" PY_VERSION}; }

  virtual const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override { return Tools; };

  void initialize(initArgs &) override;

  void shutdown() override;

  ModuleType getModuleType() const override { return ModuleType::TOOLPROVIDER; }

  const std::vector<Dependency> &getDependencies() override {
    static const std::vector<Dependency> deps{
        Dependency{
            "python",
            LoadOrder::BEFORE,
            {
                {
                    {1, 0, 0},
                    VersionComparison::GREATER_EQUAL,
                },
            },
        },
    };
    return deps;
  };
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
