#include <algorithm>
#include <cmath>
#include <cmd/variable.hpp>
#include <sstream>
#include <stdexcept>

namespace explo {
namespace cmd {

// ── VarValue
// ──────────────────────────────────────────────────────────────────

std::string VarValue::toString() const {
  switch (type) {
  case VarType::String:
    return sval;
  case VarType::Integer:
    return std::to_string(ival);
  case VarType::Float: {
    std::ostringstream oss;
    oss << fval;
    return oss.str();
  }
  case VarType::Bool:
    return ival ? "true" : "false";
  case VarType::Array: {
    std::string out;
    for (size_t i = 0; i < aval.size(); ++i) {
      if (i)
        out += ' ';
      out += aval[i];
    }
    return out;
  }
  }
  return {};
}

long long VarValue::toInt() const {
  switch (type) {
  case VarType::Integer:
    return ival;
  case VarType::Float:
    return static_cast<long long>(fval);
  case VarType::Bool:
    return ival;
  case VarType::String:
    try {
      return std::stoll(sval);
    } catch (...) {
      return 0;
    }
  case VarType::Array:
    return static_cast<long long>(aval.size());
  }
  return 0;
}

bool VarValue::toBool() const {
  switch (type) {
  case VarType::Integer:
    return ival != 0;
  case VarType::Float:
    return fval != 0.0;
  case VarType::String:
    return !sval.empty() && sval != "0" && sval != "false";
  case VarType::Bool:
    return ival != 0;
  case VarType::Array:
    return !aval.empty();
  }
  return false;
}

VarValue VarValue::add(const VarValue &rhs) const {
  if (type == VarType::String || rhs.type == VarType::String)
    return VarValue(toString() + rhs.toString());
  if (type == VarType::Float || rhs.type == VarType::Float)
    return VarValue(static_cast<double>(toInt()) + static_cast<double>(rhs.toInt()));
  return VarValue(toInt() + rhs.toInt());
}

VarValue VarValue::sub(const VarValue &rhs) const {
  if (type == VarType::Float || rhs.type == VarType::Float)
    return VarValue(static_cast<double>(toInt()) - static_cast<double>(rhs.toInt()));
  return VarValue(toInt() - rhs.toInt());
}

VarValue VarValue::mul(const VarValue &rhs) const {
  if (type == VarType::Float || rhs.type == VarType::Float)
    return VarValue(static_cast<double>(toInt()) * static_cast<double>(rhs.toInt()));
  return VarValue(toInt() * rhs.toInt());
}

VarValue VarValue::div(const VarValue &rhs) const {
  long long d = rhs.toInt();
  if (d == 0)
    throw std::runtime_error("Division by zero");
  if (type == VarType::Float || rhs.type == VarType::Float)
    return VarValue(static_cast<double>(toInt()) / static_cast<double>(rhs.toInt()));
  return VarValue(toInt() / d);
}

bool VarValue::eq(const VarValue &rhs) const {
  if (type == VarType::String || rhs.type == VarType::String)
    return toString() == rhs.toString();
  if (type == VarType::Float || rhs.type == VarType::Float)
    return std::abs(static_cast<double>(toInt()) - static_cast<double>(rhs.toInt())) < 1e-9;
  return toInt() == rhs.toInt();
}

bool VarValue::lt(const VarValue &rhs) const {
  if (type == VarType::String || rhs.type == VarType::String)
    return toString() < rhs.toString();
  return toInt() < rhs.toInt();
}

// ── VariableStore
// ─────────────────────────────────────────────────────────────

VariableStore::VariableStore() {
  scopes_.push_back({}); // global scope
}

void VariableStore::set(const std::string &name, VarValue val) {
  // Walk from innermost scope outward; if found, update in place
  for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
    if (scopes_[i].count(name)) {
      scopes_[i][name] = std::move(val);
      return;
    }
  }
  // Not found: create in current scope
  scopes_.back()[name] = std::move(val);
}

void VariableStore::setGlobal(const std::string &name, VarValue val) { scopes_[0][name] = std::move(val); }

std::optional<VarValue> VariableStore::get(const std::string &name) const {
  for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
    auto it = scopes_[i].find(name);
    if (it != scopes_[i].end())
      return it->second;
  }
  return std::nullopt;
}

std::string VariableStore::expand(const std::string &s) const {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '$') {
      ++i;
      std::string varName;
      if (i < s.size() && s[i] == '{') {
        // ${VAR} form
        ++i;
        while (i < s.size() && s[i] != '}')
          varName += s[i++];
        if (i < s.size())
          ++i; // skip '}'
      } else {
        // $VAR form (alphanumeric + underscore)
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_'))
          varName += s[i++];
      }
      if (!varName.empty()) {
        // Array index: $arr[0]
        if (i < s.size() && s[i] == '[') {
          ++i;
          std::string idxStr;
          while (i < s.size() && s[i] != ']')
            idxStr += s[i++];
          if (i < s.size())
            ++i;
          try {
            size_t idx = std::stoull(idxStr);
            auto elem = getElement(varName, idx);
            if (elem)
              out += elem->toString();
          } catch (...) {
          }
          continue;
        }
        auto v = get(varName);
        if (v)
          out += v->toString();
      } else {
        out += '$';
      }
    } else {
      out += s[i++];
    }
  }
  return out;
}

void VariableStore::pushScope() { scopes_.push_back({}); }

void VariableStore::popScope() {
  if (scopes_.size() > 1)
    scopes_.pop_back();
}

void VariableStore::exportVar(const std::string &name) { exported_[name] = true; }

bool VariableStore::isExported(const std::string &name) const {
  auto it = exported_.find(name);
  return it != exported_.end() && it->second;
}

std::vector<std::string> VariableStore::names() const {
  std::vector<std::string> out;
  for (auto &scope : scopes_)
    for (auto &[k, _] : scope)
      out.push_back(k);
  // deduplicate
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void VariableStore::unset(const std::string &name) {
  for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
    auto it = scopes_[i].find(name);
    if (it != scopes_[i].end()) {
      scopes_[i].erase(it);
      return;
    }
  }
}

std::optional<VarValue> VariableStore::getElement(const std::string &name, size_t idx) const {
  auto v = get(name);
  if (!v || v->type != VarType::Array)
    return std::nullopt;
  if (idx >= v->aval.size())
    return std::nullopt;
  return VarValue(v->aval[idx]);
}

void VariableStore::setElement(const std::string &name, size_t idx, VarValue val) {
  auto v = get(name);
  VarValue arr;
  if (v && v->type == VarType::Array)
    arr = *v;
  else
    arr = VarValue(std::vector<std::string>{});
  if (idx >= arr.aval.size())
    arr.aval.resize(idx + 1);
  arr.aval[idx] = val.toString();
  set(name, std::move(arr));
}

void VariableStore::appendElement(const std::string &name, VarValue val) {
  auto v = get(name);
  VarValue arr;
  if (v && v->type == VarType::Array)
    arr = *v;
  else
    arr = VarValue(std::vector<std::string>{});
  arr.aval.push_back(val.toString());
  set(name, std::move(arr));
}

} // namespace cmd
} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
