#pragma once

#include "interfaces/tool_provider.hpp"
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <memory>
#include <spdlog/logger.h>

#define STR_(x) #x
#define STR(x) STR_(x)

using namespace explo;

class luaLoader final : public IToolProvider {
  std::shared_ptr<spdlog::logger> logger;

public:
  luaLoader() = default;
  luaLoader(const luaLoader &) = delete;
  luaLoader(std::shared_ptr<spdlog::logger> logger)
      : logger(std::move(logger)) {
    this->logger->info("Initializing loader {}...", getVersion());
  }
  ~luaLoader() = default;

  const char *getName() const override { return "loader"; }
  const char *getVersion() const override {
    return "0.0.1 with lua: v" STR(LUA_VERSION_MAJOR_N) "." STR(
        LUA_VERSION_MINOR_N) "." STR(LUA_VERSION_RELEASE_N);
  }

  void initialize() override;
  void shutdown() override;
};

class LuaWrapper {
  lua_State *L;

public:
  using LuaFunction = std::function<int(lua_State *)>;

  LuaWrapper() {
    L = luaL_newstate();
    luaL_openlibs(L);
  }

  ~LuaWrapper() {
    if (L) {
      lua_close(L);
    }
  }

  // Add methods to interact with the Lua state as needed
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
