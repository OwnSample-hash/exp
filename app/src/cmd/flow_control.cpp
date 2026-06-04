#include <cctype>
#include <cmd.hpp>
#include <sstream>
#include <stdexcept>
#include <string>

namespace explo {
namespace cmd {

// ─────────────────────────────────────────────────────────────────────────────
// File-local token for the Pratt expression parser
// ─────────────────────────────────────────────────────────────────────────────

struct ExprToken {
  enum Kind {
    Num,
    Var,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Eq,
    Neq,
    Lt,
    Lte,
    Gt,
    Gte,
    And,
    Or,
    Not,
    LParen,
    RParen,
    Eof
  } kind = Eof;
  long long num = 0;
  std::string name;
};

static std::vector<ExprToken> lexExpr(const std::string &src) {
  std::vector<ExprToken> toks;
  size_t i = 0;
  while (i < src.size()) {
    char c = src[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (c == '$') {
      ++i;
      std::string varName;
      if (i < src.size() && src[i] == '{') {
        ++i;
        while (i < src.size() && src[i] != '}')
          varName += src[i++];
        if (i < src.size())
          ++i;
      } else {
        while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_'))
          varName += src[i++];
      }
      toks.push_back({ExprToken::Var, 0, varName});
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      long long num = 0;
      while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i])))
        num = num * 10 + (src[i++] - '0');
      toks.push_back({ExprToken::Num, num, {}});
      continue;
    }
    auto peek = [&](char p) { return i + 1 < src.size() && src[i + 1] == p; };
    switch (c) {
    case '+':
      toks.push_back({ExprToken::Plus, 0, {}});
      ++i;
      break;
    case '-':
      toks.push_back({ExprToken::Minus, 0, {}});
      ++i;
      break;
    case '*':
      toks.push_back({ExprToken::Star, 0, {}});
      ++i;
      break;
    case '/':
      toks.push_back({ExprToken::Slash, 0, {}});
      ++i;
      break;
    case '%':
      toks.push_back({ExprToken::Percent, 0, {}});
      ++i;
      break;
    case '(':
      toks.push_back({ExprToken::LParen, 0, {}});
      ++i;
      break;
    case ')':
      toks.push_back({ExprToken::RParen, 0, {}});
      ++i;
      break;
    case '!':
      if (peek('=')) {
        toks.push_back({ExprToken::Neq, 0, {}});
        i += 2;
      } else {
        toks.push_back({ExprToken::Not, 0, {}});
        ++i;
      }
      break;
    case '=':
      if (peek('=')) {
        toks.push_back({ExprToken::Eq, 0, {}});
        i += 2;
      } else
        throw std::runtime_error("Expression: unexpected '='");
      break;
    case '<':
      if (peek('=')) {
        toks.push_back({ExprToken::Lte, 0, {}});
        i += 2;
      } else {
        toks.push_back({ExprToken::Lt, 0, {}});
        ++i;
      }
      break;
    case '>':
      if (peek('=')) {
        toks.push_back({ExprToken::Gte, 0, {}});
        i += 2;
      } else {
        toks.push_back({ExprToken::Gt, 0, {}});
        ++i;
      }
      break;
    case '&':
      if (peek('&')) {
        toks.push_back({ExprToken::And, 0, {}});
        i += 2;
      } else
        throw std::runtime_error("Expression: expected '&&'");
      break;
    case '|':
      if (peek('|')) {
        toks.push_back({ExprToken::Or, 0, {}});
        i += 2;
      } else
        throw std::runtime_error("Expression: expected '||'");
      break;
    default:
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string name;
        while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_'))
          name += src[i++];
        if (name == "true")
          toks.push_back({ExprToken::Num, 1, {}});
        else if (name == "false")
          toks.push_back({ExprToken::Num, 0, {}});
        else
          toks.push_back({ExprToken::Var, 0, name});
      } else {
        throw std::runtime_error(std::string("Expression: unexpected char '") + c + "'");
      }
    }
  }
  toks.push_back({ExprToken::Eof, 0, {}});
  return toks;
}

static int exprPrec(ExprToken::Kind k) {
  switch (k) {
  case ExprToken::Or:
    return 1;
  case ExprToken::And:
    return 2;
  case ExprToken::Eq:
  case ExprToken::Neq:
    return 3;
  case ExprToken::Lt:
  case ExprToken::Lte:
  case ExprToken::Gt:
  case ExprToken::Gte:
    return 4;
  case ExprToken::Plus:
  case ExprToken::Minus:
    return 5;
  case ExprToken::Star:
  case ExprToken::Slash:
  case ExprToken::Percent:
    return 6;
  default:
    return -1;
  }
}

