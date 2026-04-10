#include <cmd.hpp>
#include <fstream>
#include <stdexcept>

namespace explo {
namespace cmd {

static constexpr char CTRL_C = 0x03;

// ── Built-in command names ─────────────────────────────────────────────────
static constexpr const char *kSet = "set";
static constexpr const char *kUnset = "unset";
static constexpr const char *kExport = "export";
static constexpr const char *kEnv = "env";
static constexpr const char *kEcho = "echo";
static constexpr const char *kCtx = "ctx";

// ─────────────────────────────────────────────────────────────────────────────
// Constructor – builds the global context and registers built-in commands
// ─────────────────────────────────────────────────────────────────────────────

CommandProcessor::CommandProcessor() {
  globalCtx_ = std::make_shared<Context>("__global__");

  // set <name> <value>
  {
    CommandDef c;
    c.name = kSet;
    c.description = "Set a variable: set <name> <value>";
    c.variadic = true;
    c.handler = [this](const ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 3)
        throw std::runtime_error("set: usage: set <name> <value>");
      const std::string &name = ec.args[1];
      // Join remaining args as value
      std::string val;
      for (size_t i = 2; i < ec.args.size(); ++i) {
        if (i > 2)
          val += ' ';
        val += ec.args[i];
      }
      // Try to evaluate as arithmetic expression
      try {
        Expression expr(val);
        long long iv = expr.evaluate(*ec.vars);
        ec.vars->set(name, VarValue(iv));
        return {};
      } catch (...) {
      }
      // Try plain integer
      try {
        size_t pos;
        long long iv = std::stoll(val, &pos);
        if (pos == val.size()) {
          ec.vars->set(name, VarValue(iv));
          return {};
        }
      } catch (...) {
      }
      // Try float
      try {
        size_t pos;
        double dv = std::stod(val, &pos);
        if (pos == val.size()) {
          ec.vars->set(name, VarValue(dv));
          return {};
        }
      } catch (...) {
      }
      ec.vars->set(name, VarValue(val));
      return {};
    };
    globalCtx_->registerCommand(std::move(c));
  }

  // unset <name>
  {
    CommandDef c;
    c.name = kUnset;
    c.description = "Unset a variable: unset <name>";
    c.variadic = true;
    c.handler = [](const ExecutionContext &ec) -> std::string {
      for (size_t i = 1; i < ec.args.size(); ++i)
        ec.vars->unset(ec.args[i]);
      return {};
    };
    globalCtx_->registerCommand(std::move(c));
  }

  // export <name>
  {
    CommandDef c;
    c.name = kExport;
    c.description = "Export a variable to sub-contexts: export <name>";
    c.variadic = true;
    c.handler = [](const ExecutionContext &ec) -> std::string {
      for (size_t i = 1; i < ec.args.size(); ++i)
        ec.vars->exportVar(ec.args[i]);
      return {};
    };
    globalCtx_->registerCommand(std::move(c));
  }

  // env  – list all variables
  {
    CommandDef c;
    c.name = kEnv;
    c.description = "List all variables";
    c.handler = [](const ExecutionContext &ec) -> std::string {
      std::string out;
      for (auto &n : ec.vars->names()) {
        auto v = ec.vars->get(n);
        out += n + "=" + (v ? v->toString() : "") + "\n";
      }
      return out;
    };
    globalCtx_->registerCommand(std::move(c));
  }

  // echo
  {
    CommandDef c;
    c.name = kEcho;
    c.description = "Print arguments: echo <arg>...";
    c.variadic = true;
    c.handler = [](const ExecutionContext &ec) -> std::string {
      std::string out;
      for (size_t i = 1; i < ec.args.size(); ++i) {
        if (i > 1)
          out += ' ';
        out += ec.args[i];
      }
      return out;
    };
    globalCtx_->registerCommand(std::move(c));
  }

  // ctx <name> – switch context
  {
    CommandDef c;
    c.name = kCtx;
    c.description = "Switch context: ctx <name>";
    c.variadic = false;
    c.handler = [this](const ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 2)
        throw std::runtime_error("ctx: usage: ctx <context-name>");
      switchContext(ec.args[1]);
      return {};
    };
    globalCtx_->registerCommand(std::move(c));
  }

  historyFile.open(".cmd_history",
                   std::ios::in | std::ios::out | std::ios::app);
  if (!historyFile.is_open())
    return;
  std::string line;
  while (std::getline(historyFile, line)) {
    history_.push_back(line);
  }
  history_.erase(std::remove_if(history_.begin(), history_.end(),
                                [](const std::string &s) { return s.empty(); }),
                 history_.end());
  history_.shrink_to_fit();
  historyIndex_ = history_.size();
}

CommandProcessor::~CommandProcessor() {}

// ─────────────────────────────────────────────────────────────────────────────
// Context management
// ─────────────────────────────────────────────────────────────────────────────

void CommandProcessor::registerContext(std::shared_ptr<Context> ctx) {
  contexts_[ctx->name()] = std::move(ctx);
  if (!currentCtx_)
    currentCtx_ = contexts_.begin()->second;
}

void CommandProcessor::switchContext(const std::string &name) {
  auto it = contexts_.find(name);
  if (it == contexts_.end())
    throw std::runtime_error("ctx: unknown context '" + name + "'");
  currentCtx_ = it->second;
}

std::string CommandProcessor::currentContextName() const {
  return currentCtx_ ? currentCtx_->name() : "__global__";
}

void CommandProcessor::registerGlobalCommand(CommandDef cmd) {
  globalCtx_->registerCommand(std::move(cmd));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tokenizer (respects double/single quotes and backslash escapes)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string>
CommandProcessor::tokenize(const std::string &line) const {
  std::vector<std::string> tokens;
  std::string cur;
  bool inDouble = false, inSingle = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '\\' && !inSingle && i + 1 < line.size()) {
      cur += line[++i];
      continue;
    }

    if (c == '"' && !inSingle) {
      inDouble = !inDouble;
      continue;
    }
    if (c == '\'' && !inDouble) {
      inSingle = !inSingle;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c)) && !inDouble && !inSingle) {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty())
    tokens.push_back(cur);
  return tokens;
}

std::string CommandProcessor::expandVariables(const std::string &token) const {
  return vars_.expand(token);
}

