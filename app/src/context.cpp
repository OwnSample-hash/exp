#include <cmd/context.hpp>
#include <stdexcept>

namespace explo {
namespace cmd {

void Context::registerCommand(CommandDef cmd) {
  const std::string name = cmd.name;
  if (index_.count(name))
    throw std::runtime_error("Context '" + name_ + "': command '" + name +
                             "' already registered");
  index_[name] = commands_.size();
  commands_.push_back(std::move(cmd));
}

const CommandDef *Context::findCommand(const std::string &name) const {
  auto it = index_.find(name);
  if (it == index_.end())
    return nullptr;
  return &commands_[it->second];
}

std::vector<std::string> Context::commandNames() const {
  std::vector<std::string> out;
  out.reserve(commands_.size());
  for (auto &c : commands_)
    out.push_back(c.name);
  return out;
}

} // namespace cmd
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
