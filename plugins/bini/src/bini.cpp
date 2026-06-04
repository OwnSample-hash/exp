#include <bini.hpp>
#include <tool.hpp>

const char *PL_bini::getName() const { return "bini"; }

const char *PL_bini::getVersion() const { return "0.0.1"; }

void PL_bini::initialize(initArgs &args) {
  args.modules->emplace_back("bini", ModuleType::TOOL, std::make_shared<bini>(args.logger));
}

static PluginRegistry::Add<PL_bini> biniRegister("bini");

// Vim: set expandtab tabstop=2 shiftwidth=2:
