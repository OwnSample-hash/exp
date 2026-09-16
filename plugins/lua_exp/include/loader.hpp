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

class luaLoader final : public IToolProvider {
  std::shared_ptr<spdlog::logger> logger;
  std::map<std::string, std::shared_ptr<ITool>> tools = {};
  std::shared_ptr<args::Group> parser;

public:
  ~luaLoader() = default;
  explicit luaLoader(std::shared_ptr<args::Group> parser, std::shared_ptr<spdlog::logger> logger)
      : logger(std::move(logger)) {}
  luaLoader(const luaLoader &) = delete;
  luaLoader(luaLoader &&) = delete;

  luaLoader &operator=(const luaLoader &) = delete;
  luaLoader &operator=(luaLoader &&) = delete;

  const char *getName() const override { return "luaLoader"; }
  const char *getVersion() const override {
    return "0.0.1 with lua: v" STR(LUA_VERSION_MAJOR_N) "." STR(LUA_VERSION_MINOR_N) "." STR(LUA_VERSION_RELEASE_N);
  }

  void initialize() override;
  void shutdown() override;

  const std::map<std::string, std::shared_ptr<ITool>> &getTools() const override { return tools; }
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
