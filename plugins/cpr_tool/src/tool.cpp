#include "cmd/command_def.hpp"
#include "cmd/command_processor.hpp"
#include "cmd/variable.hpp"
#include <cmd.hpp>
#include <cpr/cpr.h>
#include <cpr/curl_container.h>
#include <nlohmann/json.hpp>
#include <tool.hpp>

using json = nlohmann::json;

using namespace explo;

void CPR::initialize() {
  logger->info("Initializing CPR tool");
  tags.emplace_back("http");
  tags.emplace_back("network");
  {
    auto ctx = std::make_shared<cmd::Context>(this->getName());
    {
      cmd::CommandDef c;
      c.name = "run";
      c.description = "Run the cpr main function";
      c.variadic = false;
      c.addDynamic("<json-encoded-data>", R"(^.+$)", "JSON-encoded data to pass to the CPR tool");
      c.handler = [&](const cmd::ExecutionContext &ec) -> std::string {
        if (ec.args.size() < 2) {
          return "\033[1;31mError: Missing required argument "
                 "<yaml-encoded-data>\033[0m";
        }
        this->payload = ec.args[1];
        this->logger->trace("Received command with payload: {}", this->payload);
        this->execute();
        if (this->status != 0) {
          return "\033[1;31mCPR tool execution failed with status: " + std::to_string(this->status) + "\033[0m";
        }
        return "CPR tool execution completed";
      };
      ctx->registerCommand(c);
    }
    cmd::CommandProcessor::instance().registerContext(ctx);
  }
}

void CPR::shutdown() { logger->info("Shutting down CPR tool"); }

void CPR::invoke(std::string_view prefix, bool soft) {
  logger->info("Invoking CPR tool with prefix: {}", prefix);
  this->prefix = prefix;
  auto &cpVars = cmd::CommandProcessor::instance().vars();
  if (soft && cpVars.get(this->prefix + ".args").has_value()) {
    this->logger->info("CPR tool args already exist, skipping due to soft invoke");
    return;
  }
  cpVars.set(this->prefix + ".args", cmd::VarValue{this->payload});
}

void CPR::suppress() {
  logger->info("Suppressing CPR tool");
  auto &cpVars = cmd::CommandProcessor::instance().vars();
  cpVars.unset(prefix + ".args");
}

void CPR::execute() {
  logger->info("Executing CPR tool");
  if (this->payload.empty()) {
    auto &cp = cmd::CommandProcessor::instance();
    auto payload_ = cp.vars().get("cpr.args");
    if (!payload_) {
      logger->error("No payload provided for CPR tool execution");
      this->status = -1;
      return;
    }
    this->payload = payload_->toString();
  }
  logger->trace("Executing CPR tool with payload: {}", this->payload);
  json data = json::parse(this->payload);
  std::string url = data["url"];
  std::string method = data["method"];
  cpr::Parameters params;
  cpr::Header headers;
  if (method != "GET" && data.contains("params")) {
    for (auto &[key, value] : data["params"].items()) {
      params.Add({key, value.get<std::string>()});
      logger->trace("Added parameter: {}={}", key, value.get<std::string>());
    }
  }
  if (data.contains("headers")) {
    for (auto &[key, value] : data["headers"].items()) {
      headers.emplace(key, value.get<std::string>());
      logger->trace("Added header: {}={}", key, value.get<std::string>());
    }
  }
  logger->info("Making {} request to URL: {}", method, url);
  cpr::Response r;
  if (method == "GET") {
    r = cpr::Get(cpr::Url{url});
  } else if (method == "POST") {
    r = cpr::Post(cpr::Url{url}, params, headers);
  } else if (method == "PUT") {
    r = cpr::Put(cpr::Url{url}, params, headers);
  } else if (method == "DELETE") {
    r = cpr::Delete(cpr::Url{url}, params, headers);
  } else if (method == "PATCH") {
    r = cpr::Patch(cpr::Url{url}, params, headers);
  } else if (method == "HEAD") {
    r = cpr::Head(cpr::Url{url}, params, headers);
  } else {
    logger->error("Unsupported HTTP method: {}", method);
    this->status = -1;
    return;
  }
  logger->info("Received response with status code: {}", r.status_code);
  logger->debug("Response body: {}", r.text);
  this->status = r.status_code;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
