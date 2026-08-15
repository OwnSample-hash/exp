#pragma once
#include <algorithm>
#include <format>
#include <fstream>
#include <iomanip>
#include <sstream>

#define CHECK(cmds, cmd)                                                                                               \
  std::find_if(cmds.begin(), cmds.end(), [&](const auto &c) { return c.name == cmd; }) == cmds.end()

{
  auto &cp = cmd::CommandProcessor::instance();
  auto &cmds = cp.getContext()->commands();

  {
    auto &vars = cp.vars();
    vars.set("version", cmd::VarValue(std::string("1.0.0")));
    vars.set("current_tool", cmd::VarValue(std::string("no tool")));
    vars.set("prompt", cmd::VarValue(std::string("${current_tool} \33[33m>\33[0m ")));
  }
  if (CHECK(cmds, "mem")) {
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
      rss *= sysconf(_SC_PAGE_SIZE); // Convert RSS from pages to B
      while (rss > 1024) {
        rss /= 1024;
        dc++;
      }
      const char *units[] = {"B", "KB", "MB", "GB", "TB"};
      return std::format("Memory usage: {} {}", rss, units[dc]);
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "plugins")) {
    cmd::CommandDef c;
    c.name = "plugins";
    c.description = "List loaded plugins";
    c.variadic = false;
    c.handler = [](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream result;
      result << "Loaded plugins:\n";
      for (const auto &entry : get_loaded_plugins()) {
        result << " - " << entry->getName() << " version: " << entry->getVersion() << "\n";
      }
      return result.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "help")) {
    cmd::CommandDef c;
    c.name = "help";
    c.description = "List of all avaiable commands";
    c.variadic = false;
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream ss;
      ss << std::left;
      ss << "Global commands:\n";
      for (const auto &cmd : cp.getContext()->commands()) {
        ss << "  - " << std::setw(15) << cmd.name << std::setw(15) << cmd.description << "\n";
      }
      if (cp.getContext(false).get() == nullptr) {
        ss << "Current commands are not available\n";
        return ss.str();
      }
      ss << "Current commands:\n";
      for (const auto &cmd : cp.getContext(false)->commands()) {
        ss << "  - " << std::setw(15) << cmd.name << std::setw(15) << cmd.description << "\n";
      }
      return ss.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "exit")) {
    cmd::CommandDef c;
    c.name = "exit";
    c.description = "Exit the application";
    c.variadic = false;
    c.handler = [](const cmd::ExecutionContext &ec) -> std::string { ::shutdown(); };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "clear")) {
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
  if (CHECK(cmds, "mods")) {
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
          result << "  - " << mod.instance->getName() << " " << mod.instance->getVersion() << "\n";
        }
      }
      return result.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "tools")) {
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
  if (CHECK(cmds, "use")) {
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
          cp.vars().set("prompt", cmd::VarValue(std::string("(" + name + ") \33[33m>\33[0m ")));
          return "Using tool: " + name + "\n";
        }
      }
      return "\033[1;31mTool not found: " + tool_name + "\033[0m";
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "script")) {
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
  if (CHECK(cmds, "rset")) {
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
          cp.vars().set(var_name, cmd::VarValue(var_value.substr(2)));
        else if (var_value[1] == 'd')
          cp.vars().set(var_name, cmd::VarValue(std::stoll(var_value.substr(2))));
        else if (var_value[1] == 'f')
          cp.vars().set(var_name, cmd::VarValue(std::stod(var_value.substr(2))));
        else if (var_value[1] == 'b') {
          std::string val = var_value.substr(2);
          std::transform(val.begin(), val.end(), val.begin(), ::tolower);
          if (val == "true" || val == "1")
            cp.vars().set(var_name, cmd::VarValue(true));
          else if (val == "false" || val == "0")
            cp.vars().set(var_name, cmd::VarValue(false));
          else
            return "\033[1;31mInvalid boolean value: " + val + "\033[0m";
        } else {
          return "\033[1;31mInvalid type specifier: %" + std::string(1, var_value[1]) +
                 "\033[0m\n"
                 "Supported type specifiers: %s (string), %d (integer), %f "
                 "(float), %b (bool)";
        }
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
// Vim: set expandtab tabstop=2 shiftwidth=2:
