#include <fileserver.hpp>

#include <webui_config.hpp>

#include <algorithm>
#include <cctype>
#include <cmd.hpp>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <plugin_interface.hpp>
#include <sstream>
#include <system_error>
#include <vector>

using namespace std::string_literals;

namespace {

/// Minimal extension -> MIME map. Anything unknown falls back to a generic
/// binary type, which is the safe default for a download server.
std::string mimeFor(const fs::path &p) {
  static const std::map<std::string, std::string> types = {
      {".html", "text/html"},        {".htm", "text/html"},         {".css", "text/css"},
      {".js", "text/javascript"},    {".json", "application/json"}, {".txt", "text/plain"},
      {".md", "text/plain"},         {".xml", "application/xml"},   {".csv", "text/csv"},
      {".png", "image/png"},         {".jpg", "image/jpeg"},        {".jpeg", "image/jpeg"},
      {".gif", "image/gif"},         {".svg", "image/svg+xml"},     {".ico", "image/x-icon"},
      {".pdf", "application/pdf"},   {".zip", "application/zip"},   {".gz", "application/gzip"},
      {".tar", "application/x-tar"}, {".mp4", "video/mp4"},         {".mp3", "audio/mpeg"},
      {".wav", "audio/wav"},
  };
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  auto it = types.find(ext);
  return it != types.end() ? it->second : "application/octet-stream";
}

/// Percent-encode a single path segment for use in a hyperlink, leaving the
/// RFC 3986 unreserved characters untouched.
std::string urlEncode(const std::string &s) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0xF]);
    }
  }
  return out;
}

/// Escape the characters that are significant inside HTML text.
std::string htmlEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&#39;";
      break;
    default:
      out.push_back(c);
    }
  }
  return out;
}

std::string humanSize(std::uintmax_t bytes) {
  static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    ++u;
  }
  std::ostringstream os;
  if (u == 0)
    os << bytes << " B";
  else
    os << std::fixed << std::setprecision(1) << v << " " << units[u];
  return os.str();
}

} // namespace

const uuids::uuid FileServer::getUUID() {
  if (uuid.is_nil()) {
    uuids::uuid_name_generator gen(uuids::uuid_namespace_url);
    uuid = gen("explo.webui.fileserver");
  }
  return uuid;
}

// ---------------------------------------------------------------------------------------------------------------------
// Server construction
// ---------------------------------------------------------------------------------------------------------------------

std::unique_ptr<httplib::Server> FileServer::buildServer(const fs::path &root) {
  std::error_code ec;
  if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
    logger->error("fileserver: served path '{}' does not exist or is not a directory", root.string());
    return nullptr;
  }

  std::unique_ptr<httplib::Server> svr;
  if (tlsEnabled()) {
    auto ssl = std::make_unique<httplib::SSLServer>(config.cert.c_str(), config.key.c_str());
    if (!ssl->is_valid()) {
      logger->error("fileserver: failed to load TLS certificate '{}' / key '{}'", config.cert, config.key);
      return nullptr;
    }
    svr = std::move(ssl);
  } else {
    svr = std::make_unique<httplib::Server>();
  }

  svr->set_logger([this](const httplib::Request &req, const httplib::Response &res) {
    logger->info("{} \"{} {} {}\" {} {}", req.remote_addr, req.method, req.path, req.version, res.status,
                 res.body.size());
  });

  installRoutes(*svr, root);
  return svr;
}

void FileServer::installRoutes(httplib::Server &svr, const fs::path &root) {
  const bool allowSubDirs = config.allowSubDirs;
  const bool autoindex = config.enableAutoindex;

  svr.Get(R"(/.*)", [this, root, allowSubDirs, autoindex](const httplib::Request &req, httplib::Response &res) {
    std::error_code ec;

    // Map the (already percent-decoded) request path onto the served root and
    // resolve it, following symlinks, so the containment check below cannot be
    // fooled by "..", absolute paths or links pointing outside the root.
    std::string rel = req.path;
    while (!rel.empty() && rel.front() == '/')
      rel.erase(rel.begin());
    fs::path target = fs::weakly_canonical(root / fs::path(rel), ec);
    if (ec) {
      res.status = 404;
      res.set_content("404 Not Found", "text/plain");
      return;
    }

    // Containment: the resolved target must be the root or live beneath it.
    fs::path within = fs::relative(target, root, ec);
    if (ec || within.empty() || (!within.empty() && within.begin()->string() == "..")) {
      logger->warn("fileserver: rejected out-of-root request '{}' from {}", req.path, req.remote_addr);
      res.status = 403;
      res.set_content("403 Forbidden", "text/plain");
      return;
    }
    const bool isRoot = (within == ".");

    // Depth of the target relative to the root (root itself == 0).
    std::size_t depth = 0;
    if (!isRoot)
      for (const auto &part : within)
        ++depth;

    if (!fs::exists(target, ec)) {
      res.status = 404;
      res.set_content("404 Not Found", "text/plain");
      return;
    }

    // Subdirectory policy: when disabled, only the root listing and files that
    // sit directly in the root are reachable.
    if (!allowSubDirs && !isRoot && (depth > 1 || fs::is_directory(target, ec))) {
      res.status = 403;
      res.set_content("403 Forbidden: subdirectory access is disabled", "text/plain");
      return;
    }

    // ---- Directory: autoindex -------------------------------------------------
    if (fs::is_directory(target, ec)) {
      if (!autoindex) {
        res.status = 403;
        res.set_content("403 Forbidden: directory listing is disabled", "text/plain");
        return;
      }

      std::string base = req.path;
      if (base.empty() || base.back() != '/')
        base += '/';

      std::ostringstream html;
      const std::string title = htmlEscape(req.path);
      html << "<!doctype html><html><head><meta charset=\"utf-8\">"
           << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           << "<title>Index of " << title << "</title></head><body>"
           << "<h1>Index of " << title << "</h1><hr><ul>";

      if (!isRoot)
        html << "<li><a href=\"../\">../</a></li>";

      std::vector<fs::directory_entry> entries;
      for (const auto &e : fs::directory_iterator(target, fs::directory_options::skip_permission_denied, ec))
        entries.push_back(e);
      std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &a, const fs::directory_entry &b) {
        bool ad = a.is_directory(), bd = b.is_directory();
        if (ad != bd)
          return ad > bd; // directories first
        return a.path().filename().string() < b.path().filename().string();
      });

      for (const auto &e : entries) {
        const bool dir = e.is_directory(ec);
        if (dir && !allowSubDirs)
          continue; // nothing reachable inside, so do not advertise it
        const std::string name = e.path().filename().string();
        std::string href = base + urlEncode(name) + (dir ? "/" : "");
        html << "<li><a href=\"" << htmlEscape(href) << "\">" << htmlEscape(name) << (dir ? "/" : "") << "</a>";
        if (!dir) {
          auto sz = e.file_size(ec);
          if (!ec)
            html << " <span>(" << humanSize(sz) << ")</span>";
        }
        html << "</li>";
      }
      html << "</ul><hr></body></html>";
      res.set_content(html.str(), "text/html");
      return;
    }

    // ---- Regular file: stream as a download ----------------------------------
    if (!fs::is_regular_file(target, ec)) {
      res.status = 404;
      res.set_content("404 Not Found", "text/plain");
      return;
    }

    auto size = fs::file_size(target, ec);
    if (ec) {
      res.status = 500;
      res.set_content("500 Internal Server Error", "text/plain");
      return;
    }

    auto ifs = std::make_shared<std::ifstream>(target, std::ios::binary);
    if (!ifs->is_open()) {
      res.status = 500;
      res.set_content("500 Internal Server Error", "text/plain");
      return;
    }

    res.set_header("Content-Disposition", "attachment; filename=\"" + target.filename().string() + "\"");
    res.set_content_provider(
        static_cast<size_t>(size), mimeFor(target),
        [ifs](size_t offset, size_t length, httplib::DataSink &sink) -> bool {
          char buf[65536];
          ifs->seekg(static_cast<std::streamoff>(offset));
          size_t want = std::min(length, sizeof(buf));
          ifs->read(buf, static_cast<std::streamsize>(want));
          std::streamsize got = ifs->gcount();
          if (got <= 0)
            return false;
          return sink.write(buf, static_cast<size_t>(got));
        },
        [ifs](bool) { ifs->close(); });
  });
}

// ---------------------------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------------------------

