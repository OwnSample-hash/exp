#include <cmd.hpp>
#include <cmd/variable.hpp>
#include <fcntl.h>
#include <lib.hpp>
#include <llib.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <tls.hpp>
#include <utils.hpp>

#ifdef __linux__
#include <unistd.h>
#endif

extern "C" {
#include <lua.h>
}

using json = nlohmann::json;

static auto &getLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  if (!logger)
    logger = spdlog::get("lua_exp")->clone("lua_exp::lua::lib");
  return logger;
}

#define X(name, level)                                                                                                 \
  int name(lua_State *L) {                                                                                             \
    int nargs = lua_gettop(L);                                                                                         \
    std::string log_msg;                                                                                               \
    for (int i = 1; i <= nargs; i++) {                                                                                 \
      if (lua_isstring(L, i)) {                                                                                        \
        log_msg += lua_tostring(L, i);                                                                                 \
      } else {                                                                                                         \
        log_msg += "<non-string argument>";                                                                            \
      }                                                                                                                \
      if (i < nargs)                                                                                                   \
        log_msg += " ";                                                                                                \
    }                                                                                                                  \
    getLogger()->level("[Lua] {}", log_msg);                                                                           \
    return 0;                                                                                                          \
  }
luaLogFuncs
#undef X

    // clang-format off
int var(lua_State *L) {
  // clang-format on
  const char *env_var = luaL_checkstring(L, 1);
  std::string prefix;
  lua_getglobal(L, "name");
  const char *tool_name = lua_tostring(L, -1);
  if (tool_name)
    prefix = std::string(tool_name);
  else {
    getLogger()->warn("Lua attempted to access variable '{}' without a valid tool "
                      "name in the global 'name' variable. This may indicate a "
                      "misconfiguration or an attempt to access variables outside of "
                      "a tool context.",
                      env_var);
  }
  auto var = explo::cmd::CommandProcessor::instance().vars().get(prefix + "." + env_var);
  if (!var) {
    lua_pushnil(L);
    return 1;
  }
  getLogger()->trace("Lua is accessed variable '{}.{}' with type {}", prefix, env_var, static_cast<int>(var->type));
  switch (var->type) {
  case explo::cmd::VarType::String:
    lua_pushstring(L, var->toString().c_str());
    break;
  case explo::cmd::VarType::Integer:
    lua_pushinteger(L, var->toInt());
    break;
  case explo::cmd::VarType::Float:
    lua_pushnumber(L, var->fval);
    break;
  case explo::cmd::VarType::Bool:
    lua_pushboolean(L, var->toBool());
    break;
  case explo::cmd::VarType::Array: {
    getLogger()->warn("Lua attempted to access array variable '{}', which is not "
                      "directly supported. Returning nil.",
                      env_var);
    lua_pushnil(L);
    break;
  }
  }
  return 1;
}

json convertLuaTable(const LTW &table, int depth = 0) {
  static int recursion_counter;
  if (depth == 0) {
    recursion_counter = 0;
  } else {
    recursion_counter++;
    if (recursion_counter > 100) {
      getLogger()->error("Excessive recursion detected in Lua table conversion. Possible "
                         "circular reference. Returning null.");
      getLogger()->debug("Current recursion depth: {}, recursion counter: {}. "
                         "Possibly the table is _G",
                         depth, recursion_counter);
      __asm__("int3"); // Trigger a breakpoint for debugging
    }
  }
  json result;
  if (depth > 10) {
    getLogger()->warn("Maximum Lua table conversion depth exceeded. Possible circular "
                      "reference detected. Returning null.");
    return nullptr;
  }
  for (const auto &[key, value] : table.iterate()) {
    if (value.is<lua_Number>()) {
      result[key] = value.as<lua_Number>();
    } else if (value.is<std::string>()) {
      result[key] = value.as<std::string>();
    } else if (value.is<bool>()) {
      result[key] = value.as<bool>();
    } else if (value.is<LFW>()) {
      getLogger()->warn("Lua table contains function at key '{}', which cannot be "
                        "converted to JSON. Skipping this entry.",
                        key);
    } else if (value.is<LTW>()) {
      getLogger()->trace("Converting nested Lua table at key '{}'", key);
      result[key] = convertLuaTable(value.as<LTW>(), depth + 1);
    } else {
      getLogger()->warn("Lua table contains unsupported type at key '{}'. Skipping this "
                        "entry.",
                        key);
    }
  }
  return result;
}

extern std::map<std::string, std::shared_ptr<ITool>> tools;

int call(lua_State *L) {
  const char *toolName = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  LTW wrapper(L, 2);

  json args = convertLuaTable(wrapper);
  getLogger()->trace("Lua is calling command '{}' with arguments: {}", toolName, args.dump());

  auto it = tools.find(toolName);
  if (it == tools.end()) {
    getLogger()->error("Lua attempted to call unknown tool '{}'", toolName);
    lua_pushnil(L);
    return 1;
  }
  auto tool = it->second;
  auto &cpVars = cmd::CommandProcessor::instance().vars();
  try {
    tool->invoke(tool->getName());
    cpVars.set(tool->getName() + std::string(".args"), cmd::VarValue(args.dump()));
    tool->execute();
    tool->suppress();
  } catch (const std::exception &e) {
    getLogger()->error("Error invoking tool '{}': {}", toolName, e.what());
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, "<s>");
  return 1;
}

int sleep(lua_State *L) {
  int ms = luaL_checkinteger(L, 1);
  explo::lib::sleep(std::chrono::milliseconds(ms));
  return 0;
}

int socket_(lua_State *L) {
  int type = luaL_checkinteger(L, 1);
  getLogger()->trace("Lua is creating a new socket with type {}", type);
  int sockfd = explo::lib::socket(AF_INET, type, 0, SOCK_NONBLOCK);
  if (sockfd < 0) {
    getLogger()->error("Failed to create socket: {}", strerror(errno));
    lua_pushnil(L);
    return 1;
  }

  getLogger()->trace("Socket created with file descriptor {}", sockfd);
  lua_pushinteger(L, sockfd);
  return 1;
}

int connect_(lua_State *L) {
  int sockfd = luaL_checkinteger(L, 1);
  const char *ip = luaL_checkstring(L, 2);
  int port = luaL_checkinteger(L, 3);

  getLogger()->trace("Lua is connecting socket {} to {}:{}", sockfd, ip, port);

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
    getLogger()->error("Invalid IP address '{}': {}", ip, strerror(errno));
    lua_pushboolean(L, false);
    return 1;
  }

  struct timeval timeout;
  timeout.tv_sec = 1; // 1 seconds timeout
  timeout.tv_usec = 0;
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(sockfd, &write_fds);

  if (explo::lib::connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0 && errno != EINPROGRESS) {
    getLogger()->error("Failed to initiate connection on socket {}: {}", sockfd, strerror(errno));
    lua_pushboolean(L, false);
    return 1;
  }
  if (explo::lib::select(sockfd + 1, nullptr, &write_fds, nullptr, &timeout) == 1) {
    int so_err;
    socklen_t len = sizeof(so_err);
    if (explo::lib::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0 || so_err != 0) {
      getLogger()->error("Failed to connect socket {}: {}", sockfd, strerror(so_err));
      lua_pushboolean(L, false);
      return 1;
    }
    if (so_err == 0) {
      getLogger()->trace("Socket {} connected successfully", sockfd);
      lua_pushboolean(L, true);
      return 1;
    }
  } else {
    getLogger()->error("Connection timed out for socket {}", sockfd);
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushboolean(L, false);
  return 1;
}

int write_(lua_State *L) {
  int fd = luaL_checkinteger(L, 1);
  size_t data_len;
  const char *data = luaL_checklstring(L, 2, &data_len);

  getLogger()->trace("Lua is writing to fd {}", fd);

  ssize_t bytes_sent = explo::lib::write(fd, data, data_len);
  if (bytes_sent < 0) {
    getLogger()->error("Failed to write tofd {}: {}", fd, strerror(errno));
    lua_pushnil(L);
    return 1;
  }
  getLogger()->trace("Wrote {} bytes to fd {}", bytes_sent, fd);
  lua_pushinteger(L, bytes_sent);
  return 1;
}

int read_(lua_State *L) {
  int fd = luaL_checkinteger(L, 1);
  int max_len = luaL_checkinteger(L, 2);

  getLogger()->trace("Lua is reading up to {} bytes from fd {}", max_len, fd);

  std::string buffer(max_len, '\0');
  ssize_t bytes_read = explo::lib::read(fd, buffer.data(), max_len);
  if (bytes_read < 0) {
    getLogger()->error("Failed to read from fd {}: {}", fd, strerror(errno));
    lua_pushnil(L);
    return 1;
  }
  getLogger()->trace("Read {} bytes from fd {}", bytes_read, fd);
  lua_pushlstring(L, buffer.data(), bytes_read);
  return 1;
}

int close_(lua_State *L) {
  int fd = luaL_checkinteger(L, 1);

  getLogger()->trace("Lua is closing fd {}", fd);

  if (explo::lib::close(fd) < 0) {
    getLogger()->error("Failed to close fd {}: {}", fd, strerror(errno));
    lua_pushboolean(L, false);
    return 1;
  }
  getLogger()->trace("Closed fd {} successfully", fd);
  lua_pushboolean(L, true);
  return 1;
}

int sconnect(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  explo::lib::TLSClient *tlsClient_ = static_cast<explo::lib::TLSClient *>(lua_touserdata(L, -1));
  if (tlsClient_) {
    getLogger()->warn(
        "Lua attempted to establish a new TLS connection while having one already. Deleting the old one.");
    delete tlsClient_;
  }

  int sockfd = luaL_checkinteger(L, 1);
  const char *host = luaL_checkstring(L, 2);
  int port = luaL_checkinteger(L, 3);

  explo::lib::TLSClient *tlsClient = explo::lib::createTLSClient();

  if (!tlsClient->connect(host, port)) {
    lua_pushboolean(L, false);
    return 1;
  }
  lua_pushlightuserdata(L, tlsClient);
  lua_setfield(L, LUA_REGISTRYINDEX, "tlsClient");

  lua_pushboolean(L, true);
  return 1;
}

int swrite(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  explo::lib::TLSClient *tlsClient = static_cast<explo::lib::TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for swrite");
    lua_pushnil(L);
    return 1;
  }
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  int sent = tlsClient->sendData(std::string(data, data_len));
  if (sent < 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushnumber(L, sent);
  return 1;
}

int sread(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  explo::lib::TLSClient *tlsClient = static_cast<explo::lib::TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for sread");
    lua_pushnil(L);
    return 1;
  }
  int max_len = luaL_checkinteger(L, 1);

  std::string data = tlsClient->recvData(max_len);
  if (data.empty()) {
  }
  lua_pushlstring(L, data.c_str(), data.size());
  return 1;
}

int sclose(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  explo::lib::TLSClient *tlsClient = static_cast<explo::lib::TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for sclose");
    lua_pushboolean(L, false);
    return 1;
  }

  lua_pushlightuserdata(L, nullptr);
  lua_setfield(L, LUA_REGISTRYINDEX, "tlsClient");
  delete tlsClient;

  lua_pushboolean(L, true);
  return 1;
}

int clock(lua_State *L) {
  lua_pushnumber(L, explo::lib::clock());
  return 1;
}

int async_scan(lua_State *L) {
  auto &logger = getLogger();
  int max = luaL_checkinteger(L, 1);

  luaL_checktype(L, 2, LUA_TTHREAD);
  lua_State *co = lua_tothread(L, 2);
  int nargs = lua_gettop(L) - 2;
  lua_xmove(L, co, nargs);

  auto results = explo::lib::async_scan(max, [&]() -> std::tuple<std::string, int, int, int> {
    std::string host;
    int port, domain, type;
    int nres;
    int status = lua_resume(co, L, nargs, &nres);

    if (status == LUA_YIELD) {
      if (nres != 4) {
        logger->error("Lua coroutine yielded with insufficient results: {} excepted 4", nres);
        return std::make_tuple("", 0, 0, 0);
      }
      host = lua_tostring(co, -4);
      port = lua_tointeger(co, -3);
      type = lua_tointeger(co, -2);
      domain = lua_tointeger(co, -1);
      lua_pop(co, nres);
      nargs = 0; // Reset nargs after the first resume
    } else if (status == LUA_OK) {
      logger->info("Lua coroutine completed successfully");
    } else {
      const char *err_msg = lua_tostring(co, -1);
      logger->error("Lua coroutine error: {}", err_msg ? err_msg : "Unknown error");
    }
    return std::make_tuple(host, port, domain, type);
  });

  lua_createtable(L, 0, results.size());

  for (const auto &[key, status] : results) {
    lua_pushnumber(L, status);
    lua_setfield(L, -2, key.c_str());
  }
  return 1;
}

int getsockopt_(lua_State *L) {
  int sockfd = luaL_checkinteger(L, 1);
  int level = luaL_checkinteger(L, 2);
  int optname = luaL_checkinteger(L, 3);

  int optval;
  socklen_t optlen = sizeof(optval);

  if (explo::lib::getsockopt(sockfd, level, optname, &optval, &optlen) < 0) {
    getLogger()->error("Failed to get socket option for fd {}: {}", sockfd, strerror(errno));
    lua_pushnil(L);
    return 1;
  }

  lua_pushinteger(L, optval);
  return 1;
}

// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
