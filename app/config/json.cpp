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
        }
      }
    }
  }
}

} // namespace explo

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
