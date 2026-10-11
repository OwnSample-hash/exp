// depbuilder.cpp - dependency tree builder on top of explo::SemVer
// Build: g++ -std=c++20 -O2 -Wall -Wextra -I<dir with semver.hpp> -o depbuilder depbuilder.cpp
#include <algorithm>
#include <cstdint>
#include <dep.hpp>
#include <interfaces/mod.hpp>
#include <iostream>
#include <map>
#include <semver.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace explo {

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------
void DependencyTreeBuilder::addPackage(Package p) {
  std::string name = p.name;
  registry_[name].push_back(std::move(p));
}

// Pointers in Result point into this builder and into `root`; keep both alive while using Result,
// and do not call addPackage() in between.
Result DependencyTreeBuilder::build(const Package &root) const {
  Result r;
  r.packages = Solver(registry_).run(root);
  r.loadOrder = computeLoadOrder(r.packages);
  return r;
}

Result DependencyTreeBuilder::build() const {
  Result best = buildFrom({});
  std::vector<Dependency> accepted;
  std::map<std::string, std::string> skipped;
  for (const std::string &name : topLevelNames()) {
    accepted.push_back({name, LoadOrder::BEFORE, {}});
    try {
      best = buildFrom(accepted);
    } catch (const std::runtime_error &e) {
      accepted.pop_back();
      skipped.emplace(name, e.what());
    }
  }
  best.skipped = std::move(skipped);
  return best;
}

Result DependencyTreeBuilder::buildFrom(const std::vector<Dependency> &top) const {
  const Package synthetic{"<root>", SemVer("0.0.0"), top};
  Chosen chosen = Solver(registry_).run(synthetic);
  chosen.erase(synthetic.name);

  Result r;
  r.loadOrder = computeLoadOrder(chosen);
  for (const Dependency &d : top)
    r.roots.push_back(chosen.at(d.name));
  r.packages = std::move(chosen);
  return r;
}

std::vector<std::string> DependencyTreeBuilder::topLevelNames() const {
  std::map<std::string, std::set<std::string>> uses; // name -> names it depends on (any version)
  std::set<std::string> used;
  for (const auto &[name, versions] : registry_) {
    uses[name];
    for (const Package &p : versions)
      for (const Dependency &d : p.dependencies)
        if (d.name != name) {
          uses[name].insert(d.name);
          used.insert(d.name);
        }
  }

  std::vector<std::string> roots;
  for (const auto &[name, _] : registry_)
    if (!used.count(name))
      roots.push_back(name);

  std::set<std::string> reached;
  auto reach = [&](const std::string &start) {
    std::vector<std::string> stack{start};
    while (!stack.empty()) {
      std::string n = std::move(stack.back());
      stack.pop_back();
      if (!reached.insert(n).second)
        continue;
      if (auto it = uses.find(n); it != uses.end())
        stack.insert(stack.end(), it->second.begin(), it->second.end());
    }
  };
  for (const std::string &r : roots)
    reach(r);
  for (const auto &[name, _] : registry_)
    if (!reached.count(name)) {
      roots.push_back(name);
      reach(name);
    }
  return roots;
}

void DependencyTreeBuilder::printTree(const Package &root, const Result &r, std::ostream &os) {
  std::set<std::string> seen{root.name};
  os << root.name << "@" << root.version.toString() << "\n";
  printChildren(root, r, seen, "", os);
}

Chosen DependencyTreeBuilder::Solver::run(const Package &root) {
  chosen_.emplace(root.name, &root);
  for (const Dependency &d : root.dependencies)
    queue_.push_back({&root, &d});
  if (!step(0))
    throw DependencyResolutionException("cannot resolve dependencies: " + failure_);
  return std::move(chosen_);
}

bool DependencyTreeBuilder::Solver::step(std::size_t i) {
  if (i == queue_.size())
    return true;
  const Request req = queue_[i]; // copy: queue_ may reallocate below
  std::vector<Version> &cons = constraints_[req.dep->name];
  const auto mark = static_cast<std::ptrdiff_t>(cons.size());
  cons.insert(cons.end(), req.dep->versions.begin(), req.dep->versions.end());

  if (visit(i, req, cons))
    return true;
  cons.erase(cons.begin() + mark, cons.end()); // undo on failure (Version has no default ctor, so no resize)
  return false;
}

bool DependencyTreeBuilder::Solver::visit(std::size_t i, const Request &req, const std::vector<Version> &cons) {
  const Dependency &dep = *req.dep;

  // Already picked: the new constraints just have to accept that pick.
  if (auto it = chosen_.find(dep.name); it != chosen_.end()) {
    if (!satisfiesAll(it->second->version, dep.versions)) {
      failure_ = "'" + dep.name + "' is already resolved to " + it->second->version.toString() + ", but '" +
                 req.from->name + "' requires " + describe(dep.versions);
      return false;
    }
    return step(i + 1);
  }

  std::vector<const Package *> candidates;
  auto reg = reg_.find(dep.name);
  if (reg != reg_.end())
    for (const Package &p : reg->second)
      if (satisfiesAll(p.version, cons))
        candidates.push_back(&p);

  if (candidates.empty()) {
    failure_ = reg == reg_.end() ? "unknown package '" + dep.name + "' (required by '" + req.from->name + "')"
                                 : "no version of '" + dep.name + "' satisfies " + describe(cons) + " (required by '" +
                                       req.from->name + "')";
    return false;
  }

  // Newest first. SemVer ignores prerelease in comparisons, so prefer a release on ties.
  std::sort(candidates.begin(), candidates.end(), [](const Package *a, const Package *b) {
    if (a->version != b->version)
      return a->version > b->version;
    return a->version.preRelease.empty() && !b->version.preRelease.empty();
  });

  for (const Package *c : candidates) {
    chosen_.emplace(dep.name, c);
    const std::size_t qmark = queue_.size();
    for (const Dependency &d : c->dependencies)
      queue_.push_back({c, &d});
    if (step(i + 1))
      return true;
    queue_.resize(qmark);
    chosen_.erase(dep.name);
  }
  return false;
}

// ---- Step 2: load order (Kahn's algorithm over the BEFORE/AFTER edges) ----
std::vector<std::string> DependencyTreeBuilder::computeLoadOrder(const Chosen &chosen) {
  std::map<std::string, std::set<std::string>> out, in; // edge a->b means "a loads before b"
  auto edge = [&](const std::string &a, const std::string &b) {
    if (a == b)
      return;
    out[a].insert(b);
    in[b].insert(a);
  };
  for (const auto &[name, pkg] : chosen) {
    out[name];
    in[name];
    for (const Dependency &d : pkg->dependencies) {
      if (d.order == LoadOrder::BEFORE)
        edge(d.name, name);
      else
        edge(name, d.name);
    }
  }

  std::map<std::string, std::size_t> indeg;
  std::set<std::string> ready; // sorted -> deterministic output
  for (const auto &[name, preds] : in) {
    indeg[name] = preds.size();
    if (preds.empty())
      ready.insert(name);
  }

  std::vector<std::string> result;
  while (!ready.empty()) {
    std::string n = *ready.begin();
    ready.erase(ready.begin());
    result.push_back(n);
    for (const std::string &m : out[n])
      if (--indeg[m] == 0)
        ready.insert(m);
  }

  if (result.size() < chosen.size())
    throw CyclicDependencyException("load order cycle: " + findCycle(chosen, in, result));
  return result;
}

// Everything Kahn could not place is stuck, but only some of it is on the cycle. Walk predecessors
// among the stuck nodes until one repeats; that repeat closes a real cycle.
std::string DependencyTreeBuilder::findCycle(const Chosen &chosen,
                                             const std::map<std::string, std::set<std::string>> &in,
                                             const std::vector<std::string> &placed) {
  const std::set<std::string> done(placed.begin(), placed.end());
  std::string cur;
  for (const auto &[name, _] : chosen)
    if (!done.count(name)) {
      cur = name;
      break;
    }

  std::vector<std::string> walk;
  std::map<std::string, std::size_t> pos;
  while (!pos.count(cur)) {
    pos[cur] = walk.size();
    walk.push_back(cur);
    for (const std::string &p : in.at(cur))
      if (!done.count(p)) {
        cur = p;
        break;
      }
  }
  std::vector<std::string> cycle(walk.begin() + static_cast<std::ptrdiff_t>(pos[cur]), walk.end());
  std::reverse(cycle.begin(), cycle.end()); // predecessor walk -> forward direction
  cycle.push_back(cycle.front());

  std::string s;
  for (const std::string &n : cycle)
    s += (s.empty() ? "" : " -> ") + n;
  return s;
}

// ---- Step 3: tree display ----
void DependencyTreeBuilder::printChildren(const Package &p, const Result &r, std::set<std::string> &seen,
                                          const std::string &prefix, std::ostream &os) {
  for (std::size_t i = 0; i < p.dependencies.size(); ++i) {
    const Dependency &d = p.dependencies[i];
    const bool last = i + 1 == p.dependencies.size();
    const Package &child = *r.packages.at(d.name);
    const bool firstVisit = seen.insert(d.name).second;
    os << prefix << (last ? "└─ " : "├─ ") << child.name << "@" << child.version.toString()
       << (!firstVisit && !child.dependencies.empty() ? " (deduped)" : "") << "\n";
    if (firstVisit)
      printChildren(child, r, seen, prefix + (last ? "   " : "│  "), os);
  }
}

} // namespace explo

// ---------------------------------------------------------------------------
// Demo
// ---------------------------------------------------------------------------
// using namespace explo;

// static void run(const char *title, const DependencyTreeBuilder &b, const Package &root) {
//   std::cout << "=== " << title << " ===\n";
//   try {
//     auto r = b.build(root);
//     std::cout << "Resolved:\n";
//     for (const auto &[n, p] : r.packages)
//       std::cout << "  " << n << "@" << p->version.toString() << "\n";
//     std::cout << "Load order:\n  ";
//     for (std::size_t i = 0; i < r.loadOrder.size(); ++i)
//       std::cout << (i ? " -> " : "") << r.loadOrder[i];
//     std::cout << "\nTree:\n";
//     DependencyTreeBuilder::printTree(root, r);
//   } catch (const std::exception &e) {
//     std::cout << "error: " << e.what() << "\n";
//   }
//   std::cout << "\n";
// }

// int main() {
//   DependencyTreeBuilder b;
//   b.addPackage(pkg("web", "2.1.0", {dep("core", {ge("1.2.0"), lt("2.0.0")}), dep("log", {ge("1.0.0"),
//   lt("1.1.0")})})); b.addPackage(pkg("web", "2.0.0", {dep("core", {ge("1.0.0"), lt("2.0.0")})}));
//   b.addPackage(pkg("core", "1.3.0", {dep("util", {ge("2.0.0"), lt("3.0.0")})}));
//   b.addPackage(pkg("core", "1.2.5", {dep("util", {ge("1.0.0"), lt("2.0.0")})}));
//   b.addPackage(pkg("util", "2.0.0"));
//   b.addPackage(pkg("util", "1.9.0"));
//   b.addPackage(pkg("log", "1.0.4", {dep("util", {ge("1.0.0"), lt("2.0.0")})}));  // clashes with core 1.3.0 ->
//   backtrack b.addPackage(pkg("log", "1.1.0")); b.addPackage(pkg("plugin-a", "1.0.0", {dep("log")})); // log loads
//   before plugin-a b.addPackage(pkg("metrics", "1.0.0", {dep("core", {}, LoadOrder::AFTER)}));     // core loads after
//   metrics b.addPackage(pkg("x", "1.0.0", {dep("y")})); b.addPackage(pkg("y", "1.0.0", {dep("x")})); // x <-> y

//   Package app = pkg("app", "0.1.0", {dep("web", {ge("2.0.0"), lt("3.0.0")}), dep("plugin-a"),
//                                      dep("metrics", {ge("1.0.0"), lt("2.0.0")})});
//   run("resolves with backtracking", b, app);

//   Package tooNew = pkg("app", "0.1.0", {dep("web"), dep("core", {ge("3.0.0")})});
//   run("unsatisfiable", b, tooNew);

//   Package cyclic = pkg("app", "0.1.0", {dep("x")});
//   run("load order cycle", b, cyclic);
// }

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120: