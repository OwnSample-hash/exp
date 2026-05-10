#include "interfaces/tool.hpp"
#include <args.hxx>
#include <cmd.hpp>
#include <config.hpp>
#include <dispatcher.hpp>
#include <filesystem>
#include <fstream>
#include <interfaces/tool.hpp>
#include <interfaces/tool_provider.hpp>
#include <iomanip>
#include <list>
#include <map>
#include <memory>
#include <module.hpp>
#include <plugin_interface.hpp>
#include <plugin_loader.hpp>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#include <string.hpp>
#include <ui.hpp>
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
  constexpr char allowed_chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-";
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

int main(int argc, const char **argv, const char **envp) {
  args::ArgumentParser parser("Explo - A modular exploitation framework");
  args::CompletionFlag completion(parser, {"complete"});
  parser.Prog(argv[0]);

  args::ValueFlag<spdlog::level::level_enum> logLevel(
      parser, "log-level",
      "Set log level (trace, debug, info, warn, error, critical)",
      {'l', "log-level"}, spdlog::level::info);

  args::ValueFlag<std::string> logFile(
      parser, "log-file", "Set log file path (default: explo.log)",
      {'f', "log-file"}, std::string("explo.log"));

  args::ValueFlag<std::string> logDir(
      parser, "log-dir", "Set log directory (default: " CONFIG_LOG_DIR ")",
      {'d', "log-dir"}, std::string(CONFIG_LOG_DIR));

  args::ValueFlag<std::string> pluginDir(
      parser, "plugin-dir",
      "Set plugin directory (default: " CONFIG_PLUGIN_INSTALL_DIR ")",
      {'p', "plugin-dir"}, std::string(CONFIG_PLUGIN_INSTALL_DIR));

  args::ValueFlag<std::string> configFile(
      parser, "config-file",
      "Set configuration file path (default: " CONFIG_DEFAULT_CONFIG_FILE ")",
      {'c', "config-file"}, std::string(CONFIG_DEFAULT_CONFIG_FILE));

  args::ValueFlag<std::string> preferredUI(
      parser, "preferred-ui",
      "Set preferred UI (default: " CONFIG_DEFAULT_PREFERRED_UI ")",
      {'P', "preferred-ui"}, std::string(CONFIG_DEFAULT_PREFERRED_UI));

  try {
    parser.ParseCLI(argc, argv);
  } catch (const std::exception &err) {
    if (std::strcmp(err.what(), "Flag could not be matched: 'h'") == 0 ||
        std::strcmp(err.what(), "Flag could not be matched: 'help'") == 0) {
    } else {

      std::cerr << err.what() << std::endl;
      std::cerr << parser;
      std::exit(1);
    }
  }

  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

  std::filesystem::create_directories(std::filesystem::path(logDir.Get()));
  spdlog::set_default_logger(spdlog::basic_logger_mt(
      "main", logDir.Get() + "/" + normalizePath(logFile.Get()), true));
  spdlog::flush_on(spdlog::level::debug);
  spdlog::set_level(logLevel.Get());

  PluginLoader &loader = PluginLoader::instance();

  for (const auto &dir_entry :
       std::filesystem::directory_iterator(pluginDir.Get())) {
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
    spdlog::info(" - Plugin: {} version: {}", entry->getName(),
                 entry->getVersion());
  }

  std::unordered_map<std::string, initArgs> pluginInitArgs;

  for (const auto &plugin : get_loaded_plugins()) {
    std::shared_ptr<std::vector<explo::Module>> plModules =
        std::make_shared<std::vector<explo::Module>>();
    std::shared_ptr<args::Group> pluginGroup =
        std::make_shared<args::Group>(parser, plugin->getName());
    std::shared_ptr<spdlog::logger> plLogger =
        spdlog::basic_logger_mt(plugin->getName(),
                                std::string(CONFIG_LOG_DIR "/") +
                                    normalizePath(plugin->getName()) + ".log",
                                true);

    auto iA = initArgs{plModules, pluginGroup, plLogger};
    pluginInitArgs.emplace(plugin->getName(), iA);

    plugin->initialize(iA);
  }

  try {
    parser.ParseCLI(argc, argv);
  } catch (args::Help) {
    std::cout << parser;
    std::exit(0);
  } catch (const std::exception &err) {
    spdlog::error("Error parsing arguments: {}", err.what());
    std::cerr << err.what() << std::endl;
    std::cerr << parser;
    std::exit(1);
  }

  std::shared_ptr<ITool> currentTool = nullptr;

  // Command
  {
    spdlog::info("Registering global commands...");
    auto &cp = cmd::CommandProcessor::instance();

    {
      auto &vars = cp.vars();
      vars.set("version", cmd::VarValue(std::string("1.0.0")));
      vars.set("current_tool", cmd::VarValue(std::string("no tool")));
      vars.set("prompt",
               cmd::VarValue(std::string("${current_tool} \33[33m>\33[0m ")));
    }
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
        std::stringstream result;
        result << "Loaded plugins:\n";
        for (const auto &entry : get_loaded_plugins()) {
          result << " - " << entry->getName()
                 << " version: " << entry->getVersion() << "\n";
        }
        return result.str();
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
          ss << "  - " << std::setw(15) << cmd.name << std::setw(15)
             << cmd.description << "\n";
        }
        return ss.str();
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "exit";
      c.description = "Exit the application";
      c.variadic = false;
      c.handler = [](const cmd::ExecutionContext &ec) -> std::string {
        std::exit(0);
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "clear";
      c.description = "Clear the screen";
      c.variadic = false;
      c.handler = [](const cmd::ExecutionContext &ec) -> std::string {
        std::cout << "\033[2J\033[H"; // ANSI escape code to clear screen
        return "";
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "mods";
      c.description = "List loaded modules";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        std::stringstream result;
        result << "Loaded modules:\n";
        for (const auto &[plugin, args] : pluginInitArgs) {
          result << "Plugin: " << plugin << "\n";
          for (const auto &mod : *args.modules) {
            result << "  - " << mod.instance->getName() << " "
                   << mod.instance->getVersion() << "\n";
          }
        }
        return result.str();
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "tools";
      c.description = "List loaded tools";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        std::stringstream result;
        result << "Loaded tools:\n";
        for (const auto &[name, tool] : tools) {
          result << " - " << name << " version: " << tool->getVersion() << "\n";
        }
        return result.str();
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "use";
      c.description = "Use tool: tool <tool_name>";
      c.addDynamic("<tool_name>", R"([^\s]+)", "Name of the tool");
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        if (ec.args.size() < 2)
          throw std::runtime_error("Usage: tool <tool_name>");
        std::string tool_name = ec.args[1];
        for (const auto &[name, tool] : tools) {
          if (name == tool_name) {
            if (currentTool) {
              currentTool->suppress();
            }
            currentTool = tool;
            currentTool->invoke(currentTool->getName());
            cp.switchContext(name);
            cp.vars().set("prompt", cmd::VarValue(std::string(
                                        "(" + name + ") \33[33m>\33[0m ")));
            return "Using tool: " + name + "\n";
          }
        }
        return "\033[1;31mTool not found: " + tool_name + "\033[0m";
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "script";
      c.description = "Execute a script: script <script_path>";
      c.addDynamic("<script_path>", R"([^\s]+)", "Path to the script");
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        if (ec.args.size() < 2)
          throw std::runtime_error("Usage: script <script_path>");
        std::string script_path = ec.args[1];
        if (!std::filesystem::exists(script_path)) {
          return "\033[1;31mScript not found: " + script_path + "\033[0m";
        }
        cp.executeScript(script_path);
        return "Executed script: " + script_path + "\n";
      };
      cp.registerGlobalCommand(c);
    }
    {
      cmd::CommandDef c;
      c.name = "rset";
      c.description = "Set a variable without of evaling as an expr. Supports "
                      "% style typing. rset "
                      "<var_name> %s<var_value>";
      c.addDynamic("<var_name>", R"([^\s]+)", "Name of the variable");
      c.addDynamic("<var_value>", R"(.+)", "Value of the variable");
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        if (ec.args.size() < 3)
          throw std::runtime_error("Usage: rset <var_name> <var_value>");
        std::string var_name = ec.args[1];
        std::string var_value = ec.args[2];
        if (var_value.size() > 2 && var_value[0] == '%') {
          if (var_value[1] == 's')
            var_value = var_value.substr(2);
          else if (var_value[1] == 'd')
            var_value = std::to_string(std::stol(var_value.substr(2)));
          else if (var_value[1] == 'f')
            var_value = std::to_string(std::stod(var_value.substr(2)));
          else if (var_value[1] == 'b') {
            std::string val = var_value.substr(2);
            std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            if (val == "true" || val == "1")
              var_value = "true";
            else if (val == "false" || val == "0")
              var_value = "false";
            else
              return "\033[1;31mInvalid boolean value: " + val + "\033[0m";
          } else {
            return "\033[1;31mInvalid type specifier: %" +
                   std::string(1, var_value[1]) +
                   "\033[0m\n"
                   "Supported type specifiers: %s (string), %d (integer), %f "
                   "(float), %b (bool)";
          }
          cp.vars().set(var_name, cmd::VarValue(var_value));
          return "";
        } else {
          cp.vars().set(var_name, cmd::VarValue(var_value));
          return "";
        }
      };
      cp.registerGlobalCommand(c);
    }
    cp.getContext()->sortCommands();
  }

  spdlog::info("Initializing tools...");
  for (const auto &[plugin, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::ModuleType::TOOL) {
        spdlog::debug("Tool: {} version: {}", mod.instance->getName(),
                      mod.instance->getVersion());
        mod.instance->initialize();
        tools.emplace(mod.instance->getName(),
                      std::static_pointer_cast<ITool>(mod.instance));
      }
      if (mod.type == explo::ModuleType::TOOLPROVIDER) {
        auto *provider =
            dynamic_cast<explo::IToolProvider *>(mod.instance.get());
        spdlog::debug("Tool Provider: {} version: {}", provider->getName(),
                      provider->getVersion());
        provider->initialize();
        for (const auto &[name, tool] : provider->getTools()) {
          spdlog::debug("  - Tool: {} version: {}", tool->getName(),
                        tool->getVersion());
          tool->initialize();
          tools.emplace(tool->getName(), std::static_pointer_cast<ITool>(tool));
        }
      }
    }
  }

  Dispatcher dispatcher = Dispatcher(pluginInitArgs, preferredUI.Get());
  dispatcher.runLoop();

  spdlog::info("Shutting down tools...");
  for (const auto &[name, arg] : pluginInitArgs) {
    for (const auto &mod : *arg.modules) {
      if (mod.type == explo::ModuleType::TOOLPROVIDER) {
        auto *provider =
            dynamic_cast<explo::IToolProvider *>(mod.instance.get());
        for (const auto &[name, tool] : provider->getTools()) {
          tool->shutdown();
        }
      } else
        mod.instance->shutdown();
    }
  }

  // clear tools to release resources before plugins are unloaded
  tools.clear();
  return 0;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
