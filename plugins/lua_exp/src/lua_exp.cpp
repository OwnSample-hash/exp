#include <loader.hpp>
#include <lua_exp.hpp>
#include <module.hpp>

const char *PL_lua_exp::getName() const { return "lua_exp"; }

const char *PL_lua_exp::getVersion() const { return "0.0.1"; }

void PL_lua_exp::initialize(initArgs &args) {
  logger = args.logger;
  logger->info("Initializing plugin: {}", getName());

  args.modules->emplace_back(
      "loader", ModuleType::TOOLPROVIDER,
      std::make_shared<luaLoader>(logger->clone(logger->name() + "::loader")));
}

static PluginRegistry::Add<PL_lua_exp> lua_expRegister("lua_exp");
// Vim: set expandtab tabstop=2 shiftwidth=2:
