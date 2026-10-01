#include <config/config.hpp>
#include <filesystem>
#include <lua_exp.hpp>
#include <lua_exp_config.hpp>
#include <plugin_interface.hpp>
#include <tool.hpp>

void luaExp::initialize(initArgs &args) {
  this->parser = args.parser;
  this->logger = args.logger;
  this->logger->info("Initializing Loader v{}...", getVersion());
  Config &instance = Config::instance();

  instance.addHive("lua_exp", "lua_exp",
                   ConfigMap{
#define X(k, v) {k, v},
                       CONFIG_LUA_EXP_OPTS
#undef X
                   },
                   "yaml", LookUpOrder::ENV_THEN_CONFIG);

  if (!instance.loadHive("lua_exp", "lua_exp", "yaml")) {
    this->logger->warn("Failed to load Lua Exp config");
    instance.saveConfigFile("lua_exp");
  } else {
    this->logger->info("Loaded Lua Exp config");
  }

  this->logger->info("Scanning for Lua scripts at \"{}\"", CONFIG_LUA_EXP_DIRECTORY);

  for (const auto &entry : std::filesystem::directory_iterator(CONFIG_LUA_EXP_DIRECTORY)) {
    if ((entry.is_regular_file() && entry.path().extension() == ".lua") ||
        (entry.is_directory() && std::filesystem::exists(entry.path() / "init.lua"))) {
      this->logger->info("Found Lua script: {}", entry.path().string());
      auto path = entry.path();
      path.replace_extension("");
      auto tool = std::make_shared<luaTool>(this->logger, path.string(), this->parser);
      tool->initialize(args);
      this->tools.emplace(tool->getName(), std::move(tool));
    }
  }
}

void luaExp::shutdown() {
  // Perform any necessary cleanup here
}

bool luaExp::cmdCheck() {
  for (const auto &[name, tool] : this->tools) {
    if (tool->cmdCheck()) {
      logger->info("Lua tool '{}' handled the command", name);
      return true;
    }
  }
  return false;
}

static ToolProviderRegistry::Add<luaExp> luaLoaderRegister("lua_exp");
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
