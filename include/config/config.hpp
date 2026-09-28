#pragma once

#include "args.hxx"
#include <config/type.hpp>
#include <config/yaml.hpp>
#include <filesystem>
#include <interfaces/config_serializer.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace fs = std::filesystem;

namespace explo {

extern ConfigMap envVars;

enum class LookUpOrder : std::uint8_t {
  ENV = 0b00,
  CONFIG = 0b01,
  ENV_THEN_CONFIG = 0b10,
  CONFIG_THEN_ENV = 0b11,
};

struct Hive {
  std::string name;
  std::string serializer;
  fs::path configFile;
  LookUpOrder lookupOrder;
  const ConfigMap defaultValue;

  explicit Hive(const std::string &name, const std::string &serializer, fs::path configFile, LookUpOrder lookupOrder,
                const ConfigMap &defaultValue)
      : name(name), serializer(serializer), configFile(configFile), lookupOrder(lookupOrder),
        defaultValue(defaultValue) {}

  Hive(const Hive &) = delete;
  Hive &operator=(const Hive &) = delete;
  Hive(Hive &&) = delete;
  Hive &operator=(Hive &&) = delete;

  ConfigMap &get() {
    Value.insert(defaultValue.begin(), defaultValue.end());
    return Value;
  }

  template <typename VFT> ConfigValue &at(const std::string_view key, const args::ValueFlag<VFT> &cliVal) {
    if (cliVal.Get() != cliVal.GetDefault()) {
      Cli[key.data()] = cliVal.Get();
      return Cli[key.data()];
    }
    return at_(key);
  }

  ConfigValue &at(const std::string_view key) { return at_(key); }

private:
  ConfigMap Value;
  ConfigMap Cli;

  ConfigValue &at_(const std::string_view key) {
    switch (lookupOrder) {
    case LookUpOrder::ENV:
    case LookUpOrder::ENV_THEN_CONFIG:
      if (envVars.find(key.data()) != envVars.end()) {
        return envVars[key.data()];
      }
      if (lookupOrder == LookUpOrder::ENV) {
        throw std::runtime_error("Key not found in environment variables: " + std::string(key));
      }
    case LookUpOrder::CONFIG:
    case LookUpOrder::CONFIG_THEN_ENV:
      if (Value.find(key.data()) != Value.end()) {
        return Value[key.data()];
      }
      if (lookupOrder == LookUpOrder::CONFIG || lookupOrder == LookUpOrder::ENV_THEN_CONFIG) {
        throw std::runtime_error("Key not found in config: " + std::string(key));
      }
      if (envVars.find(key.data()) != envVars.end()) {
        return envVars[key.data()];
      }
    default:
      throw std::runtime_error("Invalid lookup order or key not found: " + std::string(key));
    }
  }
};

class Config {
  std::unordered_map<std::string, std::unique_ptr<IConfigSerializer>> serializers;
  std::unordered_map<std::string, Hive> entries;

  Config() {};

public:
  ~Config() = default;

  static Config &instance() {
    static Config instance;
    return instance;
  }

  bool addSerializer(const std::string &name, std::unique_ptr<IConfigSerializer> serializer) {
    if (!serializer) {
      return false;
    }
    serializers[name] = std::move(serializer);
    return true;
  }

  bool addHive(const std::string &name, fs::path configFilePath, const ConfigMap &defaultValue,
               const std::string_view serializer = "yaml", LookUpOrder lookupOrder = LookUpOrder::ENV_THEN_CONFIG) {
    if (serializers.find(serializer.data()) == serializers.end()) {
      return false;
    }
    if (entries.find(name) != entries.end()) {
      return false;
    }
    auto [pos, status] =
        entries.try_emplace(name, name, std::string(serializer), configFilePath, lookupOrder, defaultValue);
    return status;
  }

  bool addEnvVars(const char **envp) {
    if (!envp) {
      return false;
    }
    for (const char **env = envp; *env != nullptr; ++env) {
      std::string envVar(*env);
      if (envVar.starts_with("EXPLO_")) {
        envVar = envVar.substr(6);
      } else {
        continue;
      }
      size_t pos = envVar.find('=');
      if (pos != std::string::npos) {
        std::string key = envVar.substr(0, pos);
        std::string value = envVar.substr(pos + 1);
        envVars[key] = value;
      }
    }
    return true;
  }

  bool loadHive(const std::string &filePath, const std::string &hive, const std::string_view serializer = "yaml") {
    fs::path configFilePath(filePath);
    if (!fs::exists(configFilePath)) {
      return false;
    }
    auto it = entries.find(hive);
    if (it == entries.end()) {
      return false;
    }
    auto serializerIt = serializers.find(serializer.data());
    if (serializerIt == serializers.end()) {
      return false;
    }
    IConfigSerializer *serializerPtr = serializerIt->second.get();
    ConfigMap loadedConfig = serializerPtr->deserialize(configFilePath.string());
    it->second.get() = loadedConfig;
    return true;
  }

  bool saveConfigFile(const std::string &hive) {
    auto it = entries.find(hive);
    if (it == entries.end()) {
      return false;
    }
    auto &entry = it->second;
    auto serializerIt = serializers.find(entry.serializer);
    if (serializerIt == serializers.end()) {
      return false;
    }
    IConfigSerializer *serializer = serializerIt->second.get();
    serializer->serialize(entry.get(), entry.configFile.string());
    return true;
  }

  bool saveAll() {
    for (auto &[name, entry] : entries) {
      auto serializerIt = serializers.find(entry.serializer);
      if (serializerIt == serializers.end()) {
        continue;
      }
      IConfigSerializer *serializer = serializerIt->second.get();
      serializer->serialize(entry.get(), entry.configFile.string());
    }
    return true;
  }

  Hive &get(const std::string &name) {
    auto it = entries.find(name);
    if (it != entries.end()) {
      return it->second;
    }
    throw std::runtime_error("Config entry not found: " + name);
  }

  Hive &operator[](const std::string &name) { return get(name); }
};

} // namespace explo
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
