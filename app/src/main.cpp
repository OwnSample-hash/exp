#include <args.hxx>
#include <cmd.hpp>
#include <config.hpp>
#include <config/config.hpp>
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
  args::ArgumentParser parser(
      "Explo - A modular exploitation framework\nAny option under \"Global Options\" are parsed before any plugin "
      "options. Plugin options are parsed after the global options and are specific to each plugin.");
  args::CompletionFlag completion(parser, {"complete"});
  parser.Prog(argv[0]);

  args::Group globalGroup(parser, "Global Options");

  args::ValueFlag<spdlog::level::level_enum> logLevel(globalGroup, "log-level",
                                                      "Set log level (trace, debug, info, warn, error, critical)",
                                                      {'l', "log-level"}, spdlog::level::info);

  args::ValueFlag<std::string> logFile(globalGroup, "log-file", "Set log file path (default: explo.log)",
                                       {'f', "log-file"}, std::string("explo.log"));

  args::ValueFlag<std::string> logDir(globalGroup, "log-dir", "Set log directory (default: " CONFIG_LOG_DIR ")",
                                      {'d', "log-dir"}, std::string(CONFIG_LOG_DIR));

  args::ValueFlag<std::string> pluginDir(globalGroup, "plugin-dir",
                                         "Set plugin directory (default: " CONFIG_PLUGIN_INSTALL_DIR ")",
                                         {'p', "plugin-dir"}, std::string(CONFIG_PLUGIN_INSTALL_DIR));

  args::ValueFlag<std::string> configFile(globalGroup, "config-file",
                                          "Set configuration file path (default: " CONFIG_CONFIG_FILE ")",
                                          {'c', "config-file"}, std::string(CONFIG_CONFIG_FILE));

  args::ValueFlag<std::string> preferredUI(globalGroup, "preferred-ui",
                                           "Set preferred UI (default: " CONFIG_PREFERRED_UI ")", {'U', "preferred-ui"},
                                           std::string(CONFIG_PREFERRED_UI));

  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

  auto res = parser.ParseCLI(argc, argv);
  using args::Error;
  switch (parser.GetError()) {
  case Error::Usage:
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Validation:
    std::cerr << "Error validating arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Required:
    std::cerr << "Error: Missing required arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Map:
    std::cerr << "Error mapping arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Extra:
    std::cerr << "Error: Unrecognized arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Subparser:
    std::cerr << "Error parsing subcommand: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Completion:
    std::cout << parser.GetErrorMsg();
    shutdown();
  case Error::Help:
  case Error::Parse:
  case Error::None:
    break;
  default:
    spdlog::error("Unknown argument parsing error: {}", parser.GetErrorMsg());
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  };

  try {
    explo::Config config(configFile.Get(), envp, []() -> explo::ConfigMap {
      return {
#define X(key, value) {key, value},
          CONFIG_OPTS
#undef X
      };
    });
  } catch (const std::exception &e) {
    std::cerr << "Error loading configuration file: " << e.what() << std::endl;
  }

  std::filesystem::create_directories(std::filesystem::path(logDir.Get()));
  spdlog::set_default_logger(spdlog::basic_logger_mt("main", logDir.Get() + "/" + normalizePath(logFile.Get()), true));
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(logLevel.Get());
  spdlog::basic_logger_mt("lib", logDir.Get() + "/lib.log", true);

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
  spdlog::debug("2. Parsed command line arguments successfully {} {}", res, parser.GetErrorMsg());
  spdlog::error("2. Error parsing arguments: {}", parser.GetErrorMsg());
  spdlog::debug("2. Argument parsing error: {}", static_cast<int>(parser.GetError()));
  switch (parser.GetError()) {
  case Error::Usage:
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Parse:
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Validation:
    std::cerr << "Error validating arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Required:
    std::cerr << "Error: Missing required arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Map:
    std::cerr << "Error mapping arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Extra:
    std::cerr << "Error: Unrecognized arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Subparser:
    std::cerr << "Error parsing subcommand: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  case Error::Completion:
    std::cout << parser.GetErrorMsg();
    shutdown();
  case Error::Help:
    std::cout << parser << std::endl;
    shutdown();
  case Error::None:
    break;
  default:
    spdlog::error("Unknown argument parsing error: {}", parser.GetErrorMsg());
    std::cerr << "Error parsing arguments: " << parser.GetErrorMsg() << std::endl;
    std::cerr << parser;
    shutdown(1);
  };

  // Command

  spdlog::info("Registering global commands...");

#include <commands.hpp>

  for (const auto &plugin : get_loaded_plugins()) {
    if (plugin->cmdCheck()) {
      shutdown(0);
    }
  }

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

  rendererModule = dynamic_cast<IRenderer *>(rendererModuleRaw);
  if (!rendererModule) {
    spdlog::error("Error initializing display module: No valid display module found");
    goto quit;
  }

  rendererModule->initialize();
  rendererModule->runLoop();
  rendererModule->shutdown();

quit:
  shutdown(0);
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
