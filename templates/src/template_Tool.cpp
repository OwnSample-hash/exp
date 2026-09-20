// clang-format off
#include <{CONFIG_NEW_MODULE_NAME}.hpp>

void {CONFIG_NEW_MODULE_NAME}::initialize(initArgs &args) {{}}

void {CONFIG_NEW_MODULE_NAME}::invoke(const std::string_view prefix, bool soft) {{
  this->prefix = prefix;
  // Handle the invocation of the tool here
}}

void {CONFIG_NEW_MODULE_NAME}::shutdown() {{}}

void {CONFIG_NEW_MODULE_NAME}::suppress() {{}}

void {CONFIG_NEW_MODULE_NAME}::execute() {{}}

static {CONFIG_NEW_MODULE_TYPE}Registry::Add<{CONFIG_NEW_MODULE_NAME}> {CONFIG_NEW_MODULE_NAME}Registry("{CONFIG_NEW_MODULE_NAME}");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
