// clang-format off
#include "../include/%s.hpp"
#include <catch2/catch_test_macros.hpp>

#ifdef MODULE_%s_STATIC

TEST_CASE("%s module loads as static") { 
  REQUIRE_NOTHROW(module_entry_%s());
}

#endif // MODULE_STATIC

#ifdef MODULE_%s_DYNAMIC

TEST_CASE("%s module loads as dynamic") {
  REQUIRE_NOTHROW(load_module_dynamic("%s"));
}
#endif // MODULE_DYNAMIC

// Vim: set expandtab tabstop=2 shiftwidth=2:
