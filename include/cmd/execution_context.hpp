#pragma once

#include <string>
#include <vector>

namespace explo {
namespace cmd {

class VariableStore;
class FlowController;

// Passed to every CommandHandler at execution time.
struct ExecutionContext {
  std::vector<std::string> args; // tokenized arguments (arg[0] = command name)
  VariableStore *vars;           // mutable variable store
  FlowController *flow;          // flow control state
  std::string contextName;       // active context name
};

} // namespace cmd
} // namespace explo
