#include "utils.hpp"
#include <cmd.hpp>
#include <cxxabi.h>
#include <filesystem>
#include <tool.hpp>
#include <variant>

using namespace explo;

void luaTool::initialize(initArgs &args) {
  this->logger = args.logger;
  this->logger->info("Initializing Lua tool: {} v{}...", name, version);
  {
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run " + name + "'s tool main function";
      c.variadic = false;
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        this->execute();
        if (this->lastStatus != 0) {
          return "\033[1;31mTool execution failed with status: " + std::to_string(this->lastStatus) + "\033[0m";
        }
        return "Tool execution completed";
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
        this->logger->warn("Initialization script does not return a function: {}", p.string());
      }
    } else {
      this->logger->warn("Initialization script not found: {}", p.string());
    }
  } else {
    this->logger->warn("Lua tool {} does not have an 'initialize' function", name);
  }

  auto makeCommand = this->lua["makeCommand"];
  if (makeCommand.is<bool>() && makeCommand.as<bool>()) {
    this->cmd.emplace(*args.parser, this->name, "Run " + this->name + "'s tool main function", [&](args::Subparser &s) {
      auto vars = this->lua["vars"];
      auto decs_raw = this->lua["desc"];
      if (!vars.is<LTW>() && !decs_raw.is<LTW>()) {
        this->logger->warn("Lua tool {} has no 'vars' table", name);
        return;
      }
      auto decs = decs_raw.as<LTW>();
      for (const auto &[key, value] : vars.as<LTW>().iterate()) {
        this->logger->trace("Lua variable: '{}'", key);
        if (value.is<std::string>()) {
          this->flags.emplace_back(std::make_tuple(
              new args::ValueFlag<std::string>(s, key, decs[key].as<std::string>(), {key}, value.as<std::string>()),
              luaFlagtype::STRING));
        } else if (value.is<lua_Number>()) {
          this->flags.emplace_back(std::make_tuple(
              new args::ValueFlag<lua_Number>(s, key, decs[key].as<std::string>(), {key}, value.as<lua_Number>()),
              luaFlagtype::NUMBER));
        } else if (value.is<bool>()) {
          this->flags.emplace_back(
              std::make_tuple(new args::ValueFlag<bool>(s, key, decs[key].as<std::string>(), {key}, value.as<bool>()),
                              luaFlagtype::BOOLEAN));
        } else {
          const char *type_name = abi::__cxa_demangle(typeid(value).name(), nullptr, nullptr, nullptr);
          this->logger->warn("Lua variable: '{}' of type '{}' cannot be set via command line", key, type_name);
          free((void *)type_name);
        }
      }
      s.Parse();
      this->varTypes.reserve(this->flags.size());
      for (auto &[flag, type] : this->flags) {
        switch (type) {
        case luaFlagtype::STRING: {
          auto val = dynamic_cast<args::ValueFlag<std::string> *>(flag);
          if (val)
            this->varTypes.emplace(val->Name(), val->Get());
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<std::string>");
          delete flag;
          break;
        }
        case luaFlagtype::NUMBER: {
          auto val = dynamic_cast<args::ValueFlag<lua_Number> *>(flag);
          if (val)
            this->varTypes.emplace(val->Name(), val->Get());
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<lua_Number>");
          delete flag;
          break;
        }
        case luaFlagtype::BOOLEAN: {
          auto val = dynamic_cast<args::ValueFlag<bool> *>(flag);
          if (val)
            this->varTypes.emplace(val->Name(), val->Get());
          else
            throw std::runtime_error("Failed to cast flag to ValueFlag<bool>");
          delete flag;
          break;
        }
        default:
          throw std::runtime_error("Unknown luaFlagtype");
        }
      }
      this->flags.clear();
    });
  }
}

