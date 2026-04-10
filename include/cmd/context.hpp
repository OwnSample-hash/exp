#pragma once
#include "command_def.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace explo {
namespace cmd {

// A Context groups a set of CommandDefs under a named scope.
// Multiple contexts can be registered; one is active at a time.
// Global commands (from the global context) are always searched.

class Context {
public:
  explicit Context(std::string name) : name_(std::move(name)) {}

  const std::string &name() const { return name_; }

  // Registration (runtime)
  void registerCommand(CommandDef cmd);

  // Lookup
  const CommandDef *findCommand(const std::string &name) const;

  // All top-level command names
  std::vector<std::string> commandNames() const;

  // All registered commands (read-only)
  const std::vector<CommandDef> &commands() const { return commands_; }

private:
  std::string name_;
  std::vector<CommandDef> commands_;
  std::unordered_map<std::string, size_t> index_; // name -> commands_ index
};

} // namespace cmd
} // namespace explo
