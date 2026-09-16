#include <args.hxx>
#include <bini.hpp>
#include <cmd.hpp>
#include <tool.hpp>

using namespace explo;

const char *PL_bini::getName() const { return "bini"; }

const char *PL_bini::getVersion() const { return "0.0.1"; }

void PL_bini::initialize(initArgs &args) {
  biniInstance = std::make_shared<bini>(args.logger);
  args.modules->emplace_back("bini", ModuleType::TOOL, biniInstance);
  biniCommand.emplace(*args.parser, "bini", "Binary info tool", [&](args::Subparser &s) {
    args::ValueFlag<std::string> type(s, "type", "Type of analysis to perform (summary, functions, strings)",
                                      {'t', "type"}, std::string("summary"));
    args::PositionalList<std::string> targetsList(s, "args", "Targets for bini");
    args::HelpFlag help(s, "help", "Display this help menu", {'h', "help"});
    s.Parse();
    this->targets.assign(targetsList.begin(), targetsList.end());
    this->type = type.Get();
  });
}

bool PL_bini::cmdCheck() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  if (biniCommand && !(*biniCommand)) {
    return false;
  }
  biniInstance->initialize();
  biniInstance->invoke("bini", true);
  vars.set("bini.type", cmd::VarValue(type));
  for (const auto &target : targets) {
    vars.set("bini.target", cmd::VarValue(target));
    biniInstance->execute();
  }
  biniInstance->suppress();
  biniInstance->shutdown();
  return true;
}

static PluginRegistry::Add<PL_bini> biniRegister("bini");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
