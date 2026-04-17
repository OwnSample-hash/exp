#include <lua_exp.hpp>

const char *PL_lua_exp::getName() const { return "lua_exp"; }

const char *PL_lua_exp::getVersion() const { return "0.0.1"; }

void PL_lua_exp::initialize(initArgs &args) {
  // Register the modules
}

static PluginRegistry::Add<PL_lua_exp> lua_expRegister("lua_exp");
// Vim: set expandtab tabstop=2 shiftwidth=2:
