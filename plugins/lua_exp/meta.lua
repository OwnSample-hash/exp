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
  ---@type boolean
  rootRequired = false,
  ---@type function|string
  execute = function() end,
  ---@type function|string
  initialize = function() end,
  ---@type function|string
  shutdown = function() end,
  ---@type table<string, function<...>>
  commands = {},
}

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
  ---@param host string
  ---@param port number
  ---@return boolean
  sconnect = function(fd, host, port) end,
  ---@type function
  ---@param data string
  ---@return number
  swrite = function(data) end,
  ---@type function
  ---@param max_size number
  ---@return string
  sread = function(max_size) end,
  ---@type function
  ---@return boolean
  sclose = function() end,

  ---@type function
  ---@return number
  clock = function() end,

  ---@type function
  ---@param max number
  ---@param coro thread
  async_scan = function(max, coro) end,

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
