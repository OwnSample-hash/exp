// FIXME: Fix the tests so it uses the new module system to test
// #include "../include/lua_exp.hpp"
// #include <catch2/catch_test_macros.hpp>
//
// #ifdef MODULE_LUA_EXP_STATIC
//
// TEST_CASE("lua_exp module loads as static") { 
//   REQUIRE_NOTHROW(module_entry_lua_exp());
// }
//
// #endif // MODULE_STATIC
//
// #ifdef MODULE_LUA_EXP_DYNAMIC
//
// TEST_CASE("lua_exp module loads as dynamic") {
//   REQUIRE_NOTHROW(load_module_dynamic("lua_exp"));
// }
// #endif // MODULE_DYNAMIC
//
// Vim: set expandtab tabstop=2 shiftwidth=2:
