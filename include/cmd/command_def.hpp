#pragma once
#include <functional>
#include <regex>
#include <string>
#include <variant>
#include <vector>

namespace explo {
namespace cmd {

// ── Option
// ──────────────────────────────────────────────────────────────────── An
// option is either a static literal or a dynamic regex pattern.

struct StaticOption {
  std::string value;
  std::string description;
};

struct DynamicOption {
  std::string pattern; // human-readable hint, e.g. "<ip-address>"
  std::regex regex;
  std::string description;
  DynamicOption(std::string pat, const std::string &reStr, std::string desc)
      : pattern(std::move(pat)),
        regex(reStr, std::regex::ECMAScript | std::regex::optimize),
        description(std::move(desc)) {}
};

using Option = std::variant<StaticOption, DynamicOption>;

inline bool matchOption(const Option &opt, const std::string &token) {
  return std::visit(
      [&](auto &&o) -> bool {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, StaticOption>)
          return o.value == token;
        else
          return std::regex_match(token, o.regex);
      },
      opt);
}

inline std::string optionHint(const Option &opt) {
  return std::visit(
      [](auto &&o) -> std::string {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, StaticOption>)
          return o.value;
        else
          return o.pattern;
      },
      opt);
}

inline bool isStatic(const Option &opt) {
  return std::holds_alternative<StaticOption>(opt);
}

// ── CommandDef
// ────────────────────────────────────────────────────────────────

struct ExecutionContext; // forward decl – defined in ExecutionContext.hpp

using CommandHandler = std::function<std::string(const ExecutionContext &)>;
using OptionList = std::vector<Option>;

struct CommandDef {
  std::string name; // primary keyword, e.g. "connect"
  std::string description;
  OptionList options;    // ordered list of accepted arguments
  bool variadic = false; // accept unlimited trailing args

  // Nested sub-commands (tree structure)
  std::vector<CommandDef> subCommands;

  CommandHandler handler; // invoked on execution

  // Convenience builder helpers
  CommandDef &addStatic(const std::string &val, const std::string &desc = "") {
    options.push_back(StaticOption{val, desc});
    return *this;
  }
  CommandDef &addDynamic(const std::string &hint, const std::string &reStr,
                         const std::string &desc = "") {
    options.emplace_back(DynamicOption{hint, reStr, desc});
    return *this;
  }
  CommandDef &addSub(CommandDef sub) {
    subCommands.push_back(std::move(sub));
    return *this;
  }
  CommandDef &setHandler(CommandHandler h) {
    handler = std::move(h);
    return *this;
  }
  CommandDef &setVariadic(bool v = true) {
    variadic = v;
    return *this;
  }
  CommandDef &setDescription(const std::string &desc) {
    description = desc;
    return *this;
  }
  CommandDef &setName(const std::string &n) {
    name = n;
    return *this;
  }
  CommandDef &setOptions(OptionList opts) {
    options = std::move(opts);
    return *this;
  }
};

} // namespace cmd
} // namespace explo
