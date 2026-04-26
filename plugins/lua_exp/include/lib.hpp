#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

int log(lua_State *L);

const luaL_Reg libs[] = {
    {"log", log},
    {nullptr, nullptr},
};
