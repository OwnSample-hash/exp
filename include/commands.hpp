#pragma once

#define CHECK(cmds, cmd)                                                                                               \
  std::find_if(cmds.begin(), cmds.end(), [&](const auto &c) { return c.name == cmd; }) == cmds.end()

{
  auto &cp = cmd::CommandProcessor::instance();
  auto &cmds = cp.getContext()->commands();

  // Shell-like path completion for any argument that takes a file.
  // Returns every entry matching what was typed, spelled exactly as typed (so "~/", "../" and relative
  // prefixes are kept) and sorted. Directories end in '/' so you can keep tabbing into them. Dotfiles are
  // hidden unless the typed name starts with '.'. The command processor then completes to the common
  // prefix of these candidates, or to the full path when there is only one.
  auto completePath = [](const std::string &partial) -> std::vector<std::string> {
    namespace fs = std::filesystem;
    const auto slash = partial.rfind('/');
    const std::string typedDir = slash == std::string::npos ? "" : partial.substr(0, slash + 1);
    const std::string typedName = slash == std::string::npos ? partial : partial.substr(slash + 1);

    // Resolve the directory part to a real location, expanding a leading "~".
    fs::path lookup = typedDir.empty() ? fs::path(".") : fs::path(typedDir);
    if (!typedDir.empty() && typedDir[0] == '~' && (typedDir.size() == 1 || typedDir[1] == '/')) {
      if (const char *home = std::getenv("HOME"))
        lookup = fs::path(home) / typedDir.substr(std::min<std::size_t>(2, typedDir.size()));
    }

    std::vector<std::string> matches;
    std::error_code ec;
    fs::directory_iterator it(lookup, fs::directory_options::skip_permission_denied, ec);
    if (ec)
      return matches;
    const bool showHidden = !typedName.empty() && typedName[0] == '.';
    for (const auto &entry : it) {
      std::string name = entry.path().filename().string();
      if (!name.starts_with(typedName) || (name[0] == '.' && !showHidden))
        continue;
      std::error_code dirEc; // follows symlinks, like a shell does
      matches.push_back(typedDir + name + (entry.is_directory(dirEc) ? "/" : ""));
    }
    std::sort(matches.begin(), matches.end());
    return matches;
  };

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
      for (const auto &entry : loadedPlugins) {
        result << " - " << entry << "\n";
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
  if (CHECK(cmds, "tools")) {
    cmd::CommandDef c;
    c.name = "tools";
    c.description = "List loaded tools";
    c.variadic = false;
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream result;
      result << "Loaded tools:\n";
      for (const auto &[name, tool] : tools) {
        result << " - " << name << " version: " << tool->getVersion().toString() << "\n";
      }
      return result.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "mods")) {
    cmd::CommandDef c;
    c.name = "mods";
    c.description = "List loaded mods";
    c.variadic = false;
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream result;
      result << "Loaded mods:\n";
      for (const auto &mod : GetLoadedMods()) {
        result << " - " << mod->getName() << " version: " << mod->getVersion().toString() << "\n";
      }
      return result.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "use")) {
    cmd::CommandDef c;
    c.name = "use";
    c.description = "Use tool: tool <tool_name>";
    c.addDynamic("<tool_name>", R"([^\s]+)", "Name of the tool",
                 [&](const std::string &partial) -> std::vector<std::string> {
                   std::vector<std::string> matches;
                   if (partial.empty())
                     for (const auto &[name, tool] : tools)
                       matches.push_back(name);
                   else
                     for (const auto &[name, tool] : tools) {
                       if (name.find(partial) == 0)
                         matches.push_back(name);
                     }
                   return matches;
                 });
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
          cp.vars().set("current_tool", cmd::VarValue(name));
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
    c.addDynamic("<script_path>", R"([^\s]+)", "Path to the script", completePath);
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
    c.addDynamic("<var_name>", R"([^\s]+)", "Name of the variable",
                 [&](const std::string &partial) -> std::vector<std::string> {
                   auto &vars = cp.vars();
                   std::vector<std::string> matches;
                   if (partial.empty()) {
                     matches.insert_range(matches.end(), vars.names());
                   } else
                     for (const auto name : vars.names()) {
                       if (name.find(partial) == 0)
                         matches.push_back(name);
                     }
                   return matches;
                 });
    c.addDynamic("<var_value>", R"(.+)", "Value of the variable");
    c.variadic = true;
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
        else if (var_value[1] == 'a') {
          std::vector<std::string> arr(ec.args.begin() + 3, ec.args.end());
          cp.vars().set(var_name, cmd::VarValue(arr));
        } else if (var_value[1] == 'b') {
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
  if (CHECK(cmds, "uuids")) {
    cmd::CommandDef c;
    c.name = "uuids";
    c.description = "List UUIDs of loaded tools and plugins";
    c.variadic = false;
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      std::stringstream result;
      result << "Loaded tools and plugins UUIDs:\n";
      for (const auto &[name, tool] : tools) {
        result << " - Tool: " << name << " UUID: " << tool->getUUID() << "\n";
      }
      return result.str();
    };
    cp.registerGlobalCommand(c);
  }
  if (CHECK(cmds, "/")) {
    cmd::CommandDef c;
    c.name = "/";
    c.description = "Search in the tools tags";
    c.variadic = false;
    c.addDynamic("<tag to search for>", R"(.+)", "tag to search");
    c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
      if (ec.args.size() < 2)
        throw std::runtime_error("Usage: / <tag_name>");
      std::map<std::string, std::vector<std::string>> tags;
      for (const auto &[name, tool] : tools) {
        auto &tags_ = tool->getTags();
        tags[name] = {tags_.begin(), tags_.end()};
      }
      std::vector<std::tuple<double, std::string, std::string>> res = extract(ec.args[1], tags, 50.0);

      std::sort(res.begin(), res.end(), [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });
      std::stringstream ss;
      std::string cTool;
      for (const auto &r : res) {
        const double &score = std::get<0>(r);
        const std::string &toolName = std::get<1>(r);
        const std::string &tag = std::get<2>(r);
        if (toolName != cTool) {
          cTool = toolName;
          ss << toolName << ":\n" << "  " << tag << "(" << score << "%) Tool's tags: ";
          for (const auto atag : tags[toolName])
            ss << atag << " ";
          ss << '\n';
        } else {
          ss << "  " << tag << "(" << score << "%)\n";
        }
      }
      ss << "\n";
      return ss.str();
    };
    cp.registerGlobalCommand(c);
  }
  cp.getContext()->sortCommands();
}
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
