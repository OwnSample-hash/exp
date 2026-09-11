#include "config/type.hpp"
#include "interfaces/config_serializer.hpp"
#include <config/yaml.hpp>
#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace explo {

ConfigMap YAMLSerializer::deserialize(const std::string_view file_path) {
  ConfigMap config;
  YAML::Node root = YAML::LoadFile(std::string(file_path));
  for (const auto &node : root) {
    const std::string &key = node.first.as<std::string>();
    const YAML::Node &value_node = node.second;

    if (value_node.IsScalar()) {
      config[key] = ConfigValue(value_node.as<std::string>());
    } else if (value_node.IsMap()) {
      ConfigMap nested_config;
      for (const auto &nested_node : value_node) {
        const std::string &nested_key = nested_node.first.as<std::string>();
        const YAML::Node &nested_value_node = nested_node.second;
        nested_config[nested_key] = ConfigValue(nested_value_node.as<std::string>());
      }
      config[key] = ConfigValue(nested_config);
    }
  }
  return config;
}

YAML::Node convertConfigMapToYAML(const ConfigMap &config) {
  YAML::Node node;
  for (const auto &[key, value] : config) {
    if (std::holds_alternative<std::string>(value.value)) {
      node[key] = std::get<std::string>(value.value);
    } else if (std::holds_alternative<int>(value.value)) {
      node[key] = std::get<int>(value.value);
    } else if (std::holds_alternative<double>(value.value)) {
      node[key] = std::get<double>(value.value);
    } else if (std::holds_alternative<bool>(value.value)) {
      node[key] = std::get<bool>(value.value);
    } else if (std::holds_alternative<ConfigMap>(value.value)) {
      node[key] = convertConfigMapToYAML(std::get<ConfigMap>(value.value));
    }
  }
  return node;
}

void YAMLSerializer::serialize(const ConfigMap &config, const std::string_view file_path) {
  fs::path path(file_path);
  path.parent_path().make_preferred();
  if (!fs::exists(path)) {
    fs::create_directories(path.parent_path());
    throw FileNotFoundError(path.string());
  }
  YAML::Node root = convertConfigMapToYAML(config);
  std::ofstream fout;
  try {
    fout.open(file_path.data());
  } catch (const std::exception &e) {
    throw SerializerError("Failed to open file for writing: " + std::string(file_path) + ". Error: " + e.what());
  }
  fout << root;
}

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
