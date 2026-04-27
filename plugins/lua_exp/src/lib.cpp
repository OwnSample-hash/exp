#include <cmd.hpp>
#include <lib.hpp>
#include <spdlog/spdlog.h>
#include <string>

int log(lua_State *L) {
  static auto logger = spdlog::get("lua_exp")->clone("lua_exp::log");
  int nargs = lua_gettop(L);
  std::string log_msg;
  for (int i = 1; i <= nargs; i++) {
    if (lua_isstring(L, i)) {
      log_msg += lua_tostring(L, i);
    } else {
      log_msg += "<non-string argument>";
    }
    if (i < nargs)
      log_msg += " ";
  }
  logger->info("[Lua] {}", log_msg);
  return 0; // Number of return values
}

int var(lua_State *L) {
  const char *env_var = luaL_checkstring(L, 1);
  auto var = explo::cmd::CommandProcessor::instance().vars().get(env_var);
  if (var) {
    lua_pushstring(L, var->toString().c_str());
  } else {
    lua_pushnil(L);
  }
  return 1;
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