static long long parsePrimary(const std::vector<ExprToken> &toks, size_t &pos, const VariableStore &vars);
static long long parseExprPrec(const std::vector<ExprToken> &toks, size_t &pos, const VariableStore &vars, int minPrec);

static long long parsePrimary(const std::vector<ExprToken> &toks, size_t &pos, const VariableStore &vars) {
  const ExprToken &t = toks[pos];
  if (t.kind == ExprToken::Num) {
    ++pos;
    return t.num;
  }
  if (t.kind == ExprToken::Var) {
    ++pos;
    auto v = vars.get(t.name);
    return v ? v->toInt() : 0;
  }
  if (t.kind == ExprToken::Minus) {
    ++pos;
    return -parsePrimary(toks, pos, vars);
  }
  if (t.kind == ExprToken::Not) {
    ++pos;
    return parsePrimary(toks, pos, vars) == 0 ? 1 : 0;
  }
  if (t.kind == ExprToken::LParen) {
    ++pos;
    long long val = parseExprPrec(toks, pos, vars, 0);
    if (toks[pos].kind != ExprToken::RParen)
      throw std::runtime_error("Expression: expected ')'");
    ++pos;
    return val;
  }
  throw std::runtime_error("Expression: unexpected token in primary");
}

static long long parseExprPrec(const std::vector<ExprToken> &toks, size_t &pos, const VariableStore &vars,
                               int minPrec) {
  long long lhs = parsePrimary(toks, pos, vars);
  while (true) {
    int prec = exprPrec(toks[pos].kind);
    if (prec <= minPrec)
      break;
    ExprToken::Kind op = toks[pos].kind;
    ++pos;
    long long rhs = parseExprPrec(toks, pos, vars, prec);
    switch (op) {
    case ExprToken::Plus:
      lhs = lhs + rhs;
      break;
    case ExprToken::Minus:
      lhs = lhs - rhs;
      break;
    case ExprToken::Star:
      lhs = lhs * rhs;
      break;
    case ExprToken::Slash:
      if (!rhs)
        throw std::runtime_error("Division by zero");
      lhs = lhs / rhs;
      break;
    case ExprToken::Percent:
      if (!rhs)
        throw std::runtime_error("Modulo by zero");
      lhs = lhs % rhs;
      break;
    case ExprToken::Eq:
      lhs = (lhs == rhs) ? 1 : 0;
      break;
    case ExprToken::Neq:
      lhs = (lhs != rhs) ? 1 : 0;
      break;
    case ExprToken::Lt:
      lhs = (lhs < rhs) ? 1 : 0;
      break;
    case ExprToken::Lte:
      lhs = (lhs <= rhs) ? 1 : 0;
      break;
    case ExprToken::Gt:
      lhs = (lhs > rhs) ? 1 : 0;
      break;
    case ExprToken::Gte:
      lhs = (lhs >= rhs) ? 1 : 0;
      break;
    case ExprToken::And:
      lhs = (lhs && rhs) ? 1 : 0;
      break;
    case ExprToken::Or:
      lhs = (lhs || rhs) ? 1 : 0;
      break;
    default:
      break;
    }
  }
  return lhs;
}

