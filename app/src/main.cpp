#include <args.hxx>
#include <cmd.hpp>
#include <config.hpp>
#include <filesystem>
#include <interfaces/renderer.hpp>
#include <interfaces/tool.hpp>
#include <interfaces/tool_provider.hpp>
#include <list>
#include <map>
#include <memory>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string.hpp>
#include <ui.hpp>
#include <unistd.h>
#include <unordered_map>

using namespace explo;

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

std::string normalizePath(const std::string &path) {
  constexpr char allowed_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";
  std::string normalized;
  for (char c : path) {
    if (std::strchr(allowed_chars, c)) {
      normalized += c;
    } else {
      normalized += '_';
    }
  }
  return normalized;
}

namespace spdlog {
namespace level {

std::istream &operator>>(std::istream &is, spdlog::level::level_enum &level) {
  std::string token;
  is >> token;
  if (token == "trace") {
    level = spdlog::level::trace;
  } else if (token == "debug") {
    level = spdlog::level::debug;
  } else if (token == "info") {
    level = spdlog::level::info;
  } else if (token == "warn") {
    level = spdlog::level::warn;
  } else if (token == "error") {
    level = spdlog::level::err;
  } else if (token == "critical") {
    level = spdlog::level::critical;
  } else {
    is.setstate(std::ios::failbit);
  }
  return is;
}

} // namespace level
} // namespace spdlog

std::map<std::string, std::shared_ptr<ITool>> tools;

std::unordered_map<std::string, initArgs> pluginInitArgs;

thread_local std::shared_ptr<ITool> currentTool = nullptr;

[[noreturn]] void shutdown(int code = 0);

int main(int argc, const char **argv, const char **envp) {
  args::ArgumentParser parser("Explo - A modular exploitation framework");
  args::CompletionFlag completion(parser, {"complete"});
  parser.Prog(argv[0]);

  args::ValueFlag<spdlog::level::level_enum> logLevel(parser, "log-level",
                                                      "Set log level (trace, debug, info, warn, error, critical)",
                                                      {'l', "log-level"}, spdlog::level::info);

  args::ValueFlag<std::string> logFile(parser, "log-file", "Set log file path (default: explo.log)", {'f', "log-file"},
                                       std::string("explo.log"));

  args::ValueFlag<std::string> logDir(parser, "log-dir", "Set log directory (default: " CONFIG_LOG_DIR ")",
                                      {'d', "log-dir"}, std::string(CONFIG_LOG_DIR));

  args::ValueFlag<std::string> pluginDir(parser, "plugin-dir",
                                         "Set plugin directory (default: " CONFIG_PLUGIN_INSTALL_DIR ")",
                                         {'p', "plugin-dir"}, std::string(CONFIG_PLUGIN_INSTALL_DIR));

  args::ValueFlag<std::string> configFile(parser, "config-file",
                                          "Set configuration file path (default: " CONFIG_DEFAULT_CONFIG_FILE ")",
                                          {'c', "config-file"}, std::string(CONFIG_DEFAULT_CONFIG_FILE));

  args::ValueFlag<std::string> preferredUI(parser, "preferred-ui",
                                           "Set preferred UI (default: " CONFIG_DEFAULT_PREFERRED_UI ")",
                                           {'U', "preferred-ui"}, std::string(CONFIG_DEFAULT_PREFERRED_UI));

  auto res = parser.ParseCLI(argc, argv);
  if (!res) {
    /* */
  }

  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

  std::filesystem::create_directories(std::filesystem::path(logDir.Get()));
  spdlog::set_default_logger(spdlog::basic_logger_mt("main", logDir.Get() + "/" + normalizePath(logFile.Get()), true));
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(logLevel.Get());

  PluginLoader &loader = PluginLoader::instance();

  for (const auto &dir_entry : std::filesystem::directory_iterator(pluginDir.Get())) {
    if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".so") {
      std::string plugin_path = dir_entry.path().string();
      if (loader.loadPlugin(plugin_path)) {
        spdlog::info("Loaded plugin from: {}", plugin_path);
      } else {
        spdlog::error("Failed to load plugin from: {}", plugin_path);
        spdlog::error("dlsym error: {}", loader.getLastError());
        throw std::runtime_error("Failed to load plugin: " + plugin_path);
      }
    }
  }

  spdlog::info("Available plugins:");
  for (const auto &entry : get_loaded_plugins()) {
    spdlog::info(" - Plugin: {} version: {}", entry->getName(), entry->getVersion());
  }

  for (const auto &plugin : get_loaded_plugins()) {
    std::shared_ptr<std::vector<explo::Module>> plModules = std::make_shared<std::vector<explo::Module>>();
    std::shared_ptr<args::Group> pluginGroup = std::make_shared<args::Group>(parser, plugin->getName());
    std::shared_ptr<spdlog::logger> plLogger = spdlog::basic_logger_mt(
        plugin->getName(), std::string(CONFIG_LOG_DIR "/") + normalizePath(plugin->getName()) + ".log", true);
    plLogger->set_level(logLevel.Get());
    plLogger->flush_on(spdlog::level::debug);

    auto iA = initArgs{plModules, pluginGroup, plLogger};
    plugin->initialize(iA);

    pluginInitArgs.emplace(plugin->getName(), iA);
  }

  res = parser.ParseCLI(argc, argv);
  spdlog::debug("Parsed command line arguments successfully {}", res);
  if (!res) {
    spdlog::error("Error parsing arguments: {}", parser.GetErrorMsg());
    spdlog::debug("Argument parsing error: {}", static_cast<int>(parser.GetError()));
    switch (parser.GetError()) {
    Usage:
      std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Parse:
      std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Validation:
      std::cerr << "Error validating arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Required:
      std::cerr << "Error: Missing required arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Map:
      std::cerr << "Error mapping arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Extra:
      std::cerr << "Error: Unrecognized arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Help:
      std::cout << parser;
      std::exit(0);
    Subparser:
      std::cerr << "Error parsing subcommand: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    Completion:
    None:
      break;
    default:
      spdlog::error("Unknown argument parsing error: {}", parser.GetErrorMsg());
      std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
      std::cerr << parser;
      std::exit(1);
    };
  }

  // Command

  spdlog::info("Registering global commands...");

#include <commands.hpp>

  spdlog::info("Initializing tools...");
  for (const auto &[plugin, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::ModuleType::TOOL) {
        spdlog::debug("Tool: {} version: {}", mod.instance->getName(), mod.instance->getVersion());
        mod.instance->initialize();
        tools.emplace(mod.instance->getName(), std::static_pointer_cast<ITool>(mod.instance));
      }
      if (mod.type == explo::ModuleType::TOOLPROVIDER) {
        auto *provider = dynamic_cast<explo::IToolProvider *>(mod.instance.get());
        spdlog::debug("Tool Provider: {} version: {}", provider->getName(), provider->getVersion());
        provider->initialize();
        for (const auto &[name, tool] : provider->getTools()) {
          spdlog::debug("  - Tool: {} version: {}", tool->getName(), tool->getVersion());
          tool->initialize();
          tools.emplace(tool->getName(), std::static_pointer_cast<ITool>(tool));
        }
      }
    }
  }

  std::string_view preferredRenderer = preferredUI.Get();
  IMod *rendererModuleRaw = nullptr;
  for (const auto &[name, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::ModuleType::RENDERER) {
        if (!preferredRenderer.empty() && mod.name != preferredRenderer) {
          spdlog::debug("Skipping renderer module: '{}' from plugin: {} as it "
                        "does not match preferred renderer: '{}'",
                        mod.name, name, preferredRenderer);
          continue;
        }
        rendererModuleRaw = mod.instance.get();
        spdlog::info("Using display module: {} from plugin: {}", mod.name, name);
        break;
      } else {
        spdlog::debug("Module: {} from plugin: {} is not a display module", mod.name, name);
      }
    }
    if (rendererModuleRaw) {
      break;
    }
  }

  IRenderer *rendererModule = nullptr;

  try {
    rendererModule = dynamic_cast<IRenderer *>(rendererModuleRaw);
    if (!rendererModule) {
      throw std::runtime_error("No valid display module found");
    }
  } catch (const std::exception &e) {
    spdlog::error("Error initializing display module: {}", e.what());
    goto quit;
  }

  rendererModule->initialize();
  rendererModule->runLoop();
  rendererModule->shutdown();

quit:
  shutdown();
  return -1;
}

void shutdown(int code) {
  spdlog::info("Shutting down tools...");
  for (const auto &[name, arg] : pluginInitArgs) {
    for (const auto &mod : *arg.modules) {
      spdlog::debug("Shutting down module: {} from plugin: {}", mod.name, name);
      if (mod.type == explo::ModuleType::TOOLPROVIDER) {
        auto *provider = dynamic_cast<explo::IToolProvider *>(mod.instance.get());
        for (const auto &[name, tool] : provider->getTools()) {
          tool->shutdown();
        }
      } else
        mod.instance->shutdown();
    }
    arg.modules->clear();
  }
  pluginInitArgs.clear();

  // clear tools to release resources before plugins are unloaded
  tools.clear();
  std::exit(code);
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
