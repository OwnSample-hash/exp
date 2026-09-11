#pragma once

#include "../config/type.hpp"
#include <stdexcept>
#include <string_view>

namespace explo {

class SerializerError : public std::runtime_error {
public:
  explicit SerializerError(const std::string &message) : std::runtime_error(message) {}
};

class FileNotFoundError : public SerializerError {
public:
  explicit FileNotFoundError(const std::string &file_path) : SerializerError("File not found: " + file_path) {}
};

class IConfigSerializer {

public:
  virtual ~IConfigSerializer() = default;

  virtual ConfigMap deserialize(const std::string_view file_path) = 0;

  virtual void serialize(const ConfigMap &config, const std::string_view file_path) = 0;
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
