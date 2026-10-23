---@meta

---@class LuaTool
LuaTool = {
  ---@type string
  name = "",
  ---@type string
  version = "",
  ---@type string
  description = "",
  ---@type string[]
  tags = {},
  ---@type table<string, string|number|boolean|nil>
  vars = {},
  ---@type table<string, string>
  desc = {},
  ---@type boolean
  rootRequired = false,
  ---@type boolean
  makeCommand = false,
  ---@type function|string
  execute = function() end,
  ---@type function|string
  initialize = function() end,
  ---@type function|string
  shutdown = function() end,
  ---@type table<string, function<...>>
  commands = {},
}

---@class Signature
Signature = {
  ---@type string
  name = "",
  ---@type string
  version = "",
  ---@type string
  description = "",
  ---@type string
  signature = "",
  ---@type table<number, number>[]
  ports = {},
  ---@type function
  ---@param fd number
  ---@param host string
  ---@param port number
  ---@return string|nil
  check = function(fd, host, port) end,
}

---@class TLSClient
TLSClient = {}

---@type string
name = ""

explo = {
  ---@type function
  ---@param message string
  dbg = function(message) end,
  ---@type function
  ---@param message string
  info = function(message) end,
  ---@type function
  ---@param message string
  warn = function(message) end,
  ---@type function
  ---@param message string
  error = function(message) end,

  ---@type function
  ---@param name string
  ---@return string|number|boolean|nil
  var = function(name) end,


  ---@type function
  ---@param name string
  ---@return string|number|boolean|nil
  gVar = function(name) end,

  ---@type function
  ---@param tool string
  ---@param args table<string, string|number|boolean|nil>
  call = function(tool, args) end,

  ---@type function
  ---@param ms number
  sleep = function(ms) end,

  ---@type function
  ---@param type number
  ---@return number
  socket = function(type) end,
  ---@type function
  ---@param fd number
  ---@param lvl number
  ---@param opt number
  ---@return number
  getsockopt = function(fd, lvl, opt) end,
  ---@type function
  ---@param fd number
  ---@param host string
  ---@param port number
  ---@return boolean
  connect = function(fd, host, port) end,
  ---@type function
  ---@param fd number
  ---@param data string
  ---@return number
  write = function(fd, data) end,
  ---@type function
  ---@param fd number
  ---@param size number
  ---@return string
  read = function(fd, size) end,

  ---@type function
  ---@param fd number
  close = function(fd) end,

  ---@type function
  ---@param fd number
  ---@param host string
  ---@param port number
  ---@return TLSClient|nil
  sconnect = function(fd, host, port) end,
  ---@type function
  ---@param TLSClient TLSClient
  ---@param data string
  ---@return number
  swrite = function(TLSClient, data) end,
  ---@type function
  ---@param TLSClient TLSClient
  ---@param max_size number
  ---@return string
  sread = function(TLSClient, max_size) end,
  ---@type function
  ---@param TLSClient TLSClient
  ---@return boolean
  sclose = function(TLSClient) end,
  ---@type function
  ---@param fd number
  ---@param setSNI boolean
  ---@return TLSClient|nil
  supgrade = function(fd, setSNI) end,

  ---@type function
  ---@return number
  clock = function() end,

  ---@type function
  ---@param max number
  ---@param coro thread
  ---@param sigScan function
  ---@return table<string, number>
  async_scan = function(max, coro, sigScan) end,

  ---@type function
  ---@param ip string
  ---@param port number
  ---@return nil|string, string
  getnameinfo = function(ip, port) end,

  ---@type function
  ---@param path string
  ---@return table<string>|nil
  listDir = function(path) end,

  ConnectionStatus = {
    Open = 0,
    OpenUntested = 1,
    Filtered = 2,
    Error = 3,
    Timeout = 4,
    Refused = 5,
    Reset = 6,
    Closed = 7,
    Aborted = 8,
    NetReset = 9,
    HostUnreachable = 10,
    NetworkUnreachable = 11,
  },

  ---@type function
  ---@param table table
  ---@return string
  toJSON = function(table) end,

  ---@type function
  ---@param fd number
  ---@param count number
  ---@return string
  read_block = function(fd, count) end,
}

---@class HTTPConfig
HTTPConfig = {
  url = "",
  method = "",
  params = {},
  headers = {},
}

---@class HTTPResponse
HTTPResponse = {
  status_code = 0,
  headers = {},
  body = "",
}

AF_INET = 2
AF_INET6 = 10
SOCK_STREAM = 1
SOCK_DGRAM = 2
SOL_SOCKET = 1
SO_REUSEADDR = 2
SO_KEEPALIVE = 9
SO_BROADCAST = 6
SO_RCVBUF = 8
SO_SNDBUF = 7
SO_ERROR = 4
EHOSTDOWN = 112
EHOSTUNREACH = 114
EINPROGRESS = 115