void FileServer::initialize(initArgs &args) {
  this->logger = args.logger;
  getUUID();

  // Interactive command context (mirrors the other tool plugins).
  auto ctx = std::make_shared<cmd::Context>(this->getName());
  {
    cmd::CommandDef c;
    c.name = "start";
    c.description = "Start the file download web server in the background";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string {
      this->readVars();
      std::string msg;
      {
        std::lock_guard<std::mutex> lk(mtx);
        msg = startLocked();
      }
      publishVars();
      return msg;
    };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "stop";
    c.description = "Stop the running file download web server";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string {
      std::string msg;
      {
        std::lock_guard<std::mutex> lk(mtx);
        msg = stopLocked();
      }
      publishVars();
      return msg;
    };
    ctx->registerCommand(c);
  }
  {
    cmd::CommandDef c;
    c.name = "status";
    c.description = "Show the file download web server status";
    c.handler = [this](const cmd::ExecutionContext &) -> std::string { return this->statusString(); };
    ctx->registerCommand(c);
  }
  cmd::CommandProcessor::instance().registerContext(ctx);

  // Command-line sub-command: `fileserver [options]`.
  fileServerCommand.emplace(
      *args.parser, "fileserver", "Host a web server for downloading files", [this](args::Subparser &s) {
        args::ValueFlag<std::string> bind(s, "address", "Address to bind to (default " CONFIG_WEBUI_FILESERVER_BIND ")",
                                          {"bind"}, std::string(CONFIG_WEBUI_FILESERVER_BIND));
        args::ValueFlag<int> port(s, "port", "Port to listen on (default 8080)", {"port"},
                                  CONFIG_WEBUI_FILESERVER_PORT);
        args::ValueFlag<std::string> path(s, "path", "Directory to serve (default ./)", {"path"},
                                          std::string(CONFIG_WEBUI_FILESERVER_PATH));
        args::Flag subDirs(s, "sub-dirs", "Allow access to subdirectories", {"sub-dirs"});
        args::Flag noSubDirs(s, "no-sub-dirs", "Deny access to subdirectories", {"no-sub-dirs"});
        args::Flag autoindex(s, "autoindex", "Enable HTML directory listings", {"autoindex"});
        args::Flag noAutoindex(s, "no-autoindex", "Disable HTML directory listings", {"no-autoindex"});
        args::ValueFlag<std::string> cert(s, "cert", "TLS certificate file (enables TLS with --key)", {"cert"},
                                          std::string(CONFIG_WEBUI_FILESERVER_CERT));
        args::ValueFlag<std::string> key(s, "key", "TLS key file (enables TLS with --cert)", {"key"},
                                         std::string(CONFIG_WEBUI_FILESERVER_KEY));
        args::HelpFlag help(s, "help", "Display this help menu", {'h', "help"});
        s.Parse();

        config.bind = bind.Get();
        config.port = port.Get();
        config.path = path.Get();
        if (subDirs)
          config.allowSubDirs = true;
        if (noSubDirs)
          config.allowSubDirs = false;
        if (autoindex)
          config.enableAutoindex = true;
        if (noAutoindex)
          config.enableAutoindex = false;
        config.cert = cert.Get();
        config.key = key.Get();
      });

  logger->info("Tool {} initialized successfully.", this->getName());
}

bool FileServer::cmdCheck() {
  if (!(fileServerCommand && *fileServerCommand))
    return false;
  std::cout << serveBlocking() << std::endl;
  return true;
}

void FileServer::shutdown() {
  std::lock_guard<std::mutex> lk(mtx);
  stopLocked();
  logger->info("Shutting down tool: {}", this->getName());
}

// ---------------------------------------------------------------------------------------------------------------------
// Background (interactive) lifecycle
// ---------------------------------------------------------------------------------------------------------------------

std::string FileServer::startLocked() {
  if (running.load())
    return "file server already running on " + config.bind + ":" + std::to_string(config.port);

  fs::path root = fs::weakly_canonical(fs::absolute(config.path));
  auto svr = buildServer(root);
  if (!svr)
    return "\033[1;31mfile server: failed to start (check path and TLS settings)\033[0m";

  server = std::move(svr);
  running.store(true);
  const std::string bind = config.bind;
  const int port = config.port;
  const bool tls = tlsEnabled();
  serverThread = std::thread([this, bind, port]() {
    if (!server->listen(bind, port))
      logger->error("fileserver: failed to bind {}:{}", bind, port);
    running.store(false);
  });

  logger->info("fileserver: serving '{}' on {}://{}:{}", root.string(), tls ? "https" : "http", bind, port);
  return "file server started: " + std::string(tls ? "https" : "http") + "://" + bind + ":" + std::to_string(port) +
         " serving " + root.string();
}

std::string FileServer::stopLocked() {
  if (!running.load() && !serverThread.joinable())
    return "file server is not running";
  if (server)
    server->stop();
  if (serverThread.joinable())
    serverThread.join();
  server.reset();
  running.store(false);
  logger->info("fileserver: stopped");
  return "file server stopped";
}

std::string FileServer::statusString() {
  std::lock_guard<std::mutex> lk(mtx);
  std::ostringstream os;
  os << "state   : " << (running.load() ? "running" : "stopped") << "\n";
  os << "bind    : " << config.bind << ":" << config.port << "\n";
  os << "path    : " << config.path << "\n";
  os << "subdirs : " << (config.allowSubDirs ? "allowed" : "denied") << "\n";
  os << "autoindex: " << (config.enableAutoindex ? "on" : "off") << "\n";
  os << "tls     : " << (tlsEnabled() ? "on" : "off");
  return os.str();
}

// ---------------------------------------------------------------------------------------------------------------------
// Foreground (command-line) lifecycle
// ---------------------------------------------------------------------------------------------------------------------

