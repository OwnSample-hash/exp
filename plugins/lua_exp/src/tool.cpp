#include <cmd.hpp>
#include <cxxabi.h>
#include <filesystem>
#include <tool.hpp>
#include <variant>

using namespace explo;

void luaTool::initialize() {
  this->logger->info("Initializing Lua tool: {} v{}...", name, version);
  {
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run the Lua tool's main function";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        this->execute();
        return "Lua tool execution completed";
      };
      ctx->registerCommand(c);
    }
    cmd::CommandProcessor::instance().registerContext(ctx);
  }

  auto init = this->lua["initialize"];
  if (init.is<LFW>()) {
    auto func = init.as<LFW>();
    func();
  } else if (init.is<std::string>()) {
    this->logger->debug("{}/{}", this->file, init.as<std::string>());
    std::filesystem::path p(this->file);
    p /= init.as<std::string>();
    if (std::filesystem::exists(p)) {
      this->logger->info("Executing initialization script: {}", p.string());
      lua.load("initialize_fn", p.string());
      auto init_fn = lua["initialize_fn"];
      if (init_fn.is<LFW>()) {
        auto func = init_fn.as<LFW>();
        func();
      } else {
        this->logger->warn(
            "Initialization script does not return a function: {}", p.string());
      }
    } else {
      this->logger->warn("Initialization script not found: {}", p.string());
    }
  } else {
    this->logger->warn("Lua tool {} does not have an 'initialize' function",
                       name);
  }
}

void luaTool::invoke(const std::string &prefix) {
  this->prefix = prefix;
  this->logger->info("Invoking Lua tool: {} v{}...", name, version);
  auto vars = this->lua["vars"];
  if (vars.is<std::monostate>()) {
    this->logger->info("Lua tool {} has no 'vars' table", name);
    return;
  }
  if (vars.is<LTW>()) {
    auto &cpVars = cmd::CommandProcessor::instance().vars();
    auto var_table = vars.as<LTW>();
    for (const auto &[key, value] : var_table.iterate()) {
      if (value.is<std::string>()) {
        this->logger->info("Lua variable: '{}' = '{}'", key,
                           value.as<std::string>());
        cpVars.set(prefix + "." + key, cmd::VarValue(value.as<std::string>()));
      } else if (value.is<lua_Number>()) {
        this->logger->info("Lua variable: '{}' = {}", key,
                           value.as<lua_Number>());
        cpVars.set(prefix + "." + key, cmd::VarValue(value.as<lua_Number>()));
      } else if (value.is<bool>()) {
        this->logger->info("Lua variable: '{}' = {}", key, value.as<bool>());
        cpVars.set(prefix + "." + key, cmd::VarValue(value.as<bool>()));
      } else {
        const char *type_name = abi::__cxa_demangle(typeid(value).name(),
                                                    nullptr, nullptr, nullptr);
        this->logger->info("Lua variable: '{}' = '{}'", key, type_name);
        free((void *)type_name);
      }
    }
  }
}

void luaTool::shutdown() {
  this->logger->info("Shutting down Lua tool: {} v{}...", name, version);
  auto shutdown = this->lua["shutdown"];
  if (shutdown.is<LFW>()) {
    auto func = shutdown.as<LFW>();
    func();
  } else if (shutdown.is<std::string>()) {
    this->logger->debug("{}/{}", this->file, shutdown.as<std::string>());
    std::filesystem::path p(this->file);
    p /= shutdown.as<std::string>();
    if (std::filesystem::exists(p)) {
      this->logger->info("Executing shutdown script: {}", p.string());
      lua.load("shutdown_fn", p.string());
      auto shutdown_fn = lua["shutdown_fn"];
      if (shutdown_fn.is<LFW>()) {
        auto func = shutdown_fn.as<LFW>();
        func();
      } else {
        this->logger->warn("Shutdown script does not return a function: {}",
                           p.string());
      }
    } else {
      this->logger->warn("Shutdown script not found: {}", p.string());
    }
  } else {
    this->logger->warn("Lua tool {} does not have a 'shutdown' function", name);
  }
}

void luaTool::suppress() {
  this->logger->info("Suppressing Lua tool: {} v{}...", name, version);
  auto vars = this->lua["vars"];
  if (vars.is<std::monostate>()) {
    this->logger->info("Lua tool {} has no 'vars' table", name);
    return;
  }
  if (vars.is<LTW>()) {
    auto &cpVars = cmd::CommandProcessor::instance().vars();
    auto var_table = vars.as<LTW>();
    for (const auto &[key, value] : var_table.iterate()) {
      if (value.is<std::string>() || value.is<lua_Number>() ||
          value.is<bool>()) {
        this->logger->info("Unsetting Lua variable: '{}'", key);
        cpVars.unset(prefix + "." + key);
      } else {
        const char *type_name = abi::__cxa_demangle(typeid(value).name(),
                                                    nullptr, nullptr, nullptr);
        this->logger->info("Lua variable: '{}' of type '{}' cannot be unset",
                           key, type_name);
        free((void *)type_name);
      }
    }
  }
}

void luaTool::execute() {
  this->logger->info("Executing Lua tool: {} v{}...", name, version);
  auto execute = this->lua["execute"];
  if (execute.is<LFW>()) {
    auto func = execute.as<LFW>();
    func("Hello from C++!");
  } else if (execute.is<std::string>()) {
    this->logger->debug("{}/{}", this->file, execute.as<std::string>());
    std::filesystem::path p(this->file);
    p /= execute.as<std::string>();
    if (std::filesystem::exists(p)) {
      this->logger->info("Executing main script: {}", p.string());
      lua.load("execute_fn", p.string());
      auto execute_fn = lua["execute_fn"];
      if (execute_fn.is<LFW>()) {
        auto func = execute_fn.as<LFW>();
        func();
      } else {
        this->logger->warn("Main script does not return a function: {}",
                           p.string());
      }
    } else {
      this->logger->warn("Main script not found: {}", p.string());
    }
  } else {
    this->logger->warn("Lua tool {} does not have a valid 'execute' function",
                       name);
  }
}
