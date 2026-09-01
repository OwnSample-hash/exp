#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <interfaces/tool.hpp>
#include <sys/socket.h>

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

#define ConnectionStatuses                                                                                             \
  Z(Open)                                                                                                              \
  Z(OpenUntested)                                                                                                      \
  Z(Filtered)                                                                                                          \
  Z(Error)                                                                                                             \
  Z(Timeout)                                                                                                           \
  Z(Refused)                                                                                                           \
  Z(Reset)                                                                                                             \
  Z(Closed)                                                                                                            \
  Z(Aborted)                                                                                                           \
  Z(NetReset)                                                                                                          \
  Z(HostUnreachable)                                                                                                   \
  Z(NetworkUnreachable)

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

namespace ConnectionStatus {
enum Type {
#define Z(name) name,
  ConnectionStatuses
#undef Z
      Count
};
inline const char *toString(Type status) {
  switch (status) {
#define Z(name)                                                                                                        \
  case name:                                                                                                           \
    return #name;
    ConnectionStatuses
#undef Z
        default : return "Unknown";
  }
}
} // namespace ConnectionStatus

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
