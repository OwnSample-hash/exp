#pragma once
#include <functional>
#include <memory>
#include <spdlog/fmt/bundled/base.h>
#include <string>
#include <vector>

namespace explo {
namespace cmd {

class VariableStore;

// ── Expression
// ──────────────────────────────────────────────────────────────── Evaluates
// C-style boolean/arithmetic expressions used in if/while/for. Supports:
// integer literals, $var / ${var} references,
//           comparison (<,>,<=,>=,==,!=), logical (&&,||,!),
//           arithmetic (+,-,*,/,%), parentheses.

class Expression {
public:
  explicit Expression(std::string src) : src_(std::move(src)) {}

  long long evaluate(const VariableStore &vars) const;
  bool evaluateBool(const VariableStore &vars) const { return evaluate(vars) != 0; }

  const std::string &source() const { return src_; }

private:
  std::string src_;
};

// ── Flow signals
// ──────────────────────────────────────────────────────────────

enum class FlowSignal { None, Break, Continue, Return };

// ── Statement types
// ───────────────────────────────────────────────────────────

struct Statement;
using StatementList = std::vector<std::shared_ptr<Statement>>;

enum class StmtKind { Command, If, While, For, ForIn, Break, Continue, Return, Block };

struct IfClause {
  Expression condition;
  StatementList body;
  // Explicit constructor because Expression has no default ctor.
  IfClause(Expression cond, StatementList b) : condition(std::move(cond)), body(std::move(b)) {}
};

struct Statement {
  StmtKind kind = StmtKind::Command;

  // Command / Return
  std::string commandLine;

  // If
  std::vector<IfClause> ifClauses;
  StatementList elsebody;

  // While
  Expression whileCond{std::string{}};
  StatementList whileBody;

  // For (C-style)
  std::string forInit;
  Expression forCond{std::string{}};
  std::string forStep;
  StatementList forBody;

  // ForIn
  std::string forInVar;
  std::string forInList;
  StatementList forInBody;

  // Block
  StatementList blockBody;
};

// ── FlowParser
// ────────────────────────────────────────────────────────────────

class FlowParser {
public:
  StatementList parse(const std::vector<std::string> &lines) const;

private:
  size_t parseBlock(const std::vector<std::string> &lines, size_t start, StatementList &out) const;
  Statement parseStatement(const std::vector<std::string> &lines, size_t &i) const;
};

// ── FlowController
// ────────────────────────────────────────────────────────────

using LineExecutor = std::function<bool(const std::string &line)>;

class FlowController {
public:
  FlowSignal execute(const StatementList &stmts, VariableStore &vars, const LineExecutor &exec);
  FlowSignal run(const std::vector<std::string> &lines, VariableStore &vars, const LineExecutor &exec);

  long long returnValue() const { return returnValue_; }
  void setReturnValue(long long v) { returnValue_ = v; }

  static constexpr int kMaxLoopDepth = 128;

private:
  long long returnValue_ = 0;
  FlowSignal execStatement(const Statement &stmt, VariableStore &vars, const LineExecutor &exec, int depth);
};

} // namespace cmd
} // namespace explo
