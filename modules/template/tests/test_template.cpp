// clang-format off
#include "../include/%s.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("%s module loads") { REQUIRE_NOTHROW(module_entry_%s()); }
// Vim: set expandtab tabstop=2 shiftwidth=2:
