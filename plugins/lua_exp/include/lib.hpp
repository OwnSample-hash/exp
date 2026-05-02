#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <interfaces/tool.hpp>
#include <map>
#include <memory>
#include <string>

extern std::map<std::string, std::shared_ptr<explo::ITool>> tools;

#define luaFuncs                                                               \
  X(log)                                                                       \
  X(var)                                                                       \
  X(call)

#define X(name) int name(lua_State *L);
luaFuncs
#undef X

    const luaL_Reg libs[] = {
#define X(name) {#name, name},
        luaFuncs
#undef X
        {nullptr, nullptr},
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