// ─────────────────────────────────────────────────────────────────────────────
// Candidate collection for help/autocomplete
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string>
CommandProcessor::collectCandidates(const std::string &partial) const {
  std::vector<std::string> result;

  // Tokenize the current buffer to determine what stage we're at
  auto tokens = tokenize(partial);

  auto addCommandCandidates = [&](const Context *ctx,
                                  const std::string &prefix) {
    for (auto &cmd : ctx->commands()) {
      if (cmd.name.substr(0, prefix.size()) == prefix)
        result.push_back(cmd.name);
    }
  };

  if (tokens.empty()) {
    // Nothing typed yet – list all commands
    addCommandCandidates(globalCtx_.get(), "");
    if (currentCtx_)
      addCommandCandidates(currentCtx_.get(), "");
    return result;
  }

  // First token: command name candidates
  if (tokens.size() == 1 && !partial.empty() && partial.back() != ' ') {
    addCommandCandidates(globalCtx_.get(), tokens[0]);
    if (currentCtx_)
      addCommandCandidates(currentCtx_.get(), tokens[0]);
    return result;
  }

  // Find the command definition
  const CommandDef *def = globalCtx_->findCommand(tokens[0]);
  if (!def && currentCtx_)
    def = currentCtx_->findCommand(tokens[0]);
  if (!def)
    return result;

  // Navigate into sub-commands if needed
  size_t argStart = 1;
  while (argStart < tokens.size()) {
    bool found = false;
    for (auto &sub : def->subCommands) {
      if (sub.name == tokens[argStart]) {
        def = &sub;
        ++argStart;
        found = true;
        break;
      }
    }
    if (!found)
      break;
  }

  // Which argument position are we completing?
  size_t argIdx = tokens.size() - argStart; // 0-based
  bool atSpace = !partial.empty() && partial.back() == ' ';
  if (atSpace)
    argIdx++;

  // Sub-commands at this position
  if (!def->subCommands.empty()) {
    std::string prefix =
        (atSpace || tokens.size() <= argStart) ? "" : tokens.back();
    for (auto &sub : def->subCommands)
      if (sub.name.substr(0, prefix.size()) == prefix)
        result.push_back(sub.name);
    if (!result.empty())
      return result;
  }

  // Options at this position
  size_t optIdx = atSpace ? argIdx : (argIdx > 0 ? argIdx - 1 : 0);
  if (optIdx < def->options.size()) {
    const Option &opt = def->options[optIdx];
    if (isStatic(opt)) {
      const std::string &val = std::get<StaticOption>(opt).value;
      std::string pfx = atSpace ? "" : tokens.back();
      if (val.substr(0, pfx.size()) == pfx)
        result.push_back(val);
    } else {
      // Dynamic option – show the hint as a placeholder
      result.push_back(std::get<DynamicOption>(opt).pattern);
    }
  } else if (def->variadic && !def->options.empty()) {
    // Repeat last option
    const Option &opt = def->options.back();
    result.push_back(optionHint(opt));
  }

  return result;
}

