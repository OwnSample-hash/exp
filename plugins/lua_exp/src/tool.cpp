#include <cmd.hpp>
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
}

void luaTool::shutdown() {
  this->logger->info("Shutting down Lua tool: {} v{}...", name, version);
  // Additional cleanup logic can be added here
}

void luaTool::execute() {
  this->logger->info("Executing Lua tool: {} v{}...", name, version);
  LFW execute = this->lua["execute"].as<LFW>();
  execute("hello from C++");
}
