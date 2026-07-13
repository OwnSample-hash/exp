#pragma once
#include <cmd/command_processor.hpp>
#include <cmd/context.hpp>
#include <cmd/flow_control.hpp>
#include <cmd/variable.hpp>
#include <fstream>
#include <functional>
#include <memory>
#include <spdlog/fmt/std.h>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace explo {
namespace cmd {

enum class InputResult {
  Consumed,      // char was consumed, nothing to report
  Escape,        // start of an escape sequence (e.g. arrow keys)
  Help,          // '?' triggered help display
  Autocompleted, // '\t' triggered autocomplete
  Executed,      // '\n' triggered execution
  Cleared,       // '^c' cleared the line
  Error          // parse/execution error
};

struct ExecutionResult {
  bool success = true;
  std::string message; // optional human-readable result
  int exitCode = 0;
};

// Callback types
using HelpCallback = std::function<void(const std::vector<std::string> &options)>;
using AutocompleteCallback = std::function<void(const std::string &completed, bool unique)>;
using ExecuteCallback = std::function<void(const ExecutionResult &)>;
using ErrorCallback = std::function<void(const std::string &message)>;

class CommandProcessor {
  CommandProcessor();

public:
  ~CommandProcessor();

  static CommandProcessor &instance() {
    static CommandProcessor cp;
    return cp;
  }

  // ── Context management ────────────────────────────────────────────────
  void registerContext(std::shared_ptr<Context> ctx);
  void switchContext(const std::string &contextName);
  std::string currentContextName() const;

  // ── Global (always-available) commands ────────────────────────────────
  void registerGlobalCommand(CommandDef cmd);

  // ── Variable system ───────────────────────────────────────────────────
  VariableStore &vars() { return vars_; }
  const VariableStore &vars() const { return vars_; }

  // ── Flow control ──────────────────────────────────────────────────────
  FlowController &flow() { return flow_; }

  // ── Callbacks (no I/O done internally) ───────────────────────────────
  void onHelp(HelpCallback cb) { helpCb_ = std::move(cb); }
  void onAutocomplete(AutocompleteCallback cb) { autocompleteCb_ = std::move(cb); }
  void onExecute(ExecuteCallback cb) { executeCb_ = std::move(cb); }
  void onError(ErrorCallback cb) { errorCb_ = std::move(cb); }

  // ── Character-by-character input ──────────────────────────────────────
  InputResult feed(char c);

  // ── Direct buffer access (read-only) ─────────────────────────────────
  const std::string &buffer() const { return buffer_; }

  const std::size_t cursorPos() const { return cursorPos_; }

  const std::vector<std::string> &history() const { return history_; }

  // ── Script execution (multi-line with flow control) ───────────────────
  void executeScript(const std::vector<std::string> &lines);
  void executeScript(const std::string &path);

  std::shared_ptr<Context> getContext(bool isGlobal = true) const {
    return isGlobal ? this->globalCtx_ : this->currentCtx_;
  }

private:
  std::string buffer_, bufferBackup_;
  std::size_t cursorPos_ = 0;
  std::fstream historyFile;
  std::vector<std::string> history_;
  size_t historyIndex_ = 0;
  std::unordered_map<std::string, std::shared_ptr<Context>> contexts_;
  std::shared_ptr<Context> currentCtx_;
  std::shared_ptr<Context> globalCtx_; // always active

  VariableStore vars_;
  FlowController flow_;

  HelpCallback helpCb_;
  AutocompleteCallback autocompleteCb_;
  ExecuteCallback executeCb_;
  ErrorCallback errorCb_;

  // internals
  ExecutionResult executeLine(const std::string &line);
  std::vector<std::string> collectCandidates(const std::string &partial) const;
  std::string commonPrefix(const std::vector<std::string> &v) const;
  std::vector<std::string> tokenize(const std::string &line) const;
  std::string expandVariables(const std::string &token) const;
};

} // namespace cmd
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
