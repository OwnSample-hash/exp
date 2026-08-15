#include <b64.hpp>
#include <chrono>
#include <cmd.hpp>
#include <httplib.h>
#include <interfaces/tool.hpp>
#include <interfaces/tool_provider.hpp>
#include <nlohmann/json.hpp>
#include <queue>
#include <spdlog/fmt/bundled/format.h>
#include <tool.hpp>

using json = nlohmann::json;
using namespace explo;

extern std::map<std::string, std::shared_ptr<explo::ITool>> tools;
extern std::unordered_map<std::string, initArgs> pluginInitArgs;
extern thread_local std::shared_ptr<ITool> currentTool;
extern const std::list<std::unique_ptr<IPlugin>> &get_loaded_plugins();
extern void shutdown [[noreturn]] (int code = 0);

void webui::ws_loop(const httplib::Request &req, httplib::ws::WebSocket &ws) {
  logger->info("WebSocket connection established from {}", req.remote_addr);
  activeWebSocketConnections++;
  cmd::CommandProcessor &cp = cmd::CommandProcessor::instance();
  std::queue<json> eventQueue;
  std::vector<int> fds;

  cp.onHelp([&](const std::vector<std::string> &options) {
    json response;
    response["type"] = "help";
    response["options"] = options;
    eventQueue.push(response);
  });
  cp.onAutocomplete([&](const std::string &completed, bool unique) {
    json response;
    response["type"] = "autocomplete";
    response["completed"] = completed;
    response["unique"] = unique;
    eventQueue.push(response);
  });
  cp.onExecute([&](const cmd::ExecutionResult &r) {
    json response;
    response["type"] = "response";
    response["success"] = r.success;
    response["message"] = r.message;
    response["exitCode"] = r.exitCode;
    response["convert"] = {"message"};
    eventQueue.push(response);
  });
  cp.onError([&](const std::string &msg) {
    json response;
    response["type"] = "error";
    response["message"] = msg;
    eventQueue.push(response);
  });

  auto &vars = cp.vars();
  vars.set("version", cmd::VarValue(std::string("1.0.0")));
  vars.set("current_tool", cmd::VarValue(std::string("no tool")));
  vars.set("prompt", cmd::VarValue(std::string("${current_tool} \33[33m>\33[0m ")));

  logger->info("Initializing tools...");
  for (const auto &[plugin, args] : pluginInitArgs) {
    for (const auto &mod : *args.modules) {
      if (mod.type == explo::ModuleType::TOOL) {
        spdlog::debug("Tool: {} version: {}", mod.instance->getName(), mod.instance->getVersion());
        mod.instance->initialize();
      }
      if (mod.type == explo::ModuleType::TOOLPROVIDER) {
        auto *provider = dynamic_cast<explo::IToolProvider *>(mod.instance.get());
        logger->debug("Tool Provider: {} version: {}", provider->getName(), provider->getVersion());
        provider->initialize();
        for (const auto &[name, tool] : provider->getTools()) {
          logger->debug("  - Tool: {} version: {}", tool->getName(), tool->getVersion());
          tool->initialize();
        }
      }
    }
  }

#include <commands.hpp>

  logger->trace("Have {} global commands", cp.getContext()->commands().size());

  logger->info("Command processor event handlers registered for WebSocket connection from {}", req.remote_addr);
  while (ws.is_open()) {
    std::string data;
    json event;
    ws.read(data);
    try {
      event = json::parse(data);
    } catch (const json::parse_error &e) {
      logger->error("JError: {}", e.what());
    } catch (const std::exception &e) {
      logger->error("Error: {}", e.what());
    }

    logger->debug("Received event: {}", event.dump());
    try {
      std::string type = event.at("type").get<std::string>();
      logger->trace("Processing event of type: {}", type);
      if (type == "input") {
        using Ir = explo::cmd::InputResult;
        std::string input = event.at("input").get<std::string>();
        auto start = std::chrono::high_resolution_clock::now();
        for (char ch : input) {
          switch (cp.feed(ch)) {
          case Ir::Consumed:
          case Ir::Escape:
          case Ir::Help:
          case Ir::Autocompleted:
          case Ir::Executed:
          case Ir::Cleared:
          case Ir::Error:
            break;
          }
        }
        auto duration = std::chrono::high_resolution_clock::now() - start;
        json response;
        response["type"] = "inputProcessed";
        response["duration_ns"] = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        eventQueue.push(response);
      } else if (type == "action") {
        std::string action = event.at("action").get<std::string>();
      } else if (type == "upload") {
        std::string varname = event.at("varname").get<std::string>();
        std::string content = base64_decode(event.at("content").get<std::string>());
        std::FILE *tmpFile = std::tmpfile();
        if (tmpFile) {
          std::fwrite(content.data(), 1, content.size(), tmpFile);
          std::fflush(tmpFile);
          std::rewind(tmpFile);
          fds.push_back(fileno(tmpFile));
          logger->info("Uploaded file and bind it to {} ({} bytes)", varname, content.size());
          std::string tmpFilePath =
              std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(fileno(tmpFile))).string();
          vars.set(varname, cmd::VarValue(tmpFilePath));
          logger->debug("Temporary file path: {}", tmpFilePath);
          json response;
          response["type"] = "upload";
          response["size"] = content.size();
          response["status"] = "success";
          eventQueue.push(response);
        } else {
          logger->error("Failed to create temporary file for upload");
          json response;
          response["type"] = "upload";
          response["status"] = "failure";
          response["message"] = "Failed to create temporary file for upload";
          eventQueue.push(response);
        }
      } else if (type == "getPrompt") {
        json response;
        response["type"] = "prompt";
        auto vars = cp.vars();
        auto rawPrompt = vars.get("prompt").has_value() ? vars.get("prompt")->toString() : "> ";
        response["prompt"] = vars.expand(rawPrompt);
        response["convert"] = {"prompt"};
        eventQueue.push(response);
      } else if (type == "getCmds") {
        json response;
        response["type"] = "cmds";
        response["global"] = json::object();
        response["current"] = json::object();
        for (const auto &cmd : cp.getContext()->commands()) {
          response["global"][cmd.name] = cmd.description;
        }
        if (cp.getContext(false).get() != nullptr) {
          for (const auto &cmd : cp.getContext(false)->commands()) {
            response["current"][cmd.name] = cmd.description;
          }
        }
        eventQueue.push(response);
      } else if (type == "getAC") {
        json response;
        response["type"] = "ac";
        response["ac"] = activeWebSocketConnections.load();
        eventQueue.push(response);
      } else {
        logger->warn("Unknown event type: {}", type);
        eventQueue.push({{"type", "error"}, {"message", "Unknown event type: " + type}});
      }
    } catch (const json::exception &e) {
      logger->error("JSON error: {}", e.what());
    } catch (const std::exception &e) {
      logger->error("Error: {}", e.what());
    }

    logger->trace("Event queue size: {}", eventQueue.size());
    for (size_t i = 0; i < eventQueue.size(); ++i) {
      json response = eventQueue.front();
      eventQueue.pop();
      if (response.contains("convert") && response["convert"].get<json::array_t>().size() > 0) {
        convert(response);
      }
      ws.send(response.dump());
    }
    logger->trace("Event queue processed and sent to client");
  }
  logger->info("WebSocket connection closed from {}", req.remote_addr);
  cp.onHelp(nullptr);
  cp.onAutocomplete(nullptr);
  cp.onExecute(nullptr);
  cp.onError(nullptr);
  activeWebSocketConnections--;
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
