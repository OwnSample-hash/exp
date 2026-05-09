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

#define luaLogFuncs                                                            \
  X(logt, trace)                                                               \
  X(logd, debug)                                                               \
  X(logi, info)                                                                \
  X(logw, warn)                                                                \
  X(loge, error)

#define luaSocketFuncs                                                         \
  X(socket_, socket)                                                           \
  X(connect_, connect)                                                         \
  X(write_, write)                                                             \
  X(read_, read)                                                               \
  X(close_, close)

#define luaFuncs                                                               \
  X(var)                                                                       \
  X(call)                                                                      \
  X(sleep)                                                                     \
  luaLogFuncs luaSocketFuncs

#define X(name, ...) int name(lua_State *L);
luaFuncs
#undef X

    const luaL_Reg libs[] = {
#define X(name, ...) {#name, name},
        luaFuncs
#undef X
#define X(fn, name) {#name, fn},
            luaSocketFuncs
#undef X
        {nullptr, nullptr},
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
