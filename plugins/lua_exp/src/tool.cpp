#include <cmd.hpp>
#include <filesystem>
#include <tool.hpp>

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
  auto vars = this->lua["vars"];
  if (vars.is<LTW>()) {
    auto var_table = vars.as<LTW>();
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
        func("Hello from C++!");
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
