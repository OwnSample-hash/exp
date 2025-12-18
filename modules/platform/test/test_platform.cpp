#include "../include/platform.hpp"
#include <catch2/catch_test_macros.hpp>
TEST_CASE("Platform module reports name") { REQUIRE(platform_name != nullptr); }
// Vim: set expandtab tabstop=2 shiftwidth=2:
