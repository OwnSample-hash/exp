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
  friend struct LTW;

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
  friend struct LFW;

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
  LTW(lua_State *L, int index) : L(L) {
    luaL_checktype(L, index, LUA_TTABLE);
    tableIndex = lua_absindex(L, index);
    lua_pushvalue(L, tableIndex);
    nameTable();
  }

  void close() {
    if (L) {
      lua_close(L);
      L = nullptr;
    }
  }

  int insert(auto value, const char *field);

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
    tableIndex = lua_absindex(L, -1);
    logger->trace("Lua code executed successfully, table index: {}",
                  tableIndex);
    lua_setglobal(L, "tool");
    return *this;
  }

  const auto iterate() const;

private:
  lua_State *L;
  std::string tableName = "tool";
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

  LTW(lua_State *L, int index, bool) : L(L) {
    luaL_checktype(L, index, LUA_TTABLE);
    tableIndex = lua_absindex(L, index);
    lua_pushvalue(L, tableIndex);
    nameTable();
  }
};

using lua_Vartype =
    MultiValue<std::monostate, lua_Number, std::string, bool, LFW, LTW>;

inline int LTW::insert(auto value, const char *field) {
  static_assert(std::is_same<decltype(value), lua_Vartype>::value,
                "Value must be of type lua_Vartype");
  lua_Vartype v = value;
  if (v.is<lua_Number>()) {
    lua_pushnumber(L, v.as<lua_Number>());
  } else if (v.is<std::string>()) {
    lua_pushstring(L, v.as<std::string>().c_str());
  } else if (v.is<bool>()) {
    lua_pushboolean(L, v.as<bool>());
  } else if (v.is<LFW>()) {
    return 2;
  } else if (v.is<LTW>()) {
    return 3;
  } else {
    lua_pushnil(L);
  }
  lua_setglobal(L, field);
  return 0;
}

inline const auto LTW::iterate() const {
  static auto logger = spdlog::get("lua_exp")->clone("lua_exp::lua::iter");
  logger->flush_on(spdlog::level::trace);
  std::unordered_map<std::string, lua_Vartype> result;
  lua_getglobal(L, tableName.c_str());
  int idx = lua_absindex(L, -1);
  result.reserve(lua_rawlen(L, -1));
  lua_pushnil(L);
  logger->trace("Iterating over table '{}'", tableName);
  while (lua_next(L, idx) != 0) {
    std::string k = lua_tostring(L, -2);
    logger->trace("Iterating key: {}", k);
    switch (lua_type(L, -1)) {
    case LUA_TNUMBER:
      result.emplace(k, lua_Vartype(lua_tonumber(L, -1)));
      logger->trace("Value is number: {}", result.at(k).as<lua_Number>());
      break;
    case LUA_TSTRING:
      result.emplace(k, lua_Vartype(std::string(lua_tostring(L, -1))));
      logger->trace("Value is string: '{}'", result.at(k).as<std::string>());
      break;
    case LUA_TBOOLEAN:
      result.emplace(k, lua_Vartype(bool(lua_toboolean(L, -1))));
      logger->trace("Value is boolean: {}",
                    result.at(k).as<bool>() ? "true" : "false");
      break;
    case LUA_TTABLE:
      result.emplace(k, lua_Vartype(LTW(this->L, -1)));
      logger->trace("Value is table (LTW)");
      break;
    case LUA_TFUNCTION:
      result.emplace(k, lua_Vartype(LFW(L)));
      logger->trace("Value is function (LFW)");
      break;
    case LUA_TNIL:
    default:
      result.emplace(k, lua_Vartype());
      logger->trace("Value is nil or unknown type");
      break;
    }
    lua_pop(L, 1);
    logger->trace("Finished processing key: {}", k);
  }
  return result;
}

static int traceback_handler(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg) {
    luaL_traceback(L, L, msg, 1);
  } else {
    lua_pushliteral(L, "(error object is not a string)");
  }
  return 1;
}

template <typename... Args> auto LFW::operator()(Args &&...args) {
  lua_pushvalue(L, -1);
  (pushArg(std::forward<Args>(args)), ...);
  int handler_index = lua_gettop(L) - sizeof...(Args) - 1;
  lua_pushcfunction(L, traceback_handler);
  lua_insert(L, handler_index);
  if (lua_pcall(L, sizeof...(Args), 1, handler_index) != LUA_OK) {
    std::string err = lua_tostring(L, -1);
    spdlog::get("lua_exp")->error("Lua function call error: {}", err);
    lua_pop(L, 1);
    throw std::runtime_error("Lua function call error: " + err);
  }
  lua_remove(L, handler_index);
  switch (lua_type(L, -1)) {
  case LUA_TNUMBER:
    return lua_Vartype(lua_tonumber(L, -1));
  case LUA_TSTRING:
    return lua_Vartype(std::string(lua_tostring(L, -1)));
  case LUA_TBOOLEAN:
    return lua_Vartype(bool(lua_toboolean(L, -1)));
  case LUA_TTABLE:
    return lua_Vartype(LTW(this->L, -1, true));
  case LUA_TFUNCTION:
    return lua_Vartype(LFW(L));
  case LUA_TNIL:
  default:
    return lua_Vartype{};
  }
  return lua_Vartype{};
}

inline auto LTW::operator[](const char *field, bool failIfNotFound) {
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
    return lua_Vartype(LTW(this->L, -1, true));
  case LUA_TFUNCTION:
    return lua_Vartype(LFW(L));
  case LUA_TNIL:
  default:
    return lua_Vartype();
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
