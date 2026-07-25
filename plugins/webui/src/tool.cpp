#include <httplib.h>
#include <sstream>
#include <tool.hpp>

void webui::initialize() {
  if (plugin.enableTLS->Get()) {
    logger->info("Starting web UI with TLS support on port {}", ACC_ARG(port));
    server = std::make_unique<httplib::SSLServer>(&ACC_ARG(certFile)->c_str(), &ACC_ARG(keyFile)->c_str());
  } else {
    logger->info("Starting web UI on port {}", ACC_ARG(port));
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

  // server->set_mount_point("/static", "./");

  server->Get("/", [&](const httplib::Request &req, httplib::Response &res) {
    logger->info("Received request for root path from {}", req.remote_addr);
    res.set_content("Welcome to the Web UI Renderer!", "text/plain");
  });

  server->Get("/status", [&](const httplib::Request &req, httplib::Response &res) {
    logger->info("Received status request from {}", req.remote_addr);
    res.set_content("Web UI is running", "text/plain");
  });

  server->Get("/info", [&](const httplib::Request &req, httplib::Response &res) {
    logger->info("Received info request from {}", req.remote_addr);
    std::string info = "Web UI Renderer\n";
    info += "Version: 0.1.0\n";
    info += "Host: " + ACC_ARG(host) + "\n";
    info += "Port: " + std::to_string(ACC_ARG(port)) + "\n";
    info += std::string("TLS Enabled: ") + (plugin.enableTLS->Get() ? "Yes" : "No") + "\n";
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
}

void webui::shutdown() {
  logger->info("Shutting down web UI");
  if (server) {
    server->stop();
    server.reset();
  }
}

void webui::runLoop() {
  logger->info("Web UI is listening on {}:{}", ACC_ARG(host), ACC_ARG(port));
  std::cout << "Web UI is running at http" << (plugin.enableTLS->Get() ? "s" : "") << "://" << ACC_ARG(host) << ":"
            << ACC_ARG(port) << std::endl;
  server->listen(ACC_ARG(host), ACC_ARG(port));
}
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
