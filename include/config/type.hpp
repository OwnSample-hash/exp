#pragma once

#include <map>
#include <string>
#include <variant>

namespace explo {

struct ConfigValue;

using ConfigMap = std::map<std::string, ConfigValue>;

template <typename Variant, typename T>
concept MakesVaraint = requires(Variant V, T a) {
  { V = a } -> std::same_as<Variant &>;
};

struct ConfigValue {
  using ValueType = std::variant<std::monostate, std::string, int, double, bool, ConfigMap>;
  ValueType value;

  ConfigValue() : value(std::monostate{}) {};
  ConfigValue(std::monostate) : value(std::monostate{}) {}
  ConfigValue(const std::string &str) : value(str) {}
  ConfigValue(int i) : value(i) {}
  ConfigValue(double d) : value(d) {}
  ConfigValue(bool b) : value(b) {}
  ConfigValue(const ConfigMap &map) : value(map) {}
  ~ConfigValue() = default;

  template <typename T>
    requires MakesVaraint<ValueType, T>
  T get(T def = T()) const {
    T *ptr = std::get_if<T>(&value);
    return ptr ? *ptr : def;
  }
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
