#include <cmd.hpp>
#include <cmd/variable.hpp>
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
  if (!var) {
    lua_pushnil(L);
    return 1;
  }
  switch (var->type) {
  case explo::cmd::VarType::String:
    lua_pushstring(L, var->toString().c_str());
  case explo::cmd::VarType::Integer:
    lua_pushinteger(L, var->toInt());
    break;
  case explo::cmd::VarType::Float:
    lua_pushnumber(L, var->fval);
    break;
  case explo::cmd::VarType::Bool:
    lua_pushboolean(L, var->toBool());
    break;
  case explo::cmd::VarType::Array: {
    static auto logger = spdlog::get("lua_exp")->clone("lua_exp::log");
    logger->warn("Lua attempted to access array variable '{}', which is not "
                 "directly supported. Returning nil.",
                 env_var);
    lua_pushnil(L);
    break;
  }
  }
  return 1;
}

int call(lua_State *L) {
  const char *cmd_name = luaL_checkstring(L, 1);
  std::vector<std::string> args;
  int nargs = lua_gettop(L);
  for (int i = 2; i <= nargs; i++) {
    if (lua_isstring(L, i)) {
      args.push_back(lua_tostring(L, i));
    } else {
      args.push_back("<non-string argument>");
    }
  }
  lua_pushstring(L, "<s>");
  return 1;
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
