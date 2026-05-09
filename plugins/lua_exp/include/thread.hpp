#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

class LuaThread {
  lua_State *L;
  int threadRef;

public:
  LuaThread(lua_State *L) : L(L) {
    lua_pushthread(L);
    threadRef = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  ~LuaThread() { luaL_unref(L, LUA_REGISTRYINDEX, threadRef); }
};

const luaL_Reg libs[] = {
    {nullptr, nullptr},
};
// Vim: set expandtab tabstop=2 shiftwidth=2:
