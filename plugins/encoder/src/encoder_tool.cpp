#include <encoder_tool.hpp>

#include <algorithm>
#include <cmd.hpp>
#include <iostream>
#include <pipeline.hpp>
#include <process.hpp>

using namespace std::string_literals;

EncoderTool::EncoderTool(enc::EncoderDef definition, std::shared_ptr<spdlog::logger> logger,
                         std::filesystem::path workDir)
    : def(std::move(definition)), logger(std::move(logger)), workDir(std::move(workDir)) {
  tags = def.tags;
  tags.push_back("encoder");
  tags.push_back(enc::methodName(def.method));

  if (!def.uuid.empty()) {
    if (auto parsed = uuids::uuid::from_string(def.uuid))
      uuid = *parsed;
  }
  if (uuid.is_nil()) {
    // Derive a stable UUID (v5) from the encoder name so sessions persist.
    uuids::uuid_name_generator gen(uuids::uuid_namespace_url);
    uuid = gen("explo.encoder." + def.name);
  }
}

void EncoderTool::initialize(initArgs &args) {
  this->logger = args.logger;

  for (const auto &req : def.requirements) {
    if (enc::which(req).empty()) {
      missingRequirements.push_back(req);
      logger->warn("Encoder '{}' requirement not found on PATH: '{}'", def.name, req);
    }
  }

  // Register a per-tool `run` command, mirroring other tool plugins.
  auto ctx = std::make_shared<cmd::Context>(def.name);
  {
    cmd::CommandDef c;
    c.name = "run";
    c.description = "Run the '" + def.name + "' encoder";
    c.variadic = false;
    c.handler = [this](const cmd::ExecutionContext &) -> std::string {
      this->execute();
      return this->lastResult;
    };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "info";
    c.description = "Show details about the '" + def.name + "' encoder";
    c.variadic = false;
    c.handler = [this](const cmd::ExecutionContext &) -> std::string {
      std::string s = def.name + " v" + def.version + " [" + enc::methodName(def.method) + "]\n";
      s += "  " + def.description + "\n";
      s += "  inputs:\n";
      for (const auto &in : def.inputs) {
        s += "    - " + in.name + " (" + in.type + ")" + (in.required ? " [required]" : "");
        if (!in.def.empty())
          s += " default=" + in.def;
        s += "\n";
      }
      if (!missingRequirements.empty()) {
        s += "  missing requirements:";
        for (const auto &r : missingRequirements)
          s += " " + r;
        s += "\n";
      }
      return s;
    };
    ctx->registerCommand(c);
  }
  cmd::CommandProcessor::instance().registerContext(ctx);

  valid = true;
  logger->info("Initialized encoder tool '{}' ({} method, {} input(s))", def.name, enc::methodName(def.method),
               def.inputs.size());
}

void EncoderTool::invoke(std::string_view prefix, bool soft) {
  this->prefix = prefix;
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const auto &in : def.inputs) {
    const std::string key = this->prefix + "." + in.name;
    if (soft && vars.get(key).has_value())
      continue;
    vars.set(key, cmd::VarValue(in.def));
  }
  logger->info("Invoked encoder '{}' with prefix '{}'", def.name, this->prefix);
}

void EncoderTool::suppress() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const auto &in : def.inputs) {
    const std::string key = this->prefix + "." + in.name;
    if (vars.get(key).has_value())
      vars.unset(key);
  }
}

void EncoderTool::execute() {
  auto &vars = cmd::CommandProcessor::instance().vars();

  std::map<std::string, std::string> inputs;
  for (const auto &in : def.inputs) {
    const auto v = vars.get(this->prefix + "." + in.name);
    std::string value = v.has_value() ? v->toString() : in.def;
    if (in.required && value.empty()) {
      lastResult = "\033[1;31mencoder '" + def.name + "': required input '" + in.name + "' is not set\033[0m";
      logger->error("Encoder '{}' missing required input '{}'", def.name, in.name);
      return;
    }
    // Validate enum inputs.
    if (in.type == "enum" && !in.values.empty() &&
        std::find(in.values.begin(), in.values.end(), value) == in.values.end()) {
      lastResult =
          "\033[1;31mencoder '" + def.name + "': '" + value + "' is not a valid value for '" + in.name + "'\033[0m";
      return;
    }
    inputs[in.name] = value;
  }

  if (!missingRequirements.empty()) {
    std::string miss;
    for (const auto &r : missingRequirements)
      miss += " " + r;
    logger->warn("Encoder '{}' running with missing requirements:{}", def.name, miss);
  }

  logger->info("Running encoder '{}'", def.name);
  enc::EncodeResult res = enc::runEncoder(def, inputs, workDir);
  if (!res.ok) {
    lastResult = "\033[1;31mencoder '" + def.name + "' failed: " + res.message + "\033[0m";
    logger->error("Encoder '{}' failed: {}", def.name, res.message);
    return;
  }

  // The rendered output goes to stdout; the command returns a short summary.
  // std::cout << res.rendered;
  if (!res.rendered.empty() && res.rendered.back() != '\n')
    std::cout << '\n';

  std::string summary = "encoder '" + def.name + "': " + res.message;
  if (!res.outputFile.empty())
    summary += " -> wrote " + res.outputFile;
  lastResult = summary;
  logger->info("{}", summary);
}

void EncoderTool::shutdown() { logger->trace("Shutting down encoder tool '{}'", def.name); }

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
