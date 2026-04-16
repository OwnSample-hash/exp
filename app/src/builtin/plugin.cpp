#include <builtin/plugin.hpp>
#include <builtin/simple_ui.hpp>
#include <module.hpp>

namespace explo {
namespace builtin {

void BuiltinPlugin::initialize(initArgs &args) {
  this->logger = args.logger;
  logger->info("Initializing builtin plugin...");

  args.modules->emplace_back("simple_ui",
                             explo::ModuleType::MODULE_TYPE_RENDERER,
                             std::make_unique<UI>(args.logger->clone(
                                 args.logger->name() + "::simple_ui")));
};

} // namespace builtin
} // namespace explo

static PluginRegistry::Add<explo::builtin::BuiltinPlugin>
    BuiltinPluginRegister("builtin");

// Vim: set expandtab tabstop=2 shiftwidth=2:
