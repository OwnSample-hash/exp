#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "lib.hpp"
#include <iterator.hpp>
#include <multivalue.hpp>
#include <random>
#include <spdlog/spdlog.h>
#include <stdexcept>

using namespace explo;

const std::string base_code = R"(
local tbl = {{}}
local status, err = xpcall(function() tbl = {}"{}" end, debug.traceback)
if not status then error(err) end
return tbl
)";

struct LFW;
struct LTW;

// Variant defined AFTER both structs are complete (see bottom),
// but we need the name visible for the structs — so we forward-declare it too.
// Use a type alias defined after the structs instead, and return it via
// a deduced / out-of-line definition.

struct LFW {
  LFW(lua_State *L, int index = -1) : L(L) {
    luaL_checktype(L, index, LUA_TFUNCTION);
  }

  template <typename... Args> auto operator()(Args &&...args);

private:
  lua_State *L;

  void pushArg(lua_Number n) { lua_pushnumber(L, n); }
  void pushArg(const std::string &s) { lua_pushstring(L, s.c_str()); }
  void pushArg(bool b) { lua_pushboolean(L, b); }
};

struct LTW {
  LTW() {
    L = luaL_newstate();
    if (!L)
      throw std::runtime_error("Failed to create Lua state");
    luaL_openlibs(L);
    luaL_newlibtable(L, libs);
    luaL_setfuncs(L, libs, 0);
    lua_setglobal(L, "explo");
  }
  LTW(lua_State *L) : L(L) {}
  ~LTW() { lua_close(L); }

  auto operator[](const char *field, bool failIfNotFound = true);

  void load(const std::string &field, const std::string &file) {
    static auto logger = spdlog::get("lua_exp");
    if (!logger)
      throw std::runtime_error("Logger not found");
    std::string code =
        std::vformat(base_code, std::make_format_args("dofile ", file));
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      logger->error("Lua error: {}", err);
      throw std::runtime_error("Lua error: " + err);
    }
    luaL_checktype(L, -1, LUA_TFUNCTION);
    lua_getglobal(L, tableName.c_str());
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, field.c_str());
  }

  LTW &operator()(const std::string &payload) {
    static auto logger = spdlog::get("lua_exp");
    if (!logger)
      throw std::runtime_error("Logger not found");
    std::string code =
        std::vformat(base_code, std::make_format_args("require ", payload));
    logger->trace("Executing Lua code:\n{}", code);
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      logger->error("Lua error: {}", err);
      throw std::runtime_error("Lua error: " + err);
    }
    luaL_checktype(L, -1, LUA_TTABLE);
    tableIndex = lua_gettop(L);
    nameTable();
    return *this;
  }

  auto iterate();

private:
  lua_State *L;
  std::string tableName;
  int tableIndex;

  void nameTable() {
    constexpr char allowed_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
    std::random_device rd;
    std::mt19937 mt;
    mt.seed(rd());
    tableName.clear();
    for (int i = 0; i < 32; i++)
      tableName += allowed_chars[mt() % (sizeof(allowed_chars) - 1)];
    lua_setglobal(L, tableName.c_str());
  }

  LTW(lua_State *L, int index) : L(L) {
    luaL_checktype(L, index, LUA_TTABLE);
    tableIndex = index;
    nameTable();
  }
};

using lua_Vartype =
    MultiValue<std::monostate, lua_Number, std::string, bool, LFW, LTW>;

inline auto LTW::iterate() {
  std::vector<std::pair<std::string, lua_Vartype>> result;
  lua_getglobal(L, tableName.c_str());
  result.reserve(lua_rawlen(L, -1));
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    std::string k = lua_tostring(L, -2);
    switch (lua_type(L, -1)) {
    case LUA_TNUMBER:
      result.emplace_back(k, lua_Vartype(lua_tonumber(L, -1)));
      break;
    case LUA_TSTRING:
      result.emplace_back(k, lua_Vartype(std::string(lua_tostring(L, -1))));
      break;
    case LUA_TBOOLEAN:
      result.emplace_back(k, lua_Vartype(bool(lua_toboolean(L, -1))));
      break;
    case LUA_TTABLE:
      result.emplace_back(k, lua_Vartype(LTW(this->L, -1)));
      break;
    case LUA_TFUNCTION:
      result.emplace_back(k, lua_Vartype(LFW(L)));
      break;
    case LUA_TNIL:
    default:
      result.emplace_back(k, lua_Vartype());
      break;
    }
    lua_pop(L, 1);
  }
  return result;
}

template <typename... Args> auto LFW::operator()(Args &&...args) {
  lua_pushvalue(L, -1);
  (pushArg(std::forward<Args>(args)), ...);
  if (lua_pcall(L, sizeof...(Args), 1, 0) != LUA_OK) {
    std::string err = lua_tostring(L, -1);
    lua_pop(L, 1);
    throw std::runtime_error("Lua function call error: " + err);
  }
  // Optionally inspect the return value here; for now return monostate
  lua_pop(L, 1);
  return lua_Vartype{};
}

inline auto LTW::operator[](const char *field, bool failIfNotFound) {
  assert(!tableName.empty());
  lua_getglobal(L, tableName.c_str());
  lua_getfield(L, -1, field);
  lua_remove(L, -2);
  switch (lua_type(L, -1)) {
  case LUA_TNUMBER:
    return lua_Vartype(lua_tonumber(L, -1));
  case LUA_TSTRING:
    return lua_Vartype(std::string(lua_tostring(L, -1)));
  case LUA_TBOOLEAN:
    return lua_Vartype(bool(lua_toboolean(L, -1)));
  case LUA_TTABLE:
    return lua_Vartype(LTW(this->L, -1));
  case LUA_TFUNCTION:
    return lua_Vartype(LFW(L));
  case LUA_TNIL:
  default:
    return lua_Vartype();
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
