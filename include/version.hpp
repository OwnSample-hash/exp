#pragma once

#include <semver.hpp>
#include <vector>

namespace explo {

enum class LoadOrder : std::uint8_t {
  AFTER,
  BEFORE,
};

enum class VersionComparison : std::uint8_t {
  EQUAL,
  GREATER,
  LESS,
  GREATER_EQUAL,
  LESS_EQUAL,
  NOT_EQUAL,
};

struct Version {
  SemVer version;
  VersionComparison comparison = VersionComparison::GREATER_EQUAL;
};

// Declared by a package ("owner") about another package.
//   order    : where the NAMED package loads relative to the owner.
//              BEFORE -> named package loads before the owner (the usual meaning of "depends on")
//              AFTER  -> named package loads after the owner
//   versions : constraints on the named package's version; ALL must hold. Empty = any version.
// Every Dependency is hard: the named package must be resolvable and ends up in the tree.
struct Dependency {
  std::string name;
  LoadOrder order = LoadOrder::BEFORE;
  std::vector<Version> versions;
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: