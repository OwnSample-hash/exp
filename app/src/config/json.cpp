#include <config/json.hpp>
#include <config/type.hpp>
#include <filesystem>
#include <fstream>
#include <interfaces/config_serializer.hpp>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using JSON = nlohmann::json;

namespace explo {
ConfigMap JSONSerializer::deserialize(const std::string_view file_path) {
  ConfigMap config;
  std::ifstream file(file_path.data());
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open JSON file: " + std::string(file_path));
  }
  JSON json_data;
  file >> json_data;
  for (const auto &[key, value] : json_data.items()) {
    if (value.is_string()) {
      config[key] = ConfigValue(value.get<std::string>());
    } else if (value.is_number_integer()) {
      config[key] = ConfigValue(value.get<int>());
    } else if (value.is_number_float()) {
      config[key] = ConfigValue(value.get<double>());
    } else if (value.is_boolean()) {
      config[key] = ConfigValue(value.get<bool>());
    } else if (value.is_object()) {
      ConfigMap nested_config;
      for (const auto &[nested_key, nested_value] : value.items()) {
        if (nested_value.is_string()) {
          nested_config[nested_key] = ConfigValue(nested_value.get<std::string>());
        } else if (nested_value.is_number_integer()) {
          nested_config[nested_key] = ConfigValue(nested_value.get<int>());
        } else if (nested_value.is_number_float()) {
          nested_config[nested_key] = ConfigValue(nested_value.get<double>());
        } else if (nested_value.is_boolean()) {
          nested_config[nested_key] = ConfigValue(nested_value.get<bool>());
        } else {
          throw std::runtime_error("Unsupported nested JSON value type for key: " + nested_key);
        }
      }
      config[key] = ConfigValue(nested_config);
    }
  }
  return config;
}

JSON serializeConfigMap(const ConfigMap &config) {
  JSON json_data;
  for (const auto &[key, value] : config) {
    if (std::holds_alternative<std::string>(value.value)) {
      json_data[key] = std::get<std::string>(value.value);
    } else if (std::holds_alternative<int>(value.value)) {
      json_data[key] = std::get<int>(value.value);
    } else if (std::holds_alternative<double>(value.value)) {
      json_data[key] = std::get<double>(value.value);
    } else if (std::holds_alternative<bool>(value.value)) {
      json_data[key] = std::get<bool>(value.value);
    } else if (std::holds_alternative<ConfigMap>(value.value)) {
      ConfigMap nested_config = std::get<ConfigMap>(value.value);
      JSON nested_json = serializeConfigMap(nested_config);
      json_data[key] = nested_json;
    }
  }
  return json_data;
}

void JSONSerializer::serialize(const ConfigMap &config, const std::string_view file_path) {
  JSON json_data;
  for (const auto &[key, value] : config) {
    if (std::holds_alternative<std::string>(value.value)) {
      json_data[key] = std::get<std::string>(value.value);
    } else if (std::holds_alternative<int>(value.value)) {
      json_data[key] = std::get<int>(value.value);
    } else if (std::holds_alternative<double>(value.value)) {
      json_data[key] = std::get<double>(value.value);
    } else if (std::holds_alternative<bool>(value.value)) {
      json_data[key] = std::get<bool>(value.value);
    } else if (std::holds_alternative<ConfigMap>(value.value)) {
      ConfigMap nested_config = std::get<ConfigMap>(value.value);
      JSON nested_json = serializeConfigMap(nested_config);
      json_data[key] = nested_json;
    }
  }

  std::ofstream file(file_path.data());
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open JSON file for writing: " + std::string(file_path));
  }
  file << json_data.dump(2); // Pretty print with 2 spaces indentation
}

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
