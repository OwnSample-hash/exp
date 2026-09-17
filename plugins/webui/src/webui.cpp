#include "plugin_interface.hpp"
#include <arc.hpp>
#include <httplib.h>
#include <limits.h>
#include <sstream>
#include <sys/mount.h>
#include <webui.hpp>
#include <webui_config.hpp>

constexpr unsigned char webui_arc[] = {
#embed ARCHIVE
};

void webui::initialize(initArgs &args) {
  if (!basicInitDone) {
    this->logger = args.logger;
    this->group = args.parser;
    basicInitDone = true;
    logger->info("Initializing web UI module");
    return;
  }
  assert(this->logger != nullptr);

  host.emplace(*(args.parser), "host", "Host to bind the web UI to (default localhost)", args::Matcher{'H', "host"},
               std::string("localhost"));

  port.emplace(*(args.parser), "port", "Port to bind the web UI to (default 8080)", args::Matcher{'P', "port"}, 8080);

  enableTLS.emplace(*(args.parser), "enable-tls", "Enable TLS for the web UI (default false)",
                    args::Matcher{'T', "enable-tls"}, false);

  certFile.emplace(*(args.parser), "cert-file", "Path to TLS certificate file (required if TLS is enabled)",
                   args::Matcher{'C', "cert-file"}, std::string(""));

  keyFile.emplace(*(args.parser), "key-file", "Path to TLS key file (required if TLS is enabled)",
                  args::Matcher{'K', "key-file"}, std::string(""));

  if (arc.open(webui_arc, sizeof(webui_arc))) {
    logger->info("Web UI archive loaded successfully");
  } else {
    logger->error("Failed to load web UI archive");
  }

  if (!fs::exists(temp_dir)) {
    fs::create_directories(temp_dir);
  }

  char buffer[32];
  std::ifstream comm("/proc/self/comm", std::ios::in | std::ios::binary);
  if (!comm.is_open()) {
    logger->error("Failed to open /proc/self/comm");
    return;
  }
  comm.read(buffer, sizeof(buffer) - 1);
  buffer[comm.gcount()] = '\0'; // Null-terminate the string
  comm.close();
  std::string process_name(buffer);
  process_name.erase(std::remove(process_name.begin(), process_name.end(), '\n'), process_name.end());

  fuse_thread = std::thread([&]() {
    char **argv;
    try {
      argv = (char **)calloc(sizeof(char *), 2);
      argv[0] = strdup(process_name.c_str());
      argv[1] = nullptr;
      umount(temp_dir.string().c_str());
      if (!arc.mount_archive(temp_dir.string(), 1, argv, false, false)) {
        logger->error("Failed to mount web UI archive");
      } else {
        logger->info("Web UI archive mounted successfully at {}", temp_dir.string());
      }
    } catch (const std::exception &e) {
      logger->error("Exception in fuse_thread: {}", e.what());
    } catch (...) {
      logger->error("Unknown exception in fuse_thread");
    }
    umount(temp_dir.string().c_str());
  });

  // fuse_thread.detach();

  nouse_thread = std::thread([&]() {
    int count = 0;
    std::cout << "The web UI will automatically shut down after " << CONFIG_WEBUI_TIMEOUT_SEC
              << " seconds of inactivity (no WebSocket connections)." << std::endl;
    while (this->running) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (activeWebSocketConnections == 0) {
        count++;
        if (count >= CONFIG_WEBUI_TIMEOUT_SEC) {
          server->stop();
          break;
        }
      } else {
        count = 0;
      }
    }
    std::cout << "Web UI is shutting down due to inactivity." << std::endl;
    logger->debug("Exiting nouse_thread");
  });

  nouse_thread.detach();

  if (enableTLS && *enableTLS) {
    logger->info("Starting web UI with TLS support on port {}", port->Get());
    server = std::make_unique<httplib::SSLServer>(certFile->Get().c_str(), keyFile->Get().c_str());
  } else {
    logger->info("Starting web UI on port {}", port->Get());
    server = std::make_unique<httplib::Server>();
  }

  server->set_logger([&](const httplib::Request &req, const httplib::Response &res) {
    auto now = std::chrono::system_clock::now();
    std::string tmp =
        fmt::format("{} - - [{}] \"{} {} {}\" {} {}", req.remote_addr, std::format("{:%d/%b/%Y:%H:%M:%S %z}", now),
                    req.method, req.path, req.version, res.status, res.body.size());
    std::cout << tmp << std::endl;
    this->logger->info(tmp);
  });

  server->set_error_logger([&](const httplib::Error &err, const httplib::Request *req) {
    this->logger->error("HTTP Server error: {}", httplib::to_string(err));
    if (req) {
      std::string tmp;
      auto now = std::chrono::system_clock::now();
      tmp = fmt::format("{} - - [{}] \"{} {} {}\" {}", req->remote_addr, std::format("{:%d/%b/%Y:%H:%M:%S %z}", now),
                        req->method, req->path, req->version, req->body.size());
      std::cout << tmp << std::endl;
      this->logger->error("{}", tmp);
    }
  });

  server->set_tcp_nodelay(true);

  server->set_mount_point("/", temp_dir);

  server->Get("/info", [&](const httplib::Request &req, httplib::Response &res) {
    logger->info("Received info request from {}", req.remote_addr);
    std::string info = "Web UI Renderer\n";
    info += "Version: 0.1.0\n";
    info += "Host: " + host->Get() + "\n";
    info += "Port: " + std::to_string(port->Get()) + "\n";
    info += std::string("TLS Enabled: ") + (enableTLS->Get() ? "Yes" : "No") + "\n";
    res.set_content(info, "text/plain");
  });

  server->Get("/convert", [&](const httplib::Request &req, httplib::Response &res) {
    json response;
    response["test"] = "\x1b[38;5;196mHello \x1b[48;5;226mWorld\x1b[0m!";
    response["test2"] = "\x1b[38;2;255;0;0mRed \x1b[48;2;0;255;0mGreen\x1b[0m!";
    response["test3"] = "\x1b[9mStrikethrough \x1b[1;4mUnderline\x1b[24m Bold but not underline\x1b[0m!";
    response["convert"] = json::array({"test", "test2", "test3"});
    try {
      convert(response);
    } catch (const std::exception &e) {
      logger->error("Error during conversion: {}", e.what());
      response["error"] = e.what();
      throw;
    }
    res.set_content(response.dump(), "application/json");
  });

  server->Get("/256.css", [&](const httplib::Request &req, httplib::Response &res) {
    std::stringstream ss;
    ss << ".color-black{color:#000;}";
    ss << ".color-red{color:#c00;}";
    ss << ".color-green{color:#0c0;}";
    ss << ".color-yellow{color:#cc0;}";
    ss << ".color-blue{color:#00c;}";
    ss << ".color-magenta{color:#c0c;}";
    ss << ".color-cyan{color:#0cc;}";
    ss << ".color-white{color:#ccc;}";
    ss << ".color-bright-black{color:#222;}";
    ss << ".color-bright-red{color:#f00;}";
    ss << ".color-bright-green{color:#0f0;}";
    ss << ".color-bright-yellow{color:#ff0;}";
    ss << ".color-bright-blue{color:#00f;}";
    ss << ".color-bright-magenta{color:#f0f;}";
    ss << ".color-bright-cyan{color:#0ff;}";
    ss << ".color-bright-white{color:#fff;}";

    ss << ".color-256-0{color:#000;}";
    ss << ".color-256-1{color:#c00;}";
    ss << ".color-256-2{color:#0c0;}";
    ss << ".color-256-3{color:#cc0;}";
    ss << ".color-256-4{color:#00c;}";
    ss << ".color-256-5{color:#c0c;}";
    ss << ".color-256-6{color:#0cc;}";
    ss << ".color-256-7{color:#ccc;}";
    ss << ".color-256-8{color:#222;}";
    ss << ".color-256-9{color:#f00;}";
    ss << ".color-256-10{color:#0f0;}";
    ss << ".color-256-11{color:#ff0;}";
    ss << ".color-256-12{color:#00f;}";
    ss << ".color-256-13{color:#f0f;}";
    ss << ".color-256-14{color:#0ff;}";
    ss << ".color-256-15{color:#fff;}";
    for (int i = 16; i <= 231; i++) {
      int r = ((i - 16) / 36) % 6;
      int g = ((i - 16) / 6) % 6;
      int b = (i - 16) % 6;
      ss << ".color-256-" << i << "{color:rgb(" << (r ? r * 40 + 55 : 0) << "," << (g ? g * 40 + 55 : 0) << ","
         << (b ? b * 40 + 55 : 0) << ");}";
    }
    for (int i = 232; i <= 255; i++) {
      int gray = (i - 232) * 10 + 8;
      ss << ".color-256-" << i << "{color:rgb(" << gray << "," << gray << "," << gray << ");}";
    }
    // background
    ss << ".bg-black{background-color:#000;}";
    ss << ".bg-red{background-color:#c00;}";
    ss << ".bg-green{background-color:#0c0;}";
    ss << ".bg-yellow{background-color:#cc0;}";
    ss << ".bg-blue{background-color:#00c;}";
    ss << ".bg-magenta{background-color:#c0c;}";
    ss << ".bg-cyan{background-color:#0cc;}";
    ss << ".bg-white{background-color:#ccc;}";
    ss << ".bg-bright-black{background-color:#222;}";
    ss << ".bg-bright-red{background-color:#f00;}";
    ss << ".bg-bright-green{background-color:#0f0;}";
    ss << ".bg-bright-yellow{background-color:#ff0;}";
    ss << ".bg-bright-blue{background-color:#00f;}";
    ss << ".bg-bright-magenta{background-color:#f0f;}";
    ss << ".bg-bright-cyan{background-color:#0ff;}";
    ss << ".bg-bright-white{background-color:#fff;}";

    ss << ".bg-256-0{background-color:#000;}";
    ss << ".bg-256-1{background-color:#c00;}";
    ss << ".bg-256-2{background-color:#0c0;}";
    ss << ".bg-256-3{background-color:#cc0;}";
    ss << ".bg-256-4{background-color:#00c;}";
    ss << ".bg-256-5{background-color:#c0c;}";
    ss << ".bg-256-6{background-color:#0cc;}";
    ss << ".bg-256-7{background-color:#ccc;}";
    ss << ".bg-256-8{background-color:#222;}";
    ss << ".bg-256-9{background-color:#f00;}";
    ss << ".bg-256-10{background-color:#0f0;}";
    ss << ".bg-256-11{background-color:#ff0;}";
    ss << ".bg-256-12{background-color:#00f;}";
    ss << ".bg-256-13{background-color:#f0f;}";
    ss << ".bg-256-14{background-color:#0ff;}";
    ss << ".bg-256-15{background-color:#fff;}";
    for (int i = 16; i <= 231; i++) {
      int r = ((i - 16) / 36) % 6;
      int g = ((i - 16) / 6) % 6;
      int b = (i - 16) % 6;
      ss << ".bg-256-" << i << "{background-color:rgb(" << (r ? r * 40 + 55 : 0) << "," << (g ? g * 40 + 55 : 0) << ","
         << (b ? b * 40 + 55 : 0) << ");}";
    }
    for (int i = 232; i <= 255; i++) {
      int gray = (i - 232) * 10 + 8;
      ss << ".bg-256-" << i << "{background-color:rgb(" << gray << "," << gray << "," << gray << ");}";
    }
    res.set_content(ss.str(), "text/css");
  });

  server->WebSocket("/ws", [&](const httplib::Request &req, httplib::ws::WebSocket &ws) { this->ws_loop(req, ws); });
  logger->info("Web UI initialized successfully");
}

void webui::shutdown() {
  logger->info("Shutting down web UI");
  if (server) {
    server->stop();
    server.reset();
  }
  logger->info("Web UI server stopped");
  logger->trace("Trying to join thread fuse_thread");
  logger->trace("fuse_thread joinable: {}", fuse_thread.joinable());
  if (fuse_thread.joinable()) {
    umount(temp_dir.string().c_str());
    arc.stop_mount();
    fuse_thread.join();
  }
  logger->trace("Trying to join thread nouse_thread");
  if (nouse_thread.joinable()) {
    running = false;
    nouse_thread.join();
  }
  arc.close();
}

void webui::runLoop() {
  logger->info("Web UI is listening on {}:{}", host->Get(), port->Get());
  std::cout << "Web UI is running at http" << (enableTLS->Get() ? "s" : "") << "://" << host->Get() << ":"
            << port->Get() << std::endl;
  server->listen(host->Get(), port->Get());
}

static RendererRegistry::Add<webui> webuiRegister("webui");
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
