#pragma once

#include <args.hxx>
#include <interfaces/tool.hpp>
#include <llib.hpp>
#include <lua.h>
#include <memory>
#include <spdlog/logger.h>
#include <utils.hpp>

using namespace std::literals;

using namespace explo;

class luaTool final : public ITool {
  std::shared_ptr<spdlog::logger> logger;
  std::string file;
  std::string name;
  std::string description;
  std::string version;
  std::vector<std::string> tags;
  lua_State *L;
  LTW lua;
  int lastStatus = 0;

public:
  ~luaTool() {
    if (L) {
      lua_close(L);
      L = nullptr;
    }
  }

  luaTool() = delete;
  luaTool(const luaTool &) = delete;
  luaTool(luaTool &&) = delete;

  luaTool &operator=(const luaTool &) = delete;
  luaTool &operator=(luaTool &&) = delete;

  luaTool(std::shared_ptr<spdlog::logger> logger, const std::string &file, std::shared_ptr<args::Group> parser)
      : logger(logger), file(file) {
    L = luaL_newstate();
    if (!L)
      throw std::runtime_error("Failed to create Lua state");
    lua = LTW(L);

    luaL_openlibs(L);
    luaL_newlibtable(L, libs);
    luaL_setfuncs(L, libs, 0);
    lua_setglobal(L, "explo");

    {
#define Z(name)                                                                                                        \
  lua_pushnumber(L, name);                                                                                             \
  lua_setfield(L, -2, #name);
#define Y(ns, ...)                                                                                                     \
  do {                                                                                                                 \
    using namespace ns;                                                                                                \
    lua_createtable(L, 0, ns::Count);                                                                                  \
    __VA_ARGS__                                                                                                        \
    lua_getglobal(L, "explo");                                                                                         \
    lua_pushvalue(L, -2);                                                                                              \
    lua_setfield(L, -2, #ns);                                                                                          \
  } while (0);
#define X(name, type)                                                                                                  \
  lua_push##type(L, name);                                                                                             \
  lua_setglobal(L, #name);
      enumData
#undef X
#undef Y
#undef Z
    }

    lua(file);
    name = lua["name"].as("Unnamed Lua Tool"s);
    version = lua["version"].as("0.1"s);
    description = lua["description"].as("No description provided."s);
    this->logger->info("Initialized Lua tool: {} v{}", name, version);
    int res = 0;
    if ((res = lua.insert("name", luaVartype{name}))) {
      this->logger->warn("Failed to insert 'name' into Lua table for tool '{}' "
                         "with error code {}",
                         name, res);
    }
    LTW desc;
    try {
      desc = lua["desc"].as<LTW>();
    } catch (std::bad_variant_access &e) {
      this->logger->warn("Failed to retrieve 'desc' table for tool '{}' with error: {}", name, e.what());
    }
  }

  const char *getName() const override { return name.c_str(); }
  const char *getVersion() const override { return version.c_str(); }
  const std::vector<std::string> &getTags() const override { return tags; }

  void initialize(initArgs &) override;

  void shutdown() override;

  void execute() override;

  void invoke(std::string_view prefix, bool soft = false) override;

  void suppress() override;

  ModuleType getModuleType() const override { return ModuleType::TOOL; }
};
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
