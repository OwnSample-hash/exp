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
  modules.emplace_back(%s, explo::MODULE_TYPE_DISPLAY, std::make_unique<%s>());
}

void PL_%s::execute() {
  // Plugin execution implementation
}

static PluginRegistry::Add<PL_%s> %sRegister(%s);

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

void %s::clearScreen() {
  // Implementation to clear the screen
}

void %s::updateScreen() {
  // Implementation to update the screen
}

int %s::getWidth() const {
  // Implementation to get the width of the display
  return 0;
}

int %s::getHeight() const {
  // Implementation to get the height of the display
  return 0;
}

void %s::drawText(int x, int y, std::string_view text, Color fg, Color bg) {
  // Implementation to draw text on the display
}

void %s::onInputChar(std::function<void(char)> callback) {
  // Implementation to handle input characters
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
