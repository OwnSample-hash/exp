#include <args.hxx>
#include <cmd.hpp>
#include <config.hpp>
#include <config/config.hpp>
#include <config/json.hpp>
#include <config/yaml.hpp>
#include <filesystem>
#include <interfaces/renderer.hpp>
#include <interfaces/tool.hpp>
#include <interfaces/tool_provider.hpp>
#include <lib.hpp>
#include <map>
#include <memory>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <session.hpp>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string.hpp>
#include <ui.hpp>
#include <unistd.h>
#include <unordered_map>

using namespace explo;
using namespace std::literals;

INSTANTIATE_REGISTRY(ToolRegistry);
INSTANTIATE_REGISTRY(ToolProviderRegistry);
INSTANTIATE_REGISTRY(RendererRegistry);

const std::vector<std::unique_ptr<IMod>> &GetLoadedPlugins(bool forceReload = false) {
  static std::vector<std::unique_ptr<IMod>> plugins;
  if (plugins.empty()) {
    spdlog::info("Loading registered plugins...");
    for (const auto &entry : RendererRegistry::entries()) {
      spdlog::info("Instantiating renderer: {}", entry.getName());
      plugins.emplace_back(entry.create());
    }
    for (const auto &entry : ToolProviderRegistry::entries()) {
      spdlog::info("Instantiating tool provider: {}", entry.getName());
      plugins.emplace_back(entry.create());
    }
    for (const auto &entry : ToolRegistry::entries()) {
      spdlog::info("Instantiating tool: {}", entry.getName());
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

std::ostream &operator<<(std::ostream &os, const spdlog::level::level_enum &level) {
  switch (level) {
  case spdlog::level::trace:
    os << "trace";
    break;
  case spdlog::level::debug:
    os << "debug";
    break;
  case spdlog::level::info:
    os << "info";
    break;
  case spdlog::level::warn:
    os << "warn";
    break;
  case spdlog::level::err:
    os << "error";
    break;
  case spdlog::level::critical:
    os << "critical";
    break;
  default:
    os.setstate(std::ios::failbit);
  }
  return os;
}

} // namespace level
} // namespace spdlog

namespace explo {

ConfigMap envVars{};

}

std::map<std::string, ITool *> tools = {};

std::unordered_map<std::string, explo::initArgs> pluginInitArgs = {};

thread_local ITool *currentTool = nullptr;

[[noreturn]] void shutdown(int code = 0);

std::unique_ptr<explo::SessionStore> sessionStore = nullptr;

int main(int argc, const char **argv, const char **envp) {
  Config &instance = Config::instance();
  instance.addSerializer("yaml", std::make_unique<YAMLSerializer>());
  instance.addSerializer("json", std::make_unique<JSONSerializer>());
  instance.addEnvVars(envp);
  instance.setPrefixPath(fs::current_path());

  args::ArgumentParser parser(
      "Explo - A modular exploitation framework\nAny option under \"Global Options\" are parsed before any plugin "
      "options. Plugin options are parsed after the global options and are specific to each plugin.");
  args::CompletionFlag completion(parser, {"complete"});
  parser.RequireCommand(false);
  parser.Prog(argv[0]);

  args::Group globalGroup(parser, "Global Options");

  args::ValueFlag<spdlog::level::level_enum> logLevel(globalGroup, "log-level",
                                                      "Set log level (trace, debug, info, warn, error, critical)",
                                                      {'l', "log-level"}, spdlog::level::info);

  args::ValueFlag<std::string> logFile(globalGroup, "log-file", "Set log file path (default: " CONFIG_LOG_FILE ")",
                                       {'f', "log-file"}, std::string(CONFIG_LOG_FILE));

  args::ValueFlag<std::string> logDir(globalGroup, "log-dir", "Set log directory (default: " CONFIG_LOG_DIR ")",
                                      {'d', "log-dir"}, std::string(CONFIG_LOG_DIR));

  args::ValueFlag<std::string> pluginDir(globalGroup, "plugin-dir",
                                         "Set plugin directory (default: " CONFIG_PLUGIN_INSTALL_DIR ")",
                                         {'p', "plugin-dir"}, std::string(CONFIG_PLUGIN_INSTALL_DIR));

  args::ValueFlag<std::string> configFile(globalGroup, "config-file",
                                          "Set configuration file path (default: " CONFIG_CONFIG_FILE ")",
                                          {'c', "config-file"}, std::string(CONFIG_CONFIG_FILE));

  args::ValueFlag<std::string> configDir(globalGroup, "config-dir",
                                         "Set configuration directory path (default: " CONFIG_CONFIG_DIR ")",
                                         {'C', "config-dir"}, std::string(CONFIG_CONFIG_DIR));

  args::ValueFlag<std::string> preferredUI(globalGroup, "preferred-ui",
                                           "Set preferred UI (default: " CONFIG_PREFERRED_UI ")", {'U', "preferred-ui"},
                                           std::string(CONFIG_PREFERRED_UI));

  args::ValueFlag<std::string> sessionStorePath(globalGroup, "session-store",
                                                "Set session store file path (default: " CONFIG_SESSION_STORE ")",
                                                {'s', "session-store"}, std::string(CONFIG_SESSION_STORE));

  args::ValueFlag<std::string> sessionSocketPath(globalGroup, "session-socket",
                                                 "Set session socket path (default: " CONFIG_SESSION_SOCKET ")",
                                                 {'S', "session-socket"}, std::string(CONFIG_SESSION_SOCKET));

  args::ValueFlag<bool> sessionMode(globalGroup, "session-mode", "Enable session mode (default: false)",
                                    {'m', "session-mode"}, false);

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
    break;
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

  fs::path configFilePath(configFile.Get());
  instance.addHive("global", configFilePath,
                   ConfigMap{
#define X(k, v) {k, v},
                       CONFIG_OPTS
#undef X
                       {"log_level", spdlog::level::info},
                   },
                   "yaml", LookUpOrder::ENV_THEN_CONFIG);

  if (!instance.loadHive(configFilePath.string(), "global", "yaml")) {
    spdlog::warn("Failed to load configuration file: {}{}", configFilePath.string(),
                 instance.getSerializerSuffix("yaml"));
    instance.saveConfigFile("global");
  } else {
    spdlog::info("Loaded configuration file: {}{}", configFilePath.string(), instance.getSerializerSuffix("yaml"));
  }

  Hive &globalHive = instance.get("global");

  auto logLevelEnum = static_cast<spdlog::level::level_enum>(
      globalHive.at("log_level"s, logLevel).get(static_cast<int>(spdlog::level::info)));
  std::string logFileStr = globalHive.at("log_file"s, logFile).get<std::string>(CONFIG_LOG_FILE);
  std::string logDirStr = globalHive.at("log_dir"s, logDir).get<std::string>(CONFIG_LOG_DIR);
  std::string pluginDirStr =
      globalHive.at("plugin_install_dir"s, pluginDir).get<std::string>(CONFIG_PLUGIN_INSTALL_DIR);
  std::string preferredUIStr = globalHive.at("preferred_ui"s, preferredUI).get<std::string>(CONFIG_PREFERRED_UI);
  std::string configDirStr = globalHive.at("config_dir"s, configDir).get<std::string>(CONFIG_CONFIG_DIR);
  std::string sessionStorePathStr =
      globalHive.at("session_store"s, sessionStorePath).get<std::string>(CONFIG_SESSION_STORE);
  std::string sessionSocketPathStr =
      globalHive.at("session_socket"s, sessionSocketPath).get<std::string>(CONFIG_SESSION_SOCKET);

  fs::create_directories(configDirStr);
  instance.setPrefixPath(configDirStr);

  fs::path logDirPath(logDirStr);
  std::filesystem::create_directories(logDirPath);
  spdlog::set_default_logger(
      spdlog::basic_logger_mt("main", logDirPath.string() + "/" + normalizePath(logFileStr), true));
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(logLevel.Get());
  spdlog::basic_logger_mt("lib", logDirStr + "/lib.log", true);

  PluginLoader &loader = PluginLoader::instance();

  for (const auto &dir_entry : std::filesystem::directory_iterator(pluginDirStr)) {
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

  for (const auto &plugin : GetLoadedPlugins()) {
    spdlog::info(" - Module: {} version: {}", plugin->getName(), plugin->getVersion());
    std::shared_ptr<args::Group> pluginGroup = std::make_shared<args::Group>(parser, plugin->getName());
    std::shared_ptr<spdlog::logger> plLogger = spdlog::basic_logger_mt(
        plugin->getName(), logDirPath.string() + "/" + normalizePath(plugin->getName()) + ".log", true);
    plLogger->set_level(logLevel.Get());
    plLogger->flush_on(spdlog::level::debug);

    auto iA = explo::initArgs{pluginGroup, plLogger};
    plugin->initialize(iA);

    switch (plugin->getModuleType()) {
    case explo::ModuleType::TOOL:
      tools.emplace(plugin->getName(), dynamic_cast<explo::ITool *>(plugin.get()));
      break;
    case explo::ModuleType::TOOLPROVIDER:
      for (const auto &[name, tool] : dynamic_cast<explo::IToolProvider *>(plugin.get())->getTools()) {
        tools.emplace(name, std::dynamic_pointer_cast<explo::ITool>(tool).get());
      }
      break;
    case explo::ModuleType::RENDERER:
    default:
      spdlog::debug("entry: {} version: {} type: {}", plugin->getName(), plugin->getVersion(),
                    static_cast<int>(plugin->getModuleType()));
      break;
    }

    pluginInitArgs.emplace(plugin->getName(), iA);
  }

  sessionStore = std::make_unique<explo::SessionStore>(sessionStorePathStr, std::chrono::seconds(1));
  sessionStore->Load([&](const uuids::uuid &toolId) -> explo::HeartBeat {
    auto iter =
        std::find_if(tools.begin(), tools.end(), [&](const auto &pair) { return pair.second->getUUID() == toolId; });
    if (iter != tools.end()) {
      return iter->second->getHeartBeat();
    }
    return explo::HeartBeat{};
  });

  if (sessionMode.Get()) {
    int fd = explo::lib::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      spdlog::error("Failed to create socket: {}", strerror(errno));
      shutdown(1);
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sessionSocketPathStr.c_str(), sizeof(addr.sun_path) - 1);
    int res = explo::lib::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (res < 0) {
      spdlog::error("Failed to bind socket: {}", strerror(errno));
      shutdown(1);
    }
    for (;;) {
      int client_fd = explo::lib::accept(fd, nullptr, nullptr);
      if (client_fd < 0) {
        spdlog::error("Failed to accept connection: {}", strerror(errno));
        continue;
      }
      // Handle the client connection
    }
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
    std::cerr << "Currently does't work\n";
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

  for (const auto &plugin : GetLoadedPlugins()) {
    if (plugin->cmdCheck()) {
      shutdown(0);
    }
  }

  // Command

  spdlog::info("Registering global commands...");

#include <commands.hpp>

  spdlog::info("Initializing tools...");

  std::string_view preferredRenderer = preferredUIStr;
  IMod *rendererModuleRaw = nullptr;
  for (const auto &mod : GetLoadedPlugins()) {
    if (mod->getModuleType() == explo::ModuleType::RENDERER) {
      if (!preferredRenderer.empty() && mod->getName() != preferredRenderer) {
        spdlog::debug("Skipping renderer module: '{}' as it does not match preferred renderer: '{}'", mod->getName(),
                      preferredRenderer);
        continue;
      }
      rendererModuleRaw = mod.get();
      spdlog::info("Using display module: {}", mod->getName());
      break;
    } else {
      spdlog::debug("Module: {} is not a display module", mod->getName());
    }
    if (rendererModuleRaw) {
      break;
    }
  }

  IRenderer *rendererModule = dynamic_cast<IRenderer *>(rendererModuleRaw);
  if (!rendererModule) {
    spdlog::error("Error initializing display module: No valid display module found");
    goto quit;
  }

  rendererModule->initialize(pluginInitArgs[rendererModule->getName()]);
  rendererModule->runLoop();

quit:
  shutdown(0);
  return -1;
}

void shutdown(int code) {
  spdlog::info("Shutting down tools...");
  tools.clear();
  currentTool = nullptr;

  for (const auto &plugin : GetLoadedPlugins()) {
    plugin->shutdown();
  }

  pluginInitArgs.clear();

  std::exit(code);
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
