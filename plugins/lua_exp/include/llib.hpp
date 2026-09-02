#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <enums.hpp>
#include <interfaces/tool.hpp>
#include <sys/socket.h>

namespace ConnectionStatus = explo::ConnectionStatus;

#define luaLogFuncs                                                                                                    \
  X(trace, trace)                                                                                                      \
  X(debug, debug)                                                                                                      \
  X(info, info)                                                                                                        \
  X(warn, warn)                                                                                                        \
  X(error, error)

#define luaSocketFuncs                                                                                                 \
  X(socket_, socket)                                                                                                   \
  X(connect_, connect)                                                                                                 \
  X(write_, write)                                                                                                     \
  X(read_, read)                                                                                                       \
  X(close_, close)                                                                                                     \
  X(sconnect, sconnect)                                                                                                \
  X(swrite, swrite)                                                                                                    \
  X(sread, sread)                                                                                                      \
  X(sclose, sclose)

#define luaFuncs                                                                                                       \
  X(var, var)                                                                                                          \
  X(call, call)                                                                                                        \
  X(sleep, sleep)                                                                                                      \
  X(clock, clock)                                                                                                      \
  luaLogFuncs luaSocketFuncs

#define asyncLuaFuncs X(async_scan, async_scan)

#define enumData                                                                                                       \
  X(SOCK_STREAM, number)                                                                                               \
  X(SOCK_DGRAM, number)                                                                                                \
  X(AF_INET, number)                                                                                                   \
  X(AF_INET6, number)                                                                                                  \
  Y(ConnectionStatus, ConnectionStatuses)

#define X(name, ...) int name(lua_State *L);
luaFuncs asyncLuaFuncs
#undef X

    const luaL_Reg libs[] = {
#define X(fn, name) {#name, fn},
        luaFuncs asyncLuaFuncs
#undef X
        {nullptr, nullptr},
};

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
