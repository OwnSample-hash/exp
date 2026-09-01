#include <filesystem>
#include <loader.hpp>
#include <lua_exp_config.hpp>
#include <tool.hpp>

void luaLoader::initialize() {
  this->logger->info("Initializing Loader v{}...", getVersion());
  this->logger->info("Scanning for Lua scripts at \"{}\"", CONFIG_LUA_EXP_DIRECTORY);

  for (const auto &entry : std::filesystem::directory_iterator(CONFIG_LUA_EXP_DIRECTORY)) {
    if ((entry.is_regular_file() && entry.path().extension() == ".lua") ||
        (entry.is_directory() && std::filesystem::exists(entry.path() / "init.lua"))) {
      this->logger->info("Found Lua script: {}", entry.path().string());
      auto path = entry.path();
      path.replace_extension("");
      auto tool = std::make_shared<luaTool>(this->logger, path.string());
      this->tools.emplace(tool->getName(), std::move(tool));
    }
  }
}

void luaLoader::shutdown() {
  // Perform any necessary cleanup here
}
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
