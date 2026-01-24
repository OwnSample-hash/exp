// clang-format off
#include <%s.hpp>

std::string_view PL_%s::getName() const {
  return "%s";
}

std::string_view PL_%s::getVersion() const {
  return %s;
}

void PL_%s::initialize(std::list<explo::Module> &modules) {
  // Register the display module
}

void PL_%s::execute() {
  // Plugin execution implementation
}

static PluginRegistry::Add<PL_%s> %sRegister(%s);
// Vim: set expandtab tabstop=2 shiftwidth=2:
