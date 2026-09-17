#include <filesystem>
#include <lua_exp.hpp>
#include <lua_exp_config.hpp>
#include <plugin_interface.hpp>
#include <tool.hpp>

void luaExp::initialize(initArgs &args) {
  this->parser = args.parser;
  this->logger = args.logger;
  this->logger->info("Initializing Loader v{}...", getVersion());
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

static ToolProviderRegistry::Add<luaExp> luaLoaderRegister("lua_exp");
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
