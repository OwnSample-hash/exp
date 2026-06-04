#pragma once

#include <interfaces/tool_provider.hpp>
#include <memory>
#include <multivalue.hpp>
#include <spdlog/logger.h>

extern "C" {
#include <lua.h>
}

#define STR_(x) #x
#define STR(x) STR_(x)

using namespace explo;

class luaLoader final : public IToolProvider {
  std::shared_ptr<spdlog::logger> logger;
  std::map<std::string, std::shared_ptr<ITool>> tools = {};

public:
  luaLoader() = default;
  luaLoader(const luaLoader &) = delete;
  luaLoader(std::shared_ptr<spdlog::logger> logger) : logger(std::move(logger)) {}
  ~luaLoader() = default;

  const char *getName() const override { return "loader"; }
  const char *getVersion() const override {
    return "0.0.1 with lua: v" STR(LUA_VERSION_MAJOR_N) "." STR(LUA_VERSION_MINOR_N) "." STR(LUA_VERSION_RELEASE_N);
  }

  void initialize() override;
  void shutdown() override;

  const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override { return tools; }
};

// Vim: set expandtab tabstop=2 shiftwidth=2:
