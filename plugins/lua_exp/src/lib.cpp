#include <arpa/inet.h>
#include <cmd.hpp>
#include <cmd/variable.hpp>
#include <lib.hpp>
#include <lua.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <tls.hpp>
#include <utils.hpp>

using json = nlohmann::json;

auto &getLogger() {
  static std::shared_ptr<spdlog::logger> logger;
  if (!logger)
    logger = spdlog::get("lua_exp")->clone("lua_exp::lua::lib");
  return logger;
}

#define X(name, level)                                                         \
  int name(lua_State *L) {                                                     \
    int nargs = lua_gettop(L);                                                 \
    std::string log_msg;                                                       \
    for (int i = 1; i <= nargs; i++) {                                         \
      if (lua_isstring(L, i)) {                                                \
        log_msg += lua_tostring(L, i);                                         \
      } else {                                                                 \
        log_msg += "<non-string argument>";                                    \
      }                                                                        \
      if (i < nargs)                                                           \
        log_msg += " ";                                                        \
    }                                                                          \
    getLogger()->level("[Lua] {}", log_msg);                                   \
    return 0;                                                                  \
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
    getLogger()->warn(
        "Lua attempted to access variable '{}' without a valid tool "
        "name in the global 'name' variable. This may indicate a "
        "misconfiguration or an attempt to access variables outside of "
        "a tool context.",
        env_var);
  }
  auto var = explo::cmd::CommandProcessor::instance().vars().get(prefix + "." +
                                                                 env_var);
  if (!var) {
    lua_pushnil(L);
    return 1;
  }
  getLogger()->trace("Lua is accessed variable '{}.{}' with type {}", prefix,
                     env_var, static_cast<int>(var->type));
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
    getLogger()->warn(
        "Lua attempted to access array variable '{}', which is not "
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
      getLogger()->error(
          "Excessive recursion detected in Lua table conversion. Possible "
          "circular reference. Returning null.");
      getLogger()->debug("Current recursion depth: {}, recursion counter: {}. "
                         "Possibly the table is _G",
                         depth, recursion_counter);
      __asm__("int3"); // Trigger a breakpoint for debugging
    }
  }
  json result;
  if (depth > 10) {
    getLogger()->warn(
        "Maximum Lua table conversion depth exceeded. Possible circular "
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
      getLogger()->warn(
          "Lua table contains function at key '{}', which cannot be "
          "converted to JSON. Skipping this entry.",
          key);
    } else if (value.is<LTW>()) {
      getLogger()->trace("Converting nested Lua table at key '{}'", key);
      result[key] = convertLuaTable(value.as<LTW>(), depth + 1);
    } else {
      getLogger()->warn(
          "Lua table contains unsupported type at key '{}'. Skipping this "
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
  getLogger()->trace("Lua is calling command '{}' with arguments: {}", toolName,
                     args.dump());

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
    cpVars.set(tool->getName() + std::string(".args"),
               cmd::VarValue(args.dump()));
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
  getLogger()->trace("Lua is sleeping for {} milliseconds", ms);
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  return 0;
}

int socket_(lua_State *L) {
  int type = luaL_checkinteger(L, 1);
  getLogger()->trace("Lua is creating a new socket with type {}", type);
  int sockfd = socket(AF_INET, type, 0);
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

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    getLogger()->error("Failed to connect socket {}: {}", sockfd,
                       strerror(errno));
    lua_pushboolean(L, false);
    return 1;
  }
  getLogger()->trace("Socket {} connected successfully", sockfd);
  lua_pushboolean(L, true);
  return 1;
}

int write_(lua_State *L) {
  int fd = luaL_checkinteger(L, 1);
  size_t data_len;
  const char *data = luaL_checklstring(L, 2, &data_len);

  getLogger()->trace("Lua is writing to fd {}", fd);

  ssize_t bytes_sent = write(fd, data, data_len);
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
  ssize_t bytes_read = read(fd, buffer.data(), max_len);
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

  if (close(fd) < 0) {
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
  TLSClient *tlsClient_ = static_cast<TLSClient *>(lua_touserdata(L, -1));
  if (tlsClient_) {
    getLogger()->warn(
        "Lua attempted to establish a new TLS connection while an existing "
        "TLS client is still active. This may indicate a resource leak or "
        "mismanagement. Previous TLS client will be overwritten.");
  }

  int sockfd = luaL_checkinteger(L, 1);
  const char *host = luaL_checkstring(L, 2);
  int port = luaL_checkinteger(L, 3);

  TLSClient *tlsClient = new TLSClient();
  tlsClient->logger->trace("Lua is connecting socket {} to {}:{}", sockfd, host,
                           port);

  if (!tlsClient->connect(host, port)) {
    tlsClient->logger->error("Failed to establish TLS connection to {}:{}",
                             host, port);
    lua_pushboolean(L, false);
    return 1;
  }
  tlsClient->logger->trace("TLS connection established successfully to {}:{}",
                           host, port);
  lua_pushlightuserdata(L, tlsClient);
  lua_setfield(L, LUA_REGISTRYINDEX, "tlsClient");

  lua_pushboolean(L, true);
  return 1;
}

int swrite(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  TLSClient *tlsClient = static_cast<TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for swrite");
    lua_pushnil(L);
    return 1;
  }
  size_t data_len;
  const char *data = luaL_checklstring(L, 1, &data_len);

  tlsClient->logger->trace("Lua is writing to TLS connection");

  int sent = tlsClient->send_data(std::string(data, data_len));
  if (sent < 0) {
    tlsClient->logger->error("Failed to send data over TLS connection");
    tlsClient->logger->error("Error details: {}", tlsClient->getLastError());
    lua_pushnil(L);
    return 1;
  }
  tlsClient->logger->trace("Data sent over TLS connection successfully");
  lua_pushnumber(L, sent);
  return 1;
}

int sread(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  TLSClient *tlsClient = static_cast<TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for sread");
    lua_pushnil(L);
    return 1;
  }
  int max_len = luaL_checkinteger(L, 1);

  tlsClient->logger->trace("Lua is reading up to {} bytes from TLS connection",
                           max_len);

  std::string data = tlsClient->recv_data(max_len);
  if (data.empty()) {
    tlsClient->logger->warn("Failed to read data from TLS connection");
    tlsClient->logger->warn("Error details: {}", tlsClient->getLastError());
  }
  tlsClient->logger->trace("Data received from TLS connection: '{}' bytes",
                           data.size());
  lua_pushlstring(L, data.c_str(), data.size());
  return 1;
}

int sclose(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "tlsClient");
  TLSClient *tlsClient = static_cast<TLSClient *>(lua_touserdata(L, -1));
  if (!tlsClient) {
    getLogger()->error("No TLS client found in registry for sclose");
    lua_pushboolean(L, false);
    return 1;
  }

  tlsClient->logger->trace("Lua is closing TLS connection");

  lua_pushlightuserdata(L, nullptr);
  lua_setfield(L, LUA_REGISTRYINDEX, "tlsClient");
  tlsClient->logger->trace("TLS connection closed successfully");
  delete tlsClient;

  lua_pushboolean(L, true);
  return 1;
}

int clock(lua_State *L) {
  auto now = std::chrono::high_resolution_clock::now();
  auto epoch = now.time_since_epoch();
  auto nanos =
      std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count();
  lua_pushnumber(L, nanos);
  return 1;
}
// Vim: set expandtab tabstop=2 shiftwidth=2:
