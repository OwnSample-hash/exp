#pragma once

#include "config/yaml.hpp"
#include "type.hpp"
#include <filesystem>
#include <functional>
#include <interfaces/config_serializer.hpp>
#include <memory>
#include <unordered_map>

namespace fs = std::filesystem;

namespace explo {

enum class LookUpOrder : std::uint8_t {
  ENV = 0b00,
  CONFIG = 0b01,
  ENV_THEN_CONFIG = 0b10,
  CONFIG_THEN_ENV = 0b11,
};

template <typename Serializer = YAMLSerializer>
  requires std::derived_from<Serializer, IConfigSerializer>
class Config {
  Serializer serializer_;
  ConfigMap config_;
  std::unordered_map<std::string, std::shared_ptr<Config>> nested_configs_;
  std::unordered_map<std::string, ConfigValue> env_vars_;
  bool root_ = true;
  using DefaultConfigGenerator = std::function<ConfigMap(void)>;
  DefaultConfigGenerator default_config_generator_;
  fs::path config_file_path_;

  Config(bool root) : root_(root) {}

public:
  explicit Config(const std::string_view path, DefaultConfigGenerator generator_)
      : default_config_generator_(std::move(generator_)), config_file_path_(path) {
    load(path);
  }

  explicit Config(const std::string_view path, const char **envp, DefaultConfigGenerator generator_)
      : default_config_generator_(std::move(generator_)), config_file_path_(path) {
    load(path);
    while (*envp) {
      std::string env_var(*envp);
      if (env_var.starts_with("EXPLO_")) {
        env_var = env_var.substr(6); // Remove "EXPLO_" prefix
      } else {
        ++envp;
        continue;
      }
      auto pos = env_var.find('=');
      if (pos != std::string::npos) {
        std::string key = env_var.substr(0, pos);
        std::string value = env_var.substr(pos + 1);
        env_vars_[key] = value;
      }
      ++envp;
    }
  }

  ~Config() = default;

  const ConfigValue &get(const std::string_view key, LookUpOrder order = LookUpOrder::ENV_THEN_CONFIG) {
    switch (order) {
    case LookUpOrder::ENV:
    case LookUpOrder::ENV_THEN_CONFIG:
      if (env_vars_.find(key.data()) != env_vars_.end()) {
        return env_vars_[key.data()];
      }
      if (order == LookUpOrder::ENV) {
        throw std::runtime_error("Key not found in environment variables: " + std::string(key));
      }
    case LookUpOrder::CONFIG:
    case LookUpOrder::CONFIG_THEN_ENV:
      if (config_.find(key.data()) != config_.end()) {
        return config_[key.data()];
      }
      if (order == LookUpOrder::ENV_THEN_CONFIG || order == LookUpOrder::CONFIG) {
        throw std::runtime_error("Key not found in config: " + std::string(key));
      }
      if (env_vars_.find(key.data()) != env_vars_.end()) {
        return env_vars_[key.data()];
      }
      throw std::runtime_error("Key not found in config or environment variables: " + std::string(key));
    default:
      throw std::runtime_error("Invalid lookup order.");
    };
  }

  void load(const std::string_view file_path) { config_ = serializer_.deserialize(file_path); }

  void save(const std::string_view file_path) { serializer_.serialize(config_, file_path); }

  bool makeDefaultConfig(const std::string_view file_path, const std::string_view plugin_config = "") {
    if (!default_config_generator_) {
      throw std::runtime_error("Default config generator not set.");
    }
    config_ = default_config_generator_();
    for (auto &[key, nested_config] : nested_configs_) {
      nested_config->makeDefaultConfig(file_path);
    }
    save(file_path);
    return true;
  }

  ConfigMap &getConfig(const std::string_view pluginConfig = "") {
    if (!root_) {
      return config_;
    }
    if (!pluginConfig.empty()) {
      if (nested_configs_.find(pluginConfig) == nested_configs_.end()) {
        throw std::runtime_error("Nested config not found: " + std::string(pluginConfig));
      }
      return nested_configs_[pluginConfig]->getConfig();
    }
    return config_;
  }

  Config &operator[](const std::string_view key) {
    if (!root_) {
      throw std::runtime_error("Nested configs cannot be accessed directly.");
    }
    if (nested_configs_.find(key) == nested_configs_.end()) {
      throw std::runtime_error("Nested config not found: " + std::string(key));
    }
    return *nested_configs_[key];
  }

  void addNestedConfig(const std::string_view key, std::shared_ptr<Config> nested_config) {
    if (!root_) {
      throw std::runtime_error("Nested configs cannot be added to non-root configs.");
    }
    if (nested_config->root_) {
      nested_config->root_ = false; // Mark the nested config as non-root
    }
    if (nested_configs_.find(key) != nested_configs_.end()) {
      throw std::runtime_error("Nested config already exists: " + std::string(key));
    }
    nested_configs_[key] = std::move(nested_config);
  }
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
