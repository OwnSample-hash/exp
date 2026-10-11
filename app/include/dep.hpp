#pragma once

#include <iostream>
#include <map>
#include <semver.hpp>
#include <set>
#include <vector>
#include <version.hpp>

namespace explo {

struct Package {
  std::string name;
  SemVer version;
  std::vector<Dependency> dependencies;
};

// ---------------------------------------------------------------------------
// Version constraint helpers
// ---------------------------------------------------------------------------
inline bool satisfies(const SemVer &v, const Version &c) {
  switch (c.comparison) {
  case VersionComparison::EQUAL:
    return v == c.version;
  case VersionComparison::GREATER:
    return v > c.version;
  case VersionComparison::LESS:
    return v < c.version;
  case VersionComparison::GREATER_EQUAL:
    return v >= c.version;
  case VersionComparison::LESS_EQUAL:
    return v <= c.version;
  case VersionComparison::NOT_EQUAL:
    return v != c.version;
  }
  return false;
}

inline bool satisfiesAll(const SemVer &v, const std::vector<Version> &constraints) {
  return std::all_of(constraints.begin(), constraints.end(), [&](const Version &c) { return satisfies(v, c); });
}

inline const char *symbol(VersionComparison c) {
  switch (c) {
  case VersionComparison::EQUAL:
    return "==";
  case VersionComparison::GREATER:
    return ">";
  case VersionComparison::LESS:
    return "<";
  case VersionComparison::GREATER_EQUAL:
    return ">=";
  case VersionComparison::LESS_EQUAL:
    return "<=";
  case VersionComparison::NOT_EQUAL:
    return "!=";
  }
  return "?";
}

inline std::string describe(const std::vector<Version> &constraints) {
  if (constraints.empty())
    return "any version";
  std::string s;
  for (const Version &c : constraints)
    s += (s.empty() ? "" : ", ") + std::string(symbol(c.comparison)) + c.version.toString();
  return s;
}

inline Version eq(std::string_view v) { return {SemVer(v), VersionComparison::EQUAL}; }
inline Version gt(std::string_view v) { return {SemVer(v), VersionComparison::GREATER}; }
inline Version lt(std::string_view v) { return {SemVer(v), VersionComparison::LESS}; }
inline Version ge(std::string_view v) { return {SemVer(v), VersionComparison::GREATER_EQUAL}; }
inline Version le(std::string_view v) { return {SemVer(v), VersionComparison::LESS_EQUAL}; }
inline Version ne(std::string_view v) { return {SemVer(v), VersionComparison::NOT_EQUAL}; }

inline Dependency dep(std::string name, std::vector<Version> versions = {}, LoadOrder order = LoadOrder::BEFORE) {
  return {std::move(name), order, std::move(versions)};
}

inline Package pkg(std::string name, std::string_view version, std::vector<Dependency> deps = {}) {
  return {std::move(name), SemVer(version), std::move(deps)};
}

inline Package pkg(std::string name, SemVer version, std::vector<Dependency> deps = {}) {
  return {std::move(name), std::move(version), std::move(deps)};
}

using Chosen = std::map<std::string, const Package *>;

struct Result {
  Chosen packages;                            // name -> resolved package
  std::vector<std::string> loadOrder;         // deterministic topological order
  std::vector<const Package *> roots;         // top of each tree (one entry for build(root))
  std::map<std::string, std::string> skipped; // build() only: top-level name -> why it was left out
};

class DependencyTreeBuilder {
public:
  void addPackage(Package p);

  // Pointers in Result point into this builder and into `root`; keep both alive while using Result,
  // and do not call addPackage() in between.
  Result build(const Package &root) const;

  Result build() const;

  Result buildFrom(const std::vector<Dependency> &top) const;

  std::vector<std::string> topLevelNames() const;

  static void printTree(const Package &root, const Result &r, std::ostream &os = std::cout);

private:
  using Registry = std::map<std::string, std::vector<Package>>;
  Registry registry_;

  // ---- Step 1: version resolution (depth-first, chronological backtracking) ----
  class Solver {
  public:
    explicit Solver(const Registry &reg) : reg_(reg) {}

    Chosen run(const Package &root);

  private:
    struct Request {
      const Package *from;
      const Dependency *dep;
    };

    const Registry &reg_;
    std::vector<Request> queue_; // requests still to process, grows as packages are chosen
    Chosen chosen_;
    std::map<std::string, std::vector<Version>> constraints_; // every constraint seen so far, per package name
    std::string failure_ = "unknown failure";                 // most recent conflict, for the error message

    bool step(std::size_t i);
    bool visit(std::size_t i, const Request &req, const std::vector<Version> &cons);
  };

  // ---- Step 2: load order (Kahn's algorithm over the BEFORE/AFTER edges) ----
  static std::vector<std::string> computeLoadOrder(const Chosen &chosen);

  // Everything Kahn could not place is stuck, but only some of it is on the cycle. Walk predecessors
  // among the stuck nodes until one repeats; that repeat closes a real cycle.
  static std::string findCycle(const Chosen &chosen, const std::map<std::string, std::set<std::string>> &in,
                               const std::vector<std::string> &placed);

  // ---- Step 3: tree display ----
  static void printChildren(const Package &p, const Result &r, std::set<std::string> &seen, const std::string &prefix,
                            std::ostream &os);
};

class DependencyResolutionException : public std::runtime_error {
public:
  explicit DependencyResolutionException(const std::string &message) : std::runtime_error(message) {}
};

class CyclicDependencyException : public DependencyResolutionException {
public:
  explicit CyclicDependencyException(const std::string &cycle)
      : DependencyResolutionException("Cyclic dependency detected: " + cycle) {}
};

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
