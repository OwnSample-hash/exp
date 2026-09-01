#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <filesystem>
#include <iterator.hpp>
#include <lib.hpp>
#include <lua_exp_config.hpp>
#include <multivalue.hpp>
#include <random>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

using namespace explo;
namespace fs = std::filesystem;

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
  std::shared_ptr<spdlog::logger> logger = spdlog::get("lua_exp")->clone("lua_exp::lua::LFW");

  LFW(lua_State *L, int index = -1) : L(L) { luaL_checktype(L, index, LUA_TFUNCTION); }

  template <typename... Args> auto operator()(Args &&...args);

private:
  lua_State *L;

  void pushArg(lua_Number n) { lua_pushnumber(L, n); }
  void pushArg(const std::string &s) { lua_pushstring(L, s.c_str()); }
  void pushArg(bool b) { lua_pushboolean(L, b); }
};

struct LTW {
  friend struct LFW;
  std::shared_ptr<spdlog::logger> logger = spdlog::get("lua_exp")->clone("lua_exp::lua::LTW");

  void mark4GC() {
    lua_pushnil(L);
    lua_setglobal(L, tableName.c_str());
    assert(lua_isnil(L, lua_getglobal(L, tableName.c_str()) && "Failed to mark table for GC"));
    tableIndex = LUA_NOREF;
    tableName.clear();
    L = nullptr;
  }

  LTW() {}
  LTW(lua_State *L) : L(L) {}
  LTW(lua_State *L, int index) : L(L) {
    luaL_checktype(L, index, LUA_TTABLE);
    tableIndex = lua_absindex(L, index);
    lua_pushvalue(L, tableIndex);
    nameTable();
  }

  int insert(const char *field, auto value);

  auto operator[](const char *field, bool failIfNotFound = true);

  void load(const std::string &field, const std::string &file) {
    static auto logger = spdlog::get("lua_exp");
    if (!logger)
      throw std::runtime_error("Logger not found");
    std::string code = std::vformat(base_code, std::make_format_args("dofile ", file));
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
    logger->trace("Loading Lua payload: {}", payload);
    fs::path path(payload);
    if (path.is_relative())
      path = fs::current_path() / path;
    path = path.lexically_normal();
    fs::path libDir = fs::current_path() / CONFIG_LUA_EXP_LIB_DIR;
    if (!fs::exists(libDir) || !fs::is_directory(libDir)) {
      logger->error("Lua library directory does not exist or is not a directory: {}", libDir.string());
      throw std::runtime_error("Lua library directory does not exist or is not a directory: " + libDir.string());
    }
    libDir = libDir.lexically_normal();
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    std::string current_path = lua_tostring(L, -1);
    lua_pop(L, 1);
    std::string new_path = current_path + ";" + libDir.string() + "/?.lua;" + libDir.string() + "/?/init.lua;" +
                           path.string() + "/?.lua;" + path.string() + "/?/init.lua";
    lua_pushstring(L, new_path.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    std::string code = std::vformat(base_code, std::make_format_args("require ", payload));
    logger->trace("Executing Lua code:\n{}", code);
    if (luaL_dostring(L, code.c_str()) != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      logger->error("Lua error: {}", err);
      throw std::runtime_error("Lua error: " + err);
    }
    luaL_checktype(L, -1, LUA_TTABLE);
    tableIndex = lua_absindex(L, -1);
    logger->trace("Lua code executed successfully, table index: {}", tableIndex);
    lua_setglobal(L, "tool");
    return *this;
  }

  const auto iterate() const;

private:
  lua_State *L;
  std::string tableName = "tool";
  int tableIndex;

  void nameTable() {
    constexpr char allowed_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
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

using luaVartype = MultiValue<std::monostate, lua_Number, std::string, bool, LFW, LTW>;

inline int LTW::insert(const char *field, auto value) {
  static_assert(std::is_same<decltype(value), luaVartype>::value, "Value must be of type lua_Vartype");
  luaVartype v = value;
  logger->trace("Inserting field '{}'", field);
  if (v.is<lua_Number>()) {
    logger->trace("Value is number: {}", v.as<lua_Number>());
    lua_pushnumber(L, v.as<lua_Number>());
  } else if (v.is<std::string>()) {
    logger->trace("Value is string: '{}'", v.as<std::string>());
    lua_pushstring(L, v.as<std::string>().c_str());
  } else if (v.is<bool>()) {
    lua_pushboolean(L, v.as<bool>());
  } else if (v.is<LFW>()) {
    return 2;
  } else if (v.is<LTW>()) {
    return 3;
  } else {
    logger->warn("Value is nil or unknown type");
    lua_pushnil(L);
  }
  lua_setglobal(L, field);
  return 0;
}

inline const auto LTW::iterate() const {
  std::unordered_map<std::string, luaVartype> result;
  lua_getglobal(L, tableName.c_str());
  int idx = lua_absindex(L, -1);
  result.reserve(lua_rawlen(L, -1));
  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    // lua_tostring can convert the value at the given index in place (e.g.
    // turning a numeric key into a string). Doing that to the key lua_next
    // is currently tracking corrupts it for the next lua_next call, so
    // stringify a duplicate instead of the key itself.
    lua_pushvalue(L, -2);
    std::string k = lua_tostring(L, -1);
    lua_pop(L, 1); // drop the duplicated key

    // Snapshot the stack before touching the value. Wrapping a table value
    // below constructs a nested LTW, which pushes a copy of the table and
    // stores it in a fresh global (nameTable) — if that or any future
    // branch here ever leaves something extra on the stack, it would
    // desync the lua_pop/lua_next pairing below. lua_settop resets us back
    // to exactly [key, value] no matter what happened in the branch.
    int value_top = lua_gettop(L);
    switch (lua_type(L, -1)) {
    case LUA_TNUMBER:
      result.emplace(k, luaVartype(lua_tonumber(L, -1)));
      break;
    case LUA_TSTRING:
      result.emplace(k, luaVartype(std::string(lua_tostring(L, -1))));
      break;
    case LUA_TBOOLEAN:
      result.emplace(k, luaVartype(bool(lua_toboolean(L, -1))));
      break;
    case LUA_TTABLE:
      result.emplace(k, luaVartype(LTW(this->L, -1)));
      break;
    case LUA_TFUNCTION:
      result.emplace(k, luaVartype(LFW(L)));
      break;
    case LUA_TNIL:
    default:
      result.emplace(k, luaVartype());
      break;
    }
    lua_settop(L, value_top); // restore to exactly [key, value]
    lua_pop(L, 1);            // drop value, leaving key for lua_next
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
  if (lua_pcall(L, sizeof...(Args), LUA_MULTRET, handler_index) != LUA_OK) {
    std::string err = lua_tostring(L, -1);
    spdlog::get("lua_exp")->error("Lua function call error: {}", err);
    lua_pop(L, 1);
    throw std::runtime_error("Lua function call error: " + err);
  }
  // Everything from handler_index+1 to the current top is a return value.
  int nresults = lua_gettop(L) - handler_index;
  lua_remove(L, handler_index);
  // After removing the handler, the results occupy [first_result, first_result + nresults - 1].
  int first_result = handler_index;

  std::vector<luaVartype> results;
  results.reserve(nresults);
  for (int i = 0; i < nresults; ++i) {
    int idx = first_result + i;
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER:
      results.emplace_back(lua_tonumber(L, idx));
      break;
    case LUA_TSTRING:
      results.emplace_back(std::string(lua_tostring(L, idx)));
      break;
    case LUA_TBOOLEAN:
      results.emplace_back(bool(lua_toboolean(L, idx)));
      break;
    case LUA_TTABLE:
      results.emplace_back(LTW(this->L, idx, true));
      break;
    case LUA_TFUNCTION:
      // LFW expects the function value to be on top of the stack (index -1),
      // so push a duplicate of it there before wrapping it.
      lua_pushvalue(L, idx);
      results.emplace_back(LFW(L));
      break;
    case LUA_TNIL:
    default:
      results.emplace_back(luaVartype{});
      break;
    }
  }
  return results;
}

inline auto LTW::operator[](const char *field, bool failIfNotFound) {
  lua_getglobal(L, tableName.c_str());
  lua_getfield(L, -1, field);
  lua_remove(L, -2);
  switch (lua_type(L, -1)) {
  case LUA_TNUMBER:
    return luaVartype(lua_tonumber(L, -1));
  case LUA_TSTRING:
    return luaVartype(std::string(lua_tostring(L, -1)));
  case LUA_TBOOLEAN:
    return luaVartype(bool(lua_toboolean(L, -1)));
  case LUA_TTABLE:
    return luaVartype(LTW(this->L, -1, true));
  case LUA_TFUNCTION:
    return luaVartype(LFW(L));
  case LUA_TNIL:
  default:
    return luaVartype();
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
