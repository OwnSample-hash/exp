#include "../include/example.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Example module loads") { REQUIRE_NOTHROW(module_entry()); }
// Vim: set expandtab tabstop=2 shiftwidth=2:
