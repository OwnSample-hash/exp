#pragma once

#include <args.hxx>
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

class luaExp final : public IToolProvider {
  std::shared_ptr<spdlog::logger> logger;
  std::map<std::string, std::shared_ptr<ITool>> tools = {};
  std::shared_ptr<args::Group> parser;

public:
  ~luaExp() = default;
  explicit luaExp() {}
  luaExp(const luaExp &) = delete;
  luaExp(luaExp &&) = delete;

  luaExp &operator=(const luaExp &) = delete;
  luaExp &operator=(luaExp &&) = delete;

  const char *getName() const override { return "lua_exp"; }
  const char *getVersion() const override {
    return "0.0.1 with lua: v" STR(LUA_VERSION_MAJOR_N) "." STR(LUA_VERSION_MINOR_N) "." STR(LUA_VERSION_RELEASE_N);
  }

  void initialize(initArgs &) override;
  void shutdown() override;

  ModuleType getModuleType() const override { return ModuleType::TOOLPROVIDER; }

  const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override { return tools; }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
