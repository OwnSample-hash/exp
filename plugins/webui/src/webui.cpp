#include <args.hxx>
#include <tool.hpp>
#include <webui.hpp>

void PL_webui::initialize(initArgs &args) {
  // Register the display module
  args.modules->emplace_back("webui mod", explo::ModuleType::RENDERER, std::make_unique<webui>(args.logger));

  if (!args.parser) {
    args.logger->error("No argument parser provided to plugin: {}", getName());
    return;
  }

  this->group = args.parser;

  host.emplace(*(args.parser), "host", "Host to bind the web UI to (default localhost)", args::Matcher{'H', "host"},
               std::string("localhost"));

  port.emplace(*(args.parser), "port", "Port to bind the web UI to (default 8080)", args::Matcher{'P', "port"}, 8080);

  enableTLS.emplace(*(args.parser), "enable-tls", "Enable TLS for the web UI (default false)",
                    args::Matcher{'T', "enable-tls"}, false);
}

static PluginRegistry::Add<PL_webui> webuiRegister("webui");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