void luaTool::invoke(std::string_view prefix, bool soft) {
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
      if (soft && cpVars.get(this->prefix + "." + key).has_value()) {
        this->logger->info("Lua variable: '{}' already exists, skipping due to soft invoke", key);
        continue;
      }
      if (value.is<std::string>()) {
        this->logger->info("Lua variable: '{}' = '{}'", key, value.as<std::string>());
        cpVars.set(this->prefix + "." + key, cmd::VarValue(value.as<std::string>()));
      } else if (value.is<lua_Number>()) {
        this->logger->info("Lua variable: '{}' = {}", key, value.as<lua_Number>());
        cpVars.set(this->prefix + "." + key, cmd::VarValue(value.as<lua_Number>()));
      } else if (value.is<bool>()) {
        this->logger->info("Lua variable: '{}' = {}", key, value.as<bool>());
        cpVars.set(this->prefix + "." + key, cmd::VarValue(value.as<bool>()));
      } else {
        const char *type_name = abi::__cxa_demangle(typeid(value).name(), nullptr, nullptr, nullptr);
        this->logger->info("Unused lua variable: '{}' = '{}'", key, type_name);
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
        this->logger->warn("Shutdown script does not return a function: {}", p.string());
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
      if (value.is<std::string>() || value.is<lua_Number>() || value.is<bool>()) {
        this->logger->info("Unsetting Lua variable: '{}'", key);
        cpVars.unset(prefix + "." + key);
      } else {
        const char *type_name = abi::__cxa_demangle(typeid(value).name(), nullptr, nullptr, nullptr);
        this->logger->info("Lua variable: '{}' of type '{}' cannot be unset", key, type_name);
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
    auto status = func()[0];
    if (status.is<std::string>()) {
      this->logger->info("Main script returned: {}", status.as<std::string>());
    } else if (status.is<lua_Number>()) {
      this->logger->info("Main script returned: {}", status.as<lua_Number>());
      this->lastStatus = static_cast<int>(status.as<lua_Number>());
    } else if (status.is<bool>()) {
      this->logger->info("Main script returned: {}", status.as<bool>());
      this->lastStatus = status.as<bool>() ? 1 : 0;
    } else {
      const char *type_name = abi::__cxa_demangle(typeid(status).name(), nullptr, nullptr, nullptr);
      this->logger->info("Main script returned value of type '{}'", type_name);
      free((void *)type_name);
    }
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
        auto status = func()[0];
        if (status.is<std::string>()) {
          this->logger->info("Main script returned: {}", status.as<std::string>());
        } else if (status.is<lua_Number>()) {
          this->logger->info("Main script returned: {}", status.as<lua_Number>());
          this->lastStatus = static_cast<int>(status.as<lua_Number>());
        } else if (status.is<bool>()) {
          this->logger->info("Main script returned: {}", status.as<bool>());
          this->lastStatus = status.as<bool>() ? 1 : 0;
        } else {
          const char *type_name = abi::__cxa_demangle(typeid(status).name(), nullptr, nullptr, nullptr);
          this->logger->info("Main script returned value of type '{}'", type_name);
          free((void *)type_name);
        }
      } else {
        this->logger->warn("Main script does not return a function: {}", p.string());
      }
    } else {
      this->logger->warn("Main script not found: {}", p.string());
    }
  } else {
    this->logger->warn("Lua tool {} does not have a valid 'execute' function", name);
  }
}

template <class... Ts> struct overloads : Ts... {
  using Ts::operator()...;
};

bool luaTool::cmdCheck() {
  auto makeCommand = this->lua["makeCommand"];
  if (!makeCommand.is<bool>() || !makeCommand.as<bool>()) {
    return false;
  }
  if (cmd && !(*cmd)) {
    return false;
  }
  this->invoke(this->name, true);
  auto &vars = cmd::CommandProcessor::instance().vars();

  for (const auto &[name, var] : this->varTypes) {
    const auto visitor = overloads{
        [&](const std::string &var) { vars.set(this->prefix + "." + name, cmd::VarValue(var.data())); },
        [&](lua_Number var) { vars.set(this->prefix + "." + name, cmd::VarValue(var)); },
        [&](bool var) { vars.set(this->prefix + "." + name, cmd::VarValue(var)); },
        [&](auto var) {
          const char *type_name = abi::__cxa_demangle(typeid(var).name(), nullptr, nullptr, nullptr);
          this->logger->warn("Lua variable: '{}' of type '{}' cannot be set via command line", name, type_name);
          free((void *)type_name);
        }};
    std::visit(visitor, var.data);
  }
  this->execute();
  this->suppress();
  return true;
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
