#include <args.hxx>
#include <tool.hpp>
#include <webui.hpp>

void PL_webui::initialize(initArgs &args) {
  args.modules->emplace_back("webui", explo::ModuleType::RENDERER, std::make_unique<webui>(args.logger, *this));

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

  certFile.emplace(*(args.parser), "cert-file", "Path to TLS certificate file (required if TLS is enabled)",
                   args::Matcher{'C', "cert-file"}, std::string(""));

  keyFile.emplace(*(args.parser), "key-file", "Path to TLS key file (required if TLS is enabled)",
                  args::Matcher{'K', "key-file"}, std::string(""));
}

static PluginRegistry::Add<PL_webui> webuiRegister("webui");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
