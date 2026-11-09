---@meta

---@class LuaTool
LuaTool = {
  ---@type string
  name = "",
  ---@type string
  version = "",
  ---@type string
  description = "",
  ---@type string
  uuid = "",
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
  info = function(message) end,
  ---@type function
  ---@param message string
  warn = function(message) end,
  ---@type function
  ---@param message string
  error = function(message) end,
  ---@type function
  ---@param message string
  debug = function(message) end,
  ---@type function
  ---@param message string
  trace = function(message) end,


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
  ---@param host string
  ---@param port number
  ---@return TLSClient|nil
  sconnect = function(host, port) end,
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
  ---@type string absolute URL, e.g. "https://host:443/path?x=1"
  url = "",
  ---@type string HTTP verb (GET/POST/PUT/PATCH/DELETE/...), default "GET"
  method = "",
  ---@type table<string, string|number|boolean> form fields -> urlencoded body
  params = {},
  ---@type table<string, string> request headers
  headers = {},
  ---@type table<string, string|number|boolean>|nil extra query-string params
  query = nil,
  ---@type string|nil raw request body (takes precedence over params/json)
  body = nil,
  ---@type string|nil raw JSON body (sets Content-Type: application/json)
  json = nil,
  ---@type boolean|nil force (true) / disable (false) TLS; default: by url scheme
  tls = nil,
  ---@type boolean|nil if true, request() returns the raw response string
  raw = nil,
}

---@class HTTPResponse
HTTPResponse = {
  ---@type number parsed status code (-1 if unparseable, 444 if empty response)
  status_code = 0,
  ---@type string|nil HTTP version, e.g. "1.1"
  version = nil,
  ---@type string|nil reason phrase, e.g. "OK"
  status_text = nil,
  ---@type table<string, string> response headers (duplicates folded with ", ")
  headers = {},
  ---@type string response body (chunked bodies are decoded)
  body = "",
  ---@type string the full raw response string as received
  raw = "",
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
