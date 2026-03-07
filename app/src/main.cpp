#include <argparse/argparse.hpp>
#include <config.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <list>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <plugins.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string.hpp>
#include <unordered_map>

#ifndef CONFIG_PLUGIN_INSTALL_DIR
#define CONFIG_PLUGIN_INSTALL_DIR "plugins"
#endif

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

const char *BASE_PLUGIN_PATH = CONFIG_PLUGIN_INSTALL_DIR;

int main(int argc, const char **argv, const char **envp) {
  spdlog::set_level(spdlog::level::debug);

  std::shared_ptr<argparse::ArgumentParser> parser =
      std::make_shared<argparse::ArgumentParser>(
          *argv, "1.0.0", argparse::default_arguments::all);

  parser->add_argument("-c", "--config")
      .help("Path to configuration file")
      .default_value(std::string("config.yaml"));

  parser->add_argument("-ni", "--no-interactive")
      .help("Start in headless mode")
      .default_value(true)
      .implicit_value(false);

  PluginLoader &loader = PluginLoader::instance();

  for (const auto &dir_entry :
       std::filesystem::directory_iterator(BASE_PLUGIN_PATH)) {
    if (dir_entry.is_regular_file() && dir_entry.path().extension() ==
#if defined(_WIN32) || defined(_WIN64)
                                           ".dll"
#elif defined(__APPLE__)
                                           ".dylib"
#else
                                           ".so"
#endif
    ) {
      std::string plugin_path = dir_entry.path().string();
      if (loader.loadPlugin(plugin_path)) {
        spdlog::info("Loaded plugin from: {}", plugin_path);
      } else {
        spdlog::error("Failed to load plugin from: {}", plugin_path);
        spdlog::error("dlsym error: {}", loader.getLastError());
      }
    }
  }

  spdlog::info("Available plugins:");
  for (const auto &entry : get_loaded_plugins()) {
    spdlog::info(" - Plugin: {} version: {}", entry->getName(),
                 entry->getVersion());
  }

  std::unordered_map<std::string, initArgs> pluginInitArgs;

  for (const auto &plugin : get_loaded_plugins()) {
    std::shared_ptr<std::list<explo::Module>> plModules =
        std::make_shared<std::list<explo::Module>>();
    std::shared_ptr<argparse::ArgumentParser> plParser =
        std::make_shared<argparse::ArgumentParser>(
            plugin->getName(), plugin->getVersion(),
            argparse::default_arguments::all);
    std::shared_ptr<spdlog::logger> plLogger =
        spdlog::stdout_color_mt(plugin->getName());

    pluginInitArgs.emplace(plugin->getName(),
                           initArgs{plModules, parser, plParser, plLogger});

    plugin->initialize(pluginInitArgs[plugin->getName()]);
    parser->add_subparser(*plParser.get());
  }

  try {
    parser->parse_args(argc, argv);
  } catch (const std::exception &err) {
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    std::exit(1);
  }

  IMod *displayModule = nullptr;
  for (const auto &[name, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::MODULE_TYPE_DISPLAY) {
        displayModule = mod.instance.get();
        spdlog::info("Using display module: {} from plugin: {}", mod.name,
                     name);
        break;
      } else {
        spdlog::debug("Module: {} from plugin: {} is not a display module",
                      mod.name, name);
      }
    }
    if (displayModule) {
      break;
    }
  }

  displayModule->initialize();

  return 0;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
