// clang-format off
#include <%s.hpp>

const char* PL_%s::getName() const {
  return "%s";
}

const char* PL_%s::getVersion() const {
  return %s;
}

void PL_%s::initialize(initArgs &args) {
  // Register the display module
  args.modules->emplace_back(%s, explo::ModuleType::RENDERER, std::make_unique<%s>());
}

static PluginRegistry::Add<PL_%s> %sRegister("%s");

// Belongs to the module type(s): %s

const char *%s::getName() const {
  return "%s";
}
const char *%s::getVersion() const {
  return %s;
}

void %s::initialize() {
  // Module initialization implementation
}
void %s::shutdown() {
  // Module shutdown implementation
}

%s::%s() {
  // Constructor implementation
}

%s::~%s() {
  // Deconstructor implementation
}

void %s::runLoop() {
  // Implementation to clear the screen
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
