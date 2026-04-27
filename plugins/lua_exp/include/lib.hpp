#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

int log(lua_State *L);

int var(lua_State *L);

const luaL_Reg libs[] = {
    {"log", log},
    {"var", var},
    {nullptr, nullptr},
};
