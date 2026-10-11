#include <catch2/catch_test_macros.hpp>
#include <dep.hpp>

#include "../src/dep.cpp" // Include the implementation file to test private members

TEST_CASE("DependencyTreeBuilder resolves dependencies correctly", "[dep]") {
  using namespace explo;

  DependencyTreeBuilder b;
  b.addPackage(pkg("web", "2.1.0", {dep("core", {ge("1.2.0"), lt("2.0.0")}), dep("log", {ge("1.0.0"), lt("1.1.0")})}));
  b.addPackage(pkg("web", "2.0.0", {dep("core", {ge("1.0.0"), lt("2.0.0")})}));
  b.addPackage(pkg("core", "1.3.0", {dep("util", {ge("2.0.0"), lt("3.0.0")})}));
  b.addPackage(pkg("core", "1.2.5", {dep("util", {ge("1.0.0"), lt("2.0.0")})}));
  b.addPackage(pkg("util", "2.0.0"));
  b.addPackage(pkg("util", "1.9.0"));
  b.addPackage(pkg("log", "1.0.4", {dep("util", {ge("1.0.0"), lt("2.0.0")})})); // clashes with core 1.3.0 -> backtrack
  b.addPackage(pkg("log", "1.1.0"));
  b.addPackage(pkg("plugin-a", "1.0.0", {dep("log")}));                       // log loads before plugin-a
  b.addPackage(pkg("metrics", "1.0.0", {dep("core", {}, LoadOrder::AFTER)})); // core loads after metrics
  b.addPackage(pkg("x", "1.0.0", {dep("y")}));
  b.addPackage(pkg("y", "1.0.0", {dep("x")}));

  Package app = pkg("app", "0.1.0",
                    {
                        dep("web", {ge("2.0.0"), lt("3.0.0")}),
                        dep("plugin-a"),
                        dep("metrics", {ge("1.0.0"), lt("2.0.0")}),
                    });

  Package tooNew = pkg("app", "0.1.0", {dep("web"), dep("core", {ge("3.0.0")})});

  Package cyclic = pkg("app", "0.1.0", {dep("x")});

  REQUIRE_THROWS_AS(b.build(cyclic), CyclicDependencyException);
  REQUIRE_THROWS_AS(b.build(tooNew), DependencyResolutionException);

  REQUIRE_NOTHROW(b.build(app));
  auto result = b.build(app);

  // metrics -> util -> core -> log -> plugin-a -> web -> app
  REQUIRE(result.loadOrder == std::vector<std::string>{"metrics", "util", "core", "log", "plugin-a", "web", "app"});
  REQUIRE(result.packages.at("web")->version == SemVer("2.1.0"));
  REQUIRE(result.packages.at("core")->version == SemVer("1.2.5"));
  REQUIRE(result.packages.at("util")->version == SemVer("1.9.0"));
  REQUIRE(result.packages.at("log")->version == SemVer("1.0.4"));
  REQUIRE(result.packages.at("plugin-a")->version == SemVer("1.0.0"));
  REQUIRE(result.packages.at("metrics")->version == SemVer("1.0.0"));
  REQUIRE(result.packages.at("app")->version == SemVer("0.1.0"));

  auto result2 = b.build();
  REQUIRE(result2.skipped.find("web") == result2.skipped.end());
  REQUIRE(result2.skipped.find("core") == result2.skipped.end());
  REQUIRE(result2.skipped.find("util") == result2.skipped.end());
  REQUIRE(result2.skipped.find("log") == result2.skipped.end());
  REQUIRE(result2.skipped.find("plugin-a") == result2.skipped.end());
  REQUIRE(result2.skipped.find("metrics") == result2.skipped.end());
  REQUIRE(result2.skipped.find("y") == result2.skipped.end());
  REQUIRE(result2.skipped.find("x") != result2.skipped.end());
  REQUIRE(result2.loadOrder == std::vector<std::string>{"metrics", "util", "core", "log", "plugin-a", "web"});
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: