/**
 * @file var_snapshot.hpp
 * @brief Persist a tool's command-processor variables across a
 *        suppress()/invoke() cycle.
 *
 * Tools publish their namespaced variables (e.g. "fwd.addr", "bini.target")
 * into the global VariableStore on invoke() and unset() them on suppress().
 * Without help, any value the user changed is lost the moment the tool is
 * deselected. A VarSnapshot captures those values right before suppress()
 * removes them and hands them back on the next invoke(), so a reselected tool
 * comes up with the user's last settings instead of falling back to its
 * compiled-in defaults.
 *
 * Usage pattern:
 *   suppress():  snap.save({prefix + ".a", prefix + ".b"});  // then unset()
 *   invoke():    snap.restore();                             // then seed defaults
 */
#pragma once

#include <cmd/command_processor.hpp>
#include <cmd/variable.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace explo {
namespace cmd {

class VarSnapshot {
public:
  /**
   * @brief Record the current value of each named variable that is set.
   *
   * Call this in suppress() immediately before unset()-ing the same names.
   * Variables that are not currently set are skipped, so a name that was never
   * published simply leaves no snapshot behind.
   */
  void save(const std::vector<std::string> &names) {
    auto &vars = CommandProcessor::instance().vars();
    for (const auto &name : names)
      if (auto v = vars.get(name))
        saved_[name] = *v;
  }

  /// @brief Convenience overload: snapshot a single variable.
  void save(const std::string &name) {
    auto &vars = CommandProcessor::instance().vars();
    if (auto v = vars.get(name))
      saved_[name] = *v;
  }

  /**
   * @brief Re-publish every saved value back into the store.
   *
   * Call this in invoke() to restore the user's last settings. The snapshot is
   * retained, so repeated invoke()s (e.g. soft re-selects) stay idempotent.
   */
  void restore() const {
    auto &vars = CommandProcessor::instance().vars();
    for (const auto &[name, value] : saved_)
      vars.set(name, value);
  }

  /// @brief The saved value for a single variable, if one was captured.
  std::optional<VarValue> value(const std::string &name) const {
    auto it = saved_.find(name);
    if (it == saved_.end())
      return std::nullopt;
    return it->second;
  }

  bool has(const std::string &name) const { return saved_.find(name) != saved_.end(); }
  bool empty() const { return saved_.empty(); }
  void clear() { saved_.clear(); }

private:
  std::unordered_map<std::string, VarValue> saved_;
};

} // namespace cmd
} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