std::string
CommandProcessor::commonPrefix(const std::vector<std::string> &v) const {
  if (v.empty())
    return {};
  std::string prefix = v[0];
  for (size_t i = 1; i < v.size(); ++i) {
    size_t j = 0;
    while (j < prefix.size() && j < v[i].size() && prefix[j] == v[i][j])
      ++j;
    prefix = prefix.substr(0, j);
  }
  return prefix;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

ExecutionResult CommandProcessor::executeLine(const std::string &line) {
  ExecutionResult res;
  std::string trimmed = line;
  // Trim whitespace
  size_t a = 0, b = trimmed.size();
  while (a < b && std::isspace(static_cast<unsigned char>(trimmed[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(trimmed[b - 1])))
    --b;
  trimmed = trimmed.substr(a, b - a);

  if (trimmed.empty() || trimmed[0] == '#')
    return res; // comment or blank

  // Expand variables in the whole line first
  std::string expanded = vars_.expand(trimmed);
  auto rawTokens = tokenize(expanded);
  if (rawTokens.empty())
    return res;

  // Expand each token individually
  std::vector<std::string> args;
  for (auto &t : rawTokens)
    args.push_back(expandVariables(t));

  const std::string &cmdName = args[0];

  // Look up command (global first, then context)
  const CommandDef *def = globalCtx_->findCommand(cmdName);
  if (!def && currentCtx_)
    def = currentCtx_->findCommand(cmdName);

  if (!def) {
    res.success = false;
    res.message = "Unknown command: '" + cmdName + "'";
    res.exitCode = 127;
    return res;
  }

  // Build execution context
  ExecutionContext ec;
  ec.args = args;
  ec.vars = &vars_;
  ec.flow = &flow_;
  ec.contextName = currentContextName();

  // Validate args against option list (if not variadic)
  if (!def->variadic && !def->options.empty()) {
    size_t maxArgs = def->options.size() + 1; // +1 for cmd name
    if (args.size() > maxArgs) {
      res.success = false;
      res.message = "Too many arguments for '" + cmdName + "'";
      res.exitCode = 1;
      return res;
    }
    // Validate each arg against its option
    for (size_t i = 1; i < args.size() && (i - 1) < def->options.size(); ++i) {
      if (!matchOption(def->options[i - 1], args[i])) {
        res.success = false;
        res.message = "Invalid argument '" + args[i] + "' for option " +
                      std::to_string(i) + " of '" + cmdName + "'";
        res.exitCode = 1;
        return res;
      }
    }
  }

  if (!def->handler) {
    res.success = false;
    res.message = "Command '" + cmdName + "' has no handler";
    res.exitCode = 1;
    return res;
  }

  try {
    res.message = def->handler(ec);
  } catch (const std::exception &e) {
    res.success = false;
    res.message = e.what();
    res.exitCode = 1;
  }
  return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// Character input
// ─────────────────────────────────────────────────────────────────────────────

InputResult CommandProcessor::feed(char c) {

  static bool escapeSeq = false;

  if (c == 0x1B) { // Start of escape sequence
    escapeSeq = true;
    return InputResult::Escape;
  }

  if (escapeSeq) {
    if (c == '[') {               // '[' indicates arrow keys
      return InputResult::Escape; // Still in escape sequence, wait for next
                                  // char
    }
    escapeSeq = false;

    if (c == 'A') {
      // Up arrow – recall previous command
      if (!history_.empty()) {
        bufferBackup_ = buffer_;
        if (historyIndex_ > 0)
          buffer_ = history_[--historyIndex_];
      }
    } else if (c == 'B') {
      // Down arrow – clear line
      if (historyIndex_ < history_.size()) {
        bufferBackup_ = buffer_;
        buffer_ = (historyIndex_ < history_.size() - 1)
                      ? history_[++historyIndex_]
                      : "";
      }
    }
    return InputResult::Consumed;
  }

  // Ctrl-C  → clear line
  if (c == CTRL_C) {
    buffer_.clear();
    return InputResult::Cleared;
  }

  // Backspace
  if (c == '\b' || c == 127) {
    if (!buffer_.empty())
      buffer_.pop_back();
    return InputResult::Consumed;
  }

  // Help
  if (c == '?') {
    auto candidates = collectCandidates(buffer_);
    if (helpCb_)
      helpCb_(candidates);
    return InputResult::Help;
  }

  // Autocomplete
  if (c == '\t') {
    auto candidates = collectCandidates(buffer_);
    if (candidates.empty())
      return InputResult::Consumed;

    std::string prefix = commonPrefix(candidates);
    bool unique = (candidates.size() == 1);

    // Replace the last token with the completion
    // Find the start of the last word in the buffer
    size_t lastSpace = buffer_.rfind(' ');
    std::string base = (lastSpace == std::string::npos)
                           ? ""
                           : buffer_.substr(0, lastSpace + 1);
    buffer_ = base + prefix;

    if (autocompleteCb_)
      autocompleteCb_(buffer_, unique);
    return InputResult::Autocompleted;
  }

  // Execute
  if (c == '\n' || c == '\r') {
    std::string line = buffer_;
    buffer_.clear();
    auto result = executeLine(line);
    if (executeCb_)
      executeCb_(result);
    if (!result.success && errorCb_)
      errorCb_(result.message);
    history_.push_back(line);
    historyIndex_ = history_.size();
    historyFile << line << std::endl << std::flush;
    return InputResult::Executed;
  }

  // Ordinary character
  buffer_ += c;
  return InputResult::Consumed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Script execution
// ─────────────────────────────────────────────────────────────────────────────

void CommandProcessor::executeScript(const std::vector<std::string> &lines) {
  LineExecutor exec = [this](const std::string &line) -> bool {
    auto result = executeLine(line);
    if (executeCb_)
      executeCb_(result);
    if (!result.success && errorCb_)
      errorCb_(result.message);
    return result.success;
  };
  flow_.run(lines, vars_, exec);
}

void CommandProcessor::executeScript(const std::string &path) {
  std::vector<std::string> lines;
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Failed to open script file: " + path);
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  executeScript(lines);
}

} // namespace cmd
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
