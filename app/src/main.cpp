#include <config.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <iostream>
#include <list>
#include <module.hpp>
#include <modules.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <spdlog/spdlog.h>
#include <string>

void load_module_dynamic(const char *name) {}

INSTANTIATE_REGISTRY(PluginRegistry);

const std::list<std::unique_ptr<IPlugin>> &get_loaded_plugins() {
  static std::list<std::unique_ptr<IPlugin>> plugins;
  if (plugins.empty()) {
    spdlog::info("Loading registered plugins...");
    for (const auto &entry : PluginRegistry::entries()) {
      spdlog::info("Instantiating plugin: {}", entry.getName());
      plugins.emplace_back(entry.create());
    }
  }
  return plugins;
}

int main() {
  PluginLoader &loader = PluginLoader::instance();

  std::string plugin_path = "";
  std::getline(std::cin, plugin_path);
  if (loader.loadPlugin(plugin_path)) {
    spdlog::info("Loaded plugin from: {}", plugin_path);
  } else {
    auto err = loader.getLastError();
    std::string mangled_name =
        err.find("undefined symbol: ") != std::string::npos
            ? err.substr(err.find("undefined symbol: ") +
                         strlen("undefined symbol: "))
            : "";
    auto demangled_name = abi::__cxa_demangle(mangled_name.c_str(), 0, 0, 0);
    spdlog::error("Failed to load plugin: {}", "");
    spdlog::error("dlsym error: {}", err);
    spdlog::error("Undefined symbol: {}",
                  demangled_name ? demangled_name : mangled_name);
    std::free(demangled_name);
    return 1;
  }

  spdlog::info("Available plugins:");
  for (const auto &entry : get_loaded_plugins()) {
    spdlog::info(" - Plugin: {}", entry->getName());
    entry->execute();
  }
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
