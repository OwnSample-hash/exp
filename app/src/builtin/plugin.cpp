#include <builtin/nc.hpp>
#include <builtin/simple_ui.hpp>
#include <plugin_interface.hpp>

static RendererRegistry::Add<explo::builtin::UI> UIRegister("simple_ui");
static ToolRegistry::Add<explo::builtin::NC> NCRegister("nc");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
