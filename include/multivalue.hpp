#pragma once

#include <cassert>
#include <variant>

namespace explo {

template <typename... Types> struct MultiValue {
  std::variant<Types...> data;

  // Default constructor
  MultiValue() = default;

  // Construct from any of the variant types
  template <typename T> MultiValue(T &&val) : data(std::forward<T>(val)) {}

  // Assign from any of the variant types
  template <typename T> MultiValue &operator=(T &&val) {
    data = std::forward<T>(val);
    return *this;
  }

  // Get the value as type T (throws std::bad_variant_access if wrong type)
  template <typename T> T &as() { return std::get<T>(data); }

  template <typename T> const T &as() const { return std::get<T>(data); }

  template <typename T> T &as(T defaultValue) {
    if (auto ptr = std::get_if<T>(&data)) {
      return *ptr;
    } else {
      data = defaultValue;
      return std::get<T>(data);
    }
  }

  template <typename T> const T &as(T defaultValue) const {
    if (auto ptr = std::get_if<T>(&data)) {
      return *ptr;
    } else {
      data = defaultValue;
      return std::get<T>(data);
    }
  }

  // Check if the current active type is T
  template <typename T> bool is() const {
    return std::holds_alternative<T>(data);
  }

  // Get a pointer to T, or nullptr if wrong type (non-throwing)
  template <typename T> T *try_as() { return std::get_if<T>(&data); }

  template <typename T> const T *try_as() const {
    return std::get_if<T>(&data);
  }

  operator const std::variant<Types...>() const { return data; }
};

} // namespace explo
