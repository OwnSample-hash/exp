#include <catch2/catch_test_macros.hpp>
#include <semver.hpp>

TEST_CASE("semver parsing and comparison", "[semver]") {
  using namespace explo;

  SECTION("Parsing valid semantic versions") {
    SemVer v1("1.2.3");
    REQUIRE(v1.major == 1);
    REQUIRE(v1.minor == 2);
    REQUIRE(v1.patch == 3);
    REQUIRE(v1.preRelease.empty());
    REQUIRE(v1.buildMetadata.empty());

    SemVer v2("1.2.3-alpha+001");
    REQUIRE(v2.major == 1);
    REQUIRE(v2.minor == 2);
    REQUIRE(v2.patch == 3);
    REQUIRE(v2.preRelease == "alpha");
    REQUIRE(v2.buildMetadata == "001");
  }

  SECTION("Comparing semantic versions") {
    SemVer v1("1.0.0-alpha");
    SemVer v2("1.0.0-alpha.1");
    SemVer v3("1.0.0-alpha.beta");
    SemVer v4("1.0.0-beta");
    SemVer v5("1.0.0-beta.2");
    SemVer v6("1.0.0-beta.11");
    SemVer v7("1.0.0-rc.1");
    SemVer v8("1.0.0");

    REQUIRE(v1 < v2);
    REQUIRE(v2 < v3);
    REQUIRE(v3 < v4);
    REQUIRE(v4 < v5);
    REQUIRE(v5 < v6);
    REQUIRE(v6 < v7);
    REQUIRE(v7 < v8);
    REQUIRE(v8 > v1);
  }
}