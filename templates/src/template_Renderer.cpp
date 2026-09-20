// clang-format off
#include <{CONFIG_NEW_MODULE_NAME}.hpp>

void {CONFIG_NEW_MODULE_NAME}::initialize(initArgs &args) {{}}

void {CONFIG_NEW_MODULE_NAME}::shutdown() {{}}

void {CONFIG_NEW_MODULE_NAME}::runLoop() {{}}

static {CONFIG_NEW_MODULE_TYPE}Registry::Add<{CONFIG_NEW_MODULE_NAME}> {CONFIG_NEW_MODULE_NAME}Registry("{CONFIG_NEW_MODULE_NAME}");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
