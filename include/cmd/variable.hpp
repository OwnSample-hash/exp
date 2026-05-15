#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace explo {
namespace cmd {

// Value types supported by the variable system
enum class VarType { String, Integer, Float, Array, Bool };

struct VarValue {
  VarType type = VarType::String;

  std::string sval;
  long long ival = 0;
  double fval = 0.0;
  std::vector<std::string> aval;

  // Constructors
  VarValue() = default;
  explicit VarValue(std::string s)
      : type(VarType::String), sval(std::move(s)) {}
  explicit VarValue(const char *s) : type(VarType::String), sval(s) {}
  explicit VarValue(long long i) : type(VarType::Integer), ival(i) {}
  explicit VarValue(double d) : type(VarType::Float), fval(d) {}
  explicit VarValue(std::vector<std::string> a)
      : type(VarType::Array), aval(std::move(a)) {}
  explicit VarValue(bool b) : type(VarType::Bool), ival(b ? 1 : 0) {}

  // Coercion to string (for expansion)
  std::string toString() const;
  // Coercion to int (for conditions)
  long long toInt() const;
  // Coercion to bool (for conditions)
  bool toBool() const;
  // Arithmetic
  VarValue add(const VarValue &rhs) const;
  VarValue sub(const VarValue &rhs) const;
  VarValue mul(const VarValue &rhs) const;
  VarValue div(const VarValue &rhs) const;
  bool eq(const VarValue &rhs) const;
  bool lt(const VarValue &rhs) const;
};

// ── Scope
// ─────────────────────────────────────────────────────────────────────
// Variables are scoped (function/block scope stack).

class VariableStore {
public:
  VariableStore();

  // Set/get in current scope
  void set(const std::string &name, VarValue val);
  // Set in global scope (outermost)
  void setGlobal(const std::string &name, VarValue val);

  // Lookup (walks scope stack outward)
  std::optional<VarValue> get(const std::string &name) const;

  // Expand $VAR and ${VAR} in a string
  std::string expand(const std::string &s) const;

  // Scope management
  void pushScope();
  void popScope();

  // Export a variable name (visible to sub-executions)
  void exportVar(const std::string &name);
  bool isExported(const std::string &name) const;

  // Enumerate all visible names
  std::vector<std::string> names() const;

  // Unset a variable from the current or any outer scope
  void unset(const std::string &name);

  // Array element access
  std::optional<VarValue> getElement(const std::string &name, size_t idx) const;
  void setElement(const std::string &name, size_t idx, VarValue val);
  void appendElement(const std::string &name, VarValue val);

private:
  using Scope = std::unordered_map<std::string, VarValue>;
  std::vector<Scope> scopes_; // scopes_[0] = global
  std::unordered_map<std::string, bool> exported_;
};

} // namespace cmd
} // namespace explo
