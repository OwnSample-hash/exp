#include "cmd/command_def.hpp"
#include "cmd/command_processor.hpp"
#include "cmd/execution_context.hpp"
#include <argparse/argparse.hpp>
#include <cmd.hpp>
#include <config.hpp>
#include <csignal>
#include <dispatcher.hpp>
#include <fstream>
#include <iomanip>
#include <list>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <plugins.hpp>
#include <simple_ui.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string.hpp>
#include <termios.h>
#include <ui.hpp>
#include <unordered_map>

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

struct termios origTermios;

int main(int argc, const char **argv, const char **envp) {
  struct termios newTermios;
  tcgetattr(STDIN_FILENO, &origTermios);
  atexit([]() { tcsetattr(STDIN_FILENO, TCSANOW, &origTermios); });
  std::memcpy(&newTermios, &origTermios, sizeof(newTermios));
  newTermios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
  signal(SIGINT, SIG_IGN);

  std::filesystem::create_directories(CONFIG_LOG_DIR);
  spdlog::set_default_logger(
      spdlog::basic_logger_mt("main", CONFIG_LOG_DIR "/main.log"));
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::debug);
  const auto now = std::chrono::system_clock::now();
  const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
  spdlog::info("Starting application at {}", std::ctime(&t_c));

  std::shared_ptr<argparse::ArgumentParser> parser =
      std::make_shared<argparse::ArgumentParser>(
          *argv, "1.0.0", argparse::default_arguments::all);

  parser->add_argument("-c", "--config")
      .help("Path to configuration file")
      .default_value(std::string("config.yaml"));

  PluginLoader &loader = PluginLoader::instance();

  for (const auto &dir_entry :
       std::filesystem::directory_iterator(CONFIG_PLUGIN_INSTALL_DIR)) {
    if (dir_entry.is_regular_file() && dir_entry.path().extension() == ".so") {
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
    std::shared_ptr<spdlog::logger> plLogger = spdlog::basic_logger_mt(
        plugin->getName(),
        std::string(CONFIG_LOG_DIR "/") + plugin->getName() + ".log");

    plLogger->info("\nStarting plugin at {} ", std::ctime(&t_c));

    auto iA = initArgs{plModules, parser, plParser, plLogger};
    pluginInitArgs.emplace(plugin->getName(), iA);

    plugin->initialize(iA);
    parser->add_subparser(*plParser.get());
  }

  try {
    parser->parse_args(argc, argv);
  } catch (const std::exception &err) {
    spdlog::error("Error parsing arguments: {}", err.what());
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    std::exit(1);
  }

  auto &cp = cmd::CommandProcessor::instance();

  cp.vars().set("version", cmd::VarValue(std::string("1.0.0")));
  cp.vars().set("prompt", cmd::VarValue(std::string("\33[33m>\33[0m ")));

  // TODO: Make an way to switch to plugin based ui mode defulting to simple_ui
  // Dispatcher dispatcher(pluginInitArgs);
  //
  // if (!dispatcher.isInitialized()) {
  //   spdlog::error("Failed to initialize dispatcher");
  //   std::exit(1);
  // }
  // spdlog::info("Setting base widget UI...");
  // dispatcher.setBaseWidget(nullptr);
  // spdlog::info("Starting main loop...");
  // dispatcher.runLoop();
  {
    cmd::CommandDef c;
    c.name = "mem";
    c.description = "Show memory usage";
    c.variadic = false;
    c.handler = [](const cmd::ExecutionContext &ec) -> std::string {
      std::ifstream fp("/proc/self/stat");
      if (!fp)
        return "Failed to open /proc/self/stat";
      std::string token;
      int field_num = 0;
      double rss = 0;
      while (fp >> token) {
        field_num++;
        if (field_num == 24) { // RSS is the 24th field in /proc/[pid]/stat
          rss = std::stol(token);
          break;
        }
      }
      int dc = 0;
      int page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024; // Get page size in KB
      rss *= page_size_kb; // Convert RSS from pages to KB
      while (rss > 1024) {
        rss /= 1024;
        dc++;
      }
      const char *units[] = {"KB", "MB", "GB", "TB"};
      return "Memory usage: " + std::to_string(rss) + " " + units[dc];
    };
    cp.registerGlobalCommand(c);
  }

  {
    cmd::CommandDef c;
    c.name = "plugins";
    c.description = "List loaded plugins";
    c.variadic = false;
    c.handler = [](const cmd::ExecutionContext &ec) -> std::string {
      std::string result = "Loaded plugins:\n";
      for (const auto &entry : get_loaded_plugins()) {
        result += " - " + std::string(entry->getName()) +
                  " version: " + std::string(entry->getVersion()) + "\n";
      }
      return result;
    };
    cp.registerGlobalCommand(c);
  }

  {
    cmd::CommandDef c;
    c.name = "help";
    c.description = "List of all avaiable commands";
    c.variadic = false;
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream ss;
      ss << std::left;
      ss << "Global commands:\n";
      for (const auto &cmd : cp.getContext()->commands()) {
        ss << "  - " << std::setw(15) << cmd.name << std::setw(15)
           << cmd.description << "\n";
      }
      if (cp.getContext(false).get() == nullptr) {
        ss << "Current commands are not available\n";
        return ss.str();
      }
      ss << "Current commands:\n";
      for (const auto &cmd : cp.getContext(false)->commands()) {
        ss << "  - " << cmd.name << " " << cmd.description << "\n";
      }
      return ss.str();
    };
    cp.registerGlobalCommand(c);
  }

  cp.onHelp(printHelp);
  cp.onAutocomplete(printAutocomplete);
  cp.onExecute(printResult);
  cp.onError(printError);

  runInteractive(cp);

  return 0;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