std::string FileServer::serveBlocking() {
  fs::path root = fs::weakly_canonical(fs::absolute(config.path));
  auto svr = buildServer(root);
  if (!svr)
    return "\033[1;31mfile server: failed to start (check path and TLS settings)\033[0m";

  server = std::move(svr);
  running.store(true);
  const bool tls = tlsEnabled();
  std::cout << "File server running at " << (tls ? "https" : "http") << "://" << config.bind << ":" << config.port
            << " serving " << root.string() << "\n"
            << "Subdirectories: " << (config.allowSubDirs ? "allowed" : "denied")
            << ", autoindex: " << (config.enableAutoindex ? "on" : "off") << "\n"
            << "Press Ctrl-C to stop." << std::endl;
  logger->info("fileserver: serving '{}' on {}://{}:{}", root.string(), tls ? "https" : "http", config.bind,
               config.port);

  if (!server->listen(config.bind, config.port)) {
    running.store(false);
    server.reset();
    return "\033[1;31mfile server: failed to bind " + config.bind + ":" + std::to_string(config.port) + "\033[0m";
  }
  running.store(false);
  server.reset();
  return "file server stopped";
}

// ---------------------------------------------------------------------------------------------------------------------
// Variables (interactive UI)
// ---------------------------------------------------------------------------------------------------------------------

void FileServer::execute() {
  this->readVars();
  std::string msg;
  {
    std::lock_guard<std::mutex> lk(mtx);
    msg = startLocked();
  }
  publishVars();
  std::cout << msg << std::endl;
}

void FileServer::invoke(std::string_view prefix, bool soft) {
  this->prefix = prefix;
  auto &vars = cmd::CommandProcessor::instance().vars();
  varSnap.restore(); // bring back the user's last settings across reselects

  const auto seed = [&](const std::string &key, cmd::VarValue def) {
    if (vars.get(key).has_value()) {
      if (soft)
        logger->info("Variable '{}' already exists, skipping due to soft invoke", key);
      return;
    }
    vars.set(key, std::move(def));
  };
  seed(this->prefix + ".bind", cmd::VarValue{config.bind});
  seed(this->prefix + ".port", cmd::VarValue{static_cast<long long>(config.port)});
  seed(this->prefix + ".path", cmd::VarValue{config.path});
  seed(this->prefix + ".subdirs", cmd::VarValue{config.allowSubDirs});
  seed(this->prefix + ".autoindex", cmd::VarValue{config.enableAutoindex});
  seed(this->prefix + ".cert", cmd::VarValue{config.cert});
  seed(this->prefix + ".key", cmd::VarValue{config.key});
}

void FileServer::suppress() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  for (const char *n : {".bind", ".port", ".path", ".subdirs", ".autoindex", ".cert", ".key"})
    varSnap.save(this->prefix + n);
  for (const char *n : {".bind", ".port", ".path", ".subdirs", ".autoindex", ".cert", ".key"})
    vars.unset(this->prefix + n);
}

void FileServer::readVars() {
  if (this->prefix.empty())
    return;
  auto &vars = cmd::CommandProcessor::instance().vars();
  // Settings are latched while the server is up, so a change never implies a
  // silent restart.
  if (running.load())
    return;
  if (auto v = vars.get(this->prefix + ".bind"))
    config.bind = v->toString();
  if (auto v = vars.get(this->prefix + ".port")) {
    long long p = v->toInt();
    if (p > 0 && p <= 65535)
      config.port = static_cast<int>(p);
  }
  if (auto v = vars.get(this->prefix + ".path"))
    config.path = v->toString();
  if (auto v = vars.get(this->prefix + ".subdirs"))
    config.allowSubDirs = v->toBool();
  if (auto v = vars.get(this->prefix + ".autoindex"))
    config.enableAutoindex = v->toBool();
  if (auto v = vars.get(this->prefix + ".cert"))
    config.cert = v->toString();
  if (auto v = vars.get(this->prefix + ".key"))
    config.key = v->toString();
}

void FileServer::publishVars() {
  auto &vars = cmd::CommandProcessor::instance().vars();
  if (this->prefix.empty())
    return;
  vars.set(this->prefix + ".bind", cmd::VarValue{config.bind});
  vars.set(this->prefix + ".port", cmd::VarValue{static_cast<long long>(config.port)});
  vars.set(this->prefix + ".path", cmd::VarValue{config.path});
  vars.set(this->prefix + ".subdirs", cmd::VarValue{config.allowSubDirs});
  vars.set(this->prefix + ".autoindex", cmd::VarValue{config.enableAutoindex});
  vars.set(this->prefix + ".cert", cmd::VarValue{config.cert});
  vars.set(this->prefix + ".key", cmd::VarValue{config.key});
}

// Register the tool with the framework (picked up when the plugin .so loads).
static ToolRegistry::Add<FileServer> fileServerRegister("fileserver");

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
