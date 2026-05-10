// clang-format off
#include <%s.hpp>

const char *PL_%s::getName() const {
  return "%s";
}

const char *PL_%s::getVersion() const {
  return %s;
}

void PL_%s::initialize(initArgs &args) {
  // Register modules, commands, etc. here
  args.modules->emplace_back("%s", ModuleType::TOOL,
                             std::make_shared<%s>());
}

static PluginRegistry::Add<PL_%s> %sRegister("%s");

void %s::initialize() {
  // Initialize the tool here
}

void %s::shutdown() {
  // Clean up resources here
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