long long Expression::evaluate(const VariableStore &vars) const {
  if (src_.empty())
    return 0;
  auto toks = lexExpr(src_);
  size_t pos = 0;
  return parseExprPrec(toks, pos, vars, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// FlowParser helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string trimLine(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

static std::string firstWord(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  std::string w;
  while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
    w += line[i++];
  return w;
}

static std::string afterFirstWord(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
    ++i;
  return line.substr(i);
}

static std::string extractParen(const std::string &line) {
  size_t a = line.find('('), b = line.rfind(')');
  if (a == std::string::npos || b == std::string::npos || b <= a)
    throw std::runtime_error("Expected condition in ()");
  return line.substr(a + 1, b - a - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// FlowParser
// ─────────────────────────────────────────────────────────────────────────────

StatementList FlowParser::parse(const std::vector<std::string> &lines) const {
  StatementList out;
  size_t i = 0;
  while (i < lines.size()) {
    std::string line = trimLine(lines[i]);
    if (line.empty() || line[0] == '#') {
      ++i;
      continue;
    }
    Statement s = parseStatement(lines, i);
    out.push_back(std::make_shared<Statement>(std::move(s)));
  }
  return out;
}

size_t FlowParser::parseBlock(const std::vector<std::string> &lines, size_t start, StatementList &out) const {
  size_t i = start + 1;
  while (i < lines.size()) {
    std::string line = trimLine(lines[i]);
    if (line == "}")
      return i + 1;
    if (line.empty() || line[0] == '#') {
      ++i;
      continue;
    }
    Statement s = parseStatement(lines, i);
    out.push_back(std::make_shared<Statement>(std::move(s)));
  }
  throw std::runtime_error("FlowParser: unterminated block (missing '}')");
}

Statement FlowParser::parseStatement(const std::vector<std::string> &lines, size_t &i) const {
  std::string line = trimLine(lines[i]);
  std::string kw = firstWord(line);
  Statement stmt;

  // ── if ───────────────────────────────────────────────────────────────────
  if (kw == "if") {
    stmt.kind = StmtKind::If;
    IfClause first(Expression(extractParen(line)), StatementList{});
    ++i;
    i = parseBlock(lines, i, first.body);
    stmt.ifClauses.push_back(std::move(first));

    while (i < lines.size()) {
      std::string next = trimLine(lines[i]);
      if (firstWord(next) != "else")
        break;
      std::string after = trimLine(afterFirstWord(next));
      if (firstWord(after) == "if") {
        IfClause ec(Expression(extractParen(after)), StatementList{});
        ++i;
        i = parseBlock(lines, i, ec.body);
        stmt.ifClauses.push_back(std::move(ec));
      } else {
        ++i;
        i = parseBlock(lines, i, stmt.elsebody);
        break;
      }
    }
    return stmt;
  }

  // ── while ────────────────────────────────────────────────────────────────
  if (kw == "while") {
    stmt.kind = StmtKind::While;
    stmt.whileCond = Expression(extractParen(line));
    ++i;
    i = parseBlock(lines, i, stmt.whileBody);
    return stmt;
  }

  // ── for ──────────────────────────────────────────────────────────────────
  if (kw == "for") {
    std::string inner = extractParen(line);
    size_t inPos = inner.find(" in ");
    if (inPos != std::string::npos) {
      stmt.kind = StmtKind::ForIn;
      stmt.forInVar = trimLine(inner.substr(0, inPos));
      stmt.forInList = trimLine(inner.substr(inPos + 4));
      ++i;
      i = parseBlock(lines, i, stmt.forInBody);
    } else {
      stmt.kind = StmtKind::For;
      std::vector<std::string> parts;
      std::string cur;
      for (char c : inner) {
        if (c == ';') {
          parts.push_back(trimLine(cur));
          cur.clear();
        } else
          cur += c;
      }
      parts.push_back(trimLine(cur));
      if (parts.size() != 3)
        throw std::runtime_error("for: expected 'init; cond; step'");
      stmt.forInit = parts[0];
      stmt.forCond = Expression(parts[1]);
      stmt.forStep = parts[2];
      ++i;
      i = parseBlock(lines, i, stmt.forBody);
    }
    return stmt;
  }

  // ── control flow keywords ─────────────────────────────────────────────────
  if (kw == "break") {
    stmt.kind = StmtKind::Break;
    ++i;
    return stmt;
  }
  if (kw == "continue") {
    stmt.kind = StmtKind::Continue;
    ++i;
    return stmt;
  }
  if (kw == "return") {
    stmt.kind = StmtKind::Return;
    stmt.commandLine = afterFirstWord(line);
    ++i;
    return stmt;
  }

  // ── bare block ────────────────────────────────────────────────────────────
  if (kw == "{") {
    stmt.kind = StmtKind::Block;
    i = parseBlock(lines, i, stmt.blockBody);
    return stmt;
  }

  // ── plain command ─────────────────────────────────────────────────────────
  stmt.kind = StmtKind::Command;
  stmt.commandLine = line;
  ++i;
  return stmt;
}

// ─────────────────────────────────────────────────────────────────────────────
// FlowController
// ─────────────────────────────────────────────────────────────────────────────

FlowSignal FlowController::run(const std::vector<std::string> &lines, VariableStore &vars, const LineExecutor &exec) {
  FlowParser parser;
  auto stmts = parser.parse(lines);
  return execute(stmts, vars, exec);
}

FlowSignal FlowController::execute(const StatementList &stmts, VariableStore &vars, const LineExecutor &exec) {
  for (auto &s : stmts) {
    auto sig = execStatement(*s, vars, exec, 0);
    if (sig != FlowSignal::None)
      return sig;
  }
  return FlowSignal::None;
}

FlowSignal FlowController::execStatement(const Statement &stmt, VariableStore &vars, const LineExecutor &exec,
                                         int depth) {
  if (depth > kMaxLoopDepth)
    throw std::runtime_error("FlowController: maximum loop depth exceeded");

  switch (stmt.kind) {

  case StmtKind::Command: {
    std::string expanded = vars.expand(stmt.commandLine);
    if (!expanded.empty())
      exec(expanded);
    return FlowSignal::None;
  }

  case StmtKind::Block: {
    vars.pushScope();
    FlowSignal sig = FlowSignal::None;
    for (auto &s : stmt.blockBody) {
      sig = execStatement(*s, vars, exec, depth);
      if (sig != FlowSignal::None)
        break;
    }
    vars.popScope();
    return sig;
  }

  case StmtKind::If: {
    for (auto &clause : stmt.ifClauses) {
      if (clause.condition.evaluateBool(vars)) {
        vars.pushScope();
        FlowSignal sig = FlowSignal::None;
        for (auto &s : clause.body) {
          sig = execStatement(*s, vars, exec, depth);
          if (sig != FlowSignal::None)
            break;
        }
        vars.popScope();
        return sig;
      }
    }
    if (!stmt.elsebody.empty()) {
      vars.pushScope();
      FlowSignal sig = FlowSignal::None;
      for (auto &s : stmt.elsebody) {
        sig = execStatement(*s, vars, exec, depth);
        if (sig != FlowSignal::None)
          break;
      }
      vars.popScope();
      return sig;
    }
    return FlowSignal::None;
  }

  case StmtKind::While: {
    vars.pushScope();
    bool brk = false;
    while (stmt.whileCond.evaluateBool(vars)) {
      for (auto &s : stmt.whileBody) {
        FlowSignal sig = execStatement(*s, vars, exec, depth + 1);
        if (sig == FlowSignal::Break) {
          brk = true;
          break;
        }
        if (sig == FlowSignal::Continue)
          break;
        if (sig != FlowSignal::None) {
          vars.popScope();
          return sig;
        }
      }
      if (brk)
        break;
    }
    vars.popScope();
    return FlowSignal::None;
  }

  case StmtKind::For: {
    vars.pushScope();
    if (!stmt.forInit.empty())
      exec(vars.expand(stmt.forInit));
    bool brk = false;
    while (!brk && stmt.forCond.evaluateBool(vars)) {
      for (auto &s : stmt.forBody) {
        FlowSignal sig = execStatement(*s, vars, exec, depth + 1);
        if (sig == FlowSignal::Break) {
          brk = true;
          break;
        }
        if (sig == FlowSignal::Continue)
          break;
        if (sig != FlowSignal::None) {
          vars.popScope();
          return sig;
        }
      }
      if (!brk && !stmt.forStep.empty())
        exec(vars.expand(stmt.forStep));
    }
    vars.popScope();
    return FlowSignal::None;
  }

  case StmtKind::ForIn: {
    vars.pushScope();
    std::vector<std::string> items;
    auto listVar = vars.get(stmt.forInList);
    if (listVar && listVar->type == VarType::Array) {
      items = listVar->aval;
    } else {
      std::istringstream iss(vars.expand(stmt.forInList));
      std::string tok;
      while (iss >> tok)
        items.push_back(tok);
    }
    bool brk = false;
    for (auto &item : items) {
      if (brk)
        break;
      vars.set(stmt.forInVar, VarValue(item));
      for (auto &s : stmt.forInBody) {
        FlowSignal sig = execStatement(*s, vars, exec, depth + 1);
        if (sig == FlowSignal::Break) {
          brk = true;
          break;
        }
        if (sig == FlowSignal::Continue)
          break;
        if (sig != FlowSignal::None) {
          vars.popScope();
          return sig;
        }
      }
    }
    vars.popScope();
    return FlowSignal::None;
  }

  case StmtKind::Break:
    return FlowSignal::Break;
  case StmtKind::Continue:
    return FlowSignal::Continue;

  case StmtKind::Return:
    if (!stmt.commandLine.empty()) {
      try {
        returnValue_ = Expression(vars.expand(stmt.commandLine)).evaluate(vars);
      } catch (...) {
        returnValue_ = 0;
      }
    }
    return FlowSignal::Return;
  }
  return FlowSignal::None;
}

} // namespace cmd
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2:
