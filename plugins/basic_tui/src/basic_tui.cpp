#include <basic_tui.hpp>

const std::string PL_basic_tui::getName() const { return "basic_tui"; }

const std::string PL_basic_tui::getVersion() const { return "0.0.1"; }

void PL_basic_tui::initialize(initArgs &args) {
  // Register the display module
  args.modules->emplace_back("basic display module", explo::MODULE_TYPE_DISPLAY,
                             std::make_unique<basic_tui>(args.logger->clone(
                                 args.logger->name() + "::display")));

  // Store the logger for later use
  this->logger = args.logger;
  this->logger->info("Initialized plugin: {}", getName());
  this->logger->debug("Adding command-line arguments for plugin: {}",
                      getName());

  args.gParser->add_argument("--basic-tui-global-option")
      .help("An example global option for the basic_tui plugin")
      .default_value(std::string("default_global_value"));

  args.parser->add_argument("--basic-tui-option")
      .help("An example option for the basic_tui plugin")
      .default_value(std::string("default_value"));
}

void PL_basic_tui::execute() {
  // Plugin execution implementation
}

static PluginRegistry::Add<PL_basic_tui> basic_tuiRegister("basic_tui");

// Belongs to the module type(s): basic_tui

const char *basic_tui::getName() const { return "basic_tui"; }
const char *basic_tui::getVersion() const { return "0.1.0"; }

void basic_tui::initialize() {
  std::cout << "\33[H\33[2J\33[3J";
  logger->info("Initialize display mod");
}
void basic_tui::shutdown() {
  // Module shutdown implementation
}

basic_tui::basic_tui() {
  // Constructor implementation
}

basic_tui::~basic_tui() {
  // Deconstructor implementation
}

void basic_tui::clearScreen() {
  // Implementation to clear the screen
}

void basic_tui::updateScreen() {
  // Implementation to update the screen
}

int basic_tui::getWidth() const {
  // Implementation to get the width of the display
  return 0;
}

int basic_tui::getHeight() const {
  // Implementation to get the height of the display
  return 0;
}

void basic_tui::drawText(int x, int y, std::string_view text, Color fg,
                         Color bg) {
  // Implementation to draw text on the display
}

void basic_tui::onInputChar(std::function<void(char)> callback) {
  // Implementation to handle input characters
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
