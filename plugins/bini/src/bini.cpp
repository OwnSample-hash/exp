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
    args::PositionalList<std::string> biniArgsList(s, "args", "Arguments for bini");
    args::HelpFlag help(s, "help", "Display this help menu", {'h', "help"});
    s.Parse();
    this->biniArgs.assign(biniArgsList.begin(), biniArgsList.end());
    this->type = type.Get();
  });
  biniArgs_.emplace(*args.parser, "args", "Arguments for bini");
}

bool PL_bini::cmdCheck() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  if (biniCommand && !(*biniCommand)) {
    return false;
  }
  for (const auto &arg : biniArgs) {
    auto pos = arg.find('=');
    if (pos != std::string::npos) {
      std::string name = arg.substr(0, pos);
      std::string value = arg.substr(pos + 1);
      if (value.size() > 2 && value[0] == '%') {
        if (value[1] == 's')
          vars.set(name, cmd::VarValue(value.substr(2)));
        else if (value[1] == 'd')
          vars.set(name, cmd::VarValue(std::stoll(value.substr(2))));
        else if (value[1] == 'f')
          vars.set(name, cmd::VarValue(std::stod(value.substr(2))));
        else if (value[1] == 'b') {
          std::string val = value.substr(2);
          std::transform(val.begin(), val.end(), val.begin(), ::tolower);
          if (val == "true" || val == "1")
            vars.set(name, cmd::VarValue(true));
          else if (val == "false" || val == "0")
            vars.set(name, cmd::VarValue(false));
        }
      } else {
        vars.set(name, cmd::VarValue(value));
      }
    } else {
      std::cout << "\033[41mInvalid argument format: " << arg << "\033[0m" << std::endl;
    }
  }
  vars.set("bini.type", cmd::VarValue(type));
  biniInstance->initialize();
  biniInstance->invoke("bini", true);
  biniInstance->execute();
  biniInstance->suppress();
  biniInstance->shutdown();
  return true;
}

static PluginRegistry::Add<PL_bini> biniRegister("bini");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
