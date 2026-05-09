#include <cpr_tool.hpp>
#include <tool.hpp>

const char *PL_cpr::getName() const { return "cpr"; }

const char *PL_cpr::getVersion() const { return "0.0.1"; }

void PL_cpr::initialize(initArgs &args) {
  logger = args.logger;
  logger->info("Initializing CPR plugin");
  args.modules->emplace_back("cpr", explo::ModuleType::TOOL,
                             std::make_shared<CPR>(logger));
}

static PluginRegistry::Add<PL_cpr> cprRegister("cpr");
// Vim: set expandtab tabstop=2 shiftwidth=2:
