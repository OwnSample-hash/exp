// clang-format off
#include <%s.hpp>

const char *PL_%s::getName() const {
  return "%s";
}

const char *PL_%s::getVersion() const {
  return %s;
}

void PL_%s::initialize(initArgs &args) {
  // Register the display module
}

void PL_%s::execute() {
  // Plugin execution implementation
}

static PluginRegistry::Add<PL_%s> %sRegister("%s");
// Vim: set expandtab tabstop=2 shiftwidth=2:
