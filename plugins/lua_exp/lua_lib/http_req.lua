-- http.lua - a small, generic HTTP/1.1 client + response parser.
--
-- Communication with the host is done exclusively through the `explo` global
-- (see meta.lua): explo.socket/connect/write/read/close for plain TCP and
-- explo.sconnect/swrite/sread/sclose for TLS.
--
-- Public API (module table):
--   http.request(cfg)            -> HTTPResponse|string, err
--   http.get/post/put/delete/... -> HTTPResponse|string, err
--   http.parse(str)              -> HTTPResponse            (parse any response string)
--   http.parse_url(url)          -> { scheme, host, port, path }
--   http.encode_form(tbl)        -> string
--
-- Backward-compatible globals (kept for existing tools such as `fuzz`):
--   SendHttp(cfg) / SendHttps(cfg) -> HTTPResponse | <negative number on error>
--   ParseHTTPResponse(str)         -> HTTPResponse

local M = {}

--------------------------------------------------------------------------------
-- helpers
--------------------------------------------------------------------------------

---Percent-encode a value for use in a form body or query string.
---@param s any
---@return string
local function url_encode(s)
  return (tostring(s):gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", string.byte(c))
  end))
end

---Encode a table as application/x-www-form-urlencoded (also used for query strings).
---@param tbl table<string, string|number|boolean>
---@return string
function M.encode_form(tbl)
  local parts = {}
  for k, v in pairs(tbl) do
    parts[#parts + 1] = url_encode(k) .. "=" .. url_encode(v)
  end
  return table.concat(parts, "&")
end

---Split a URL into its parts. Missing scheme defaults to http, missing port
---to the scheme default, missing path to "/".
---@param url string
---@return table @{ scheme = string, host = string, port = number, path = string }
function M.parse_url(url)
  url = tostring(url or "")
  local scheme, rest = url:match("^(%a[%w+.-]*)://(.*)$")
  if not scheme then
    scheme = "http"
    rest = url
  end
  scheme = scheme:lower()

  local hostport, path = rest:match("^([^/]*)(/?.*)$")
  if path == nil or path == "" then
    path = "/"
  end

  local host, port
  -- IPv6 literal: [::1]:8080
  host, port = hostport:match("^%[([^%]]+)%]:?(%d*)$")
  if not host then
    host, port = hostport:match("^([^:]*):?(%d*)$")
  end

  port = tonumber(port)
  if not port then
    port = (scheme == "https") and 443 or 80
  end

  return { scheme = scheme, host = host, port = port, path = path }
end

---Build a raw HTTP/1.1 request string.
---@param method string
---@param path string
---@param headers table<string, string>
---@param body string|nil
---@return string
local function build_request(method, path, headers, body)
  local lines = { method .. " " .. path .. " HTTP/1.1" }
  for k, v in pairs(headers) do
    lines[#lines + 1] = k .. ": " .. v
  end
  local req = table.concat(lines, "\r\n") .. "\r\n\r\n"
  if body and #body > 0 then
    req = req .. body
  end
  return req
end

---Open a transport. Returns a table with handle + write/read/close closures.
---@param host string
---@param port number
---@param use_tls boolean
---@return table|nil transport
---@return string|nil err
local function connect(host, port, use_tls)
  if use_tls then
    local cli = explo.sconnect(host, port)
    if not cli then
      return nil, "TLS connect to " .. tostring(host) .. ":" .. tostring(port) .. " failed"
    end
    return {
      handle = cli,
      write = function(h, d)
        return explo.swrite(h, d)
      end,
      read = function(h, n)
        return explo.sread(h, n)
      end,
      close = function(h)
        return explo.sclose(h)
      end,
    }
  end

  local fd = explo.socket(SOCK_STREAM)
  if not fd or fd < 0 then
    return nil, "socket() failed"
  end
  if not explo.connect(fd, host, port) then
    explo.close(fd)
    return nil, "connect to " .. tostring(host) .. ":" .. tostring(port) .. " failed"
  end
  return {
    handle = fd,
    write = function(h, d)
      return explo.write(h, d)
    end,
    read = function(h, n)
      return explo.read_block(h, n)
    end,
    close = function(h)
      return explo.close(h)
    end,
  }
end

local READ_CHUNK = 4096
local MAX_RESPONSE = 32 * 1024 * 1024 -- 32 MiB safety cap

---Read a full HTTP response from a transport. Honours Content-Length and
---chunked transfer-encoding; otherwise reads until the peer closes.
---@param t table transport
---@return string raw
local function recv_response(t)
  local buf = ""
  local header_end = nil -- index of the final byte of the "\r\n\r\n" separator
  local content_length = nil
  local chunked = false

  while true do
    if header_end then
      if chunked then
        -- zero-length terminating chunk: "...\r\n0\r\n\r\n"
        if buf:find("\r\n0\r\n", header_end, true) then
          break
        end
      elseif content_length then
        if (#buf - header_end) >= content_length then
          break
        end
      end
    end

    if #buf >= MAX_RESPONSE then
      explo.warn("HTTP response exceeded " .. MAX_RESPONSE .. " bytes, truncating")
      break
    end

    local chunk = t.read(t.handle, READ_CHUNK)
    if not chunk or #chunk == 0 then
      break
    end
    buf = buf .. chunk

    if not header_end then
      local s, e = buf:find("\r\n\r\n", 1, true)
      if s then
        header_end = e
        local head = buf:sub(1, s - 1)
        local cl = head:match("[Cc]ontent%-[Ll]ength:%s*(%d+)")
        if cl then
          content_length = tonumber(cl)
        end
        if head:match("[Tt]ransfer%-[Ee]ncoding:[^\r\n]*[Cc]hunked") then
          chunked = true
        end
      end
    end
  end

  return buf
end

---Decode a chunked transfer-encoding body.
---@param body string
---@return string
local function dechunk(body)
  local out = {}
  local pos = 1
  local n = #body
  while pos <= n do
    local line_end = body:find("\r\n", pos, true)
    if not line_end then
      break
    end
    local size_hex = body:sub(pos, line_end - 1):match("^(%x+)")
    if not size_hex then
      break
    end
    local size = tonumber(size_hex, 16)
    pos = line_end + 2
    if not size or size == 0 then
      break
    end
    out[#out + 1] = body:sub(pos, pos + size - 1)
    pos = pos + size + 2 -- data + trailing CRLF
  end
  return table.concat(out)
end

--------------------------------------------------------------------------------
-- parsing
--------------------------------------------------------------------------------

---Parse an HTTP response string into a structured table. Works on any response
---string, regardless of how it was obtained.
---@param response string
---@return HTTPResponse
function M.parse(response)
  if type(response) ~= "string" or #response == 0 then
    explo.warn("Empty response received")
    return {
      status_code = 444,
      status_text = "No Response",
      version = nil,
      headers = {},
      body = "Possibly a connection reset or no response from server",
      raw = type(response) == "string" and response or "",
    }
  end

  -- Split head / body on the first blank line (tolerate bare LF separators too).
  local head, body = response:match("^(.-)\r\n\r\n(.*)$")
  if not head then
    head, body = response:match("^(.-)\n\n(.*)$")
  end
  if not head then
    head = response
    body = ""
  end

  -- Status line.
  local status_line, header_block = head:match("^([^\r\n]*)\r?\n(.*)$")
  if not status_line then
    status_line = head
    header_block = ""
  end
  local version, status_code, status_text = status_line:match("^HTTP/(%d%.%d)%s+(%d%d%d)%s*(.*)$")
  status_code = tonumber(status_code)

  -- Headers (preserve order-independent, fold duplicates with ", ").
  local headers = {}
  for line in (header_block .. "\n"):gmatch("(.-)\r?\n") do
    if line ~= "" then
      local k, v = line:match("^([^:]+):%s*(.-)%s*$")
      if k then
        if headers[k] then
          headers[k] = headers[k] .. ", " .. v
        else
          headers[k] = v
        end
      end
    end
  end

  -- Decode chunked bodies transparently.
  local te = headers["Transfer-Encoding"] or headers["transfer-encoding"]
  if te and te:lower():find("chunked", 1, true) then
    body = dechunk(body)
  end

  local parsed = {
    version = version,
    status_code = status_code,
    status_text = (status_text and #status_text > 0) and status_text or nil,
    headers = headers,
    body = body or "",
    raw = response,
  }

  if not parsed.status_code then
    explo.warn("Failed to parse status code from response, defaulting to -1")
    parsed.status_code = -1
  end

  return parsed
end

--------------------------------------------------------------------------------
-- request
--------------------------------------------------------------------------------

---@param headers table collected headers (case preserved)
---@param present table<string, boolean> lowercased keys already set
---@param name string
---@param value string
local function set_default_header(headers, present, name, value)
  if not present[name:lower()] then
    headers[name] = value
    present[name:lower()] = true
  end
end

---Perform an HTTP(S) request.
---
---cfg fields:
---  url      string   (required) e.g. "https://host:443/path?x=1"
---  method   string   verb, default "GET" (any verb allowed)
---  headers  table    custom request headers
---  params   table    form fields -> x-www-form-urlencoded body
---  json     string   raw JSON body (sets Content-Type: application/json)
---  body     string   raw request body (takes precedence over params/json)
---  query    table    extra query-string params appended to the path
---  tls      boolean  force (true) / disable (false) TLS; default: by scheme
---  raw      boolean  if true, return the raw response string instead of a table
---
---@param cfg HTTPConfig|string
---@return HTTPResponse|string|nil result   parsed table, raw string, or nil on error
---@return string|nil err                     error message when result is nil
function M.request(cfg)
  if type(cfg) == "string" then
    cfg = { url = cfg }
  end
  if type(cfg) ~= "table" or type(cfg.url) ~= "string" then
    return nil, "request: cfg.url (string) is required"
  end

  local parsed = M.parse_url(cfg.url)
  if not parsed.host or parsed.host == "" then
    return nil, "could not parse host from url: " .. cfg.url
  end

  local method = tostring(cfg.method or "GET"):upper()

  -- Copy headers, tracking which (lowercased) keys the caller already set.
  local headers = {}
  local present = {}
  if type(cfg.headers) == "table" then
    for k, v in pairs(cfg.headers) do
      headers[k] = tostring(v)
      present[tostring(k):lower()] = true
    end
  end

  -- Body resolution: explicit body > json > form params.
  local body = nil
  if cfg.body ~= nil then
    body = tostring(cfg.body)
  elseif type(cfg.json) == "string" then
    body = cfg.json
    set_default_header(headers, present, "Content-Type", "application/json")
  elseif type(cfg.params) == "table" and next(cfg.params) ~= nil then
    body = M.encode_form(cfg.params)
    set_default_header(headers, present, "Content-Type", "application/x-www-form-urlencoded")
  end

  -- Request target (path + optional extra query).
  local path = parsed.path
  if type(cfg.query) == "table" and next(cfg.query) ~= nil then
    local qs = M.encode_form(cfg.query)
    path = path .. (path:find("?", 1, true) and "&" or "?") .. qs
  end

  -- Mandatory / sensible default headers.
  local host_header = parsed.host
  if (parsed.scheme == "https" and parsed.port ~= 443) or (parsed.scheme ~= "https" and parsed.port ~= 80) then
    host_header = host_header .. ":" .. parsed.port
  end
  set_default_header(headers, present, "Host", host_header)
  if body then
    set_default_header(headers, present, "Content-Length", tostring(#body))
  end
  -- Default to Connection: close so bodies without Content-Length read cleanly.
  set_default_header(headers, present, "Connection", "close")

  local use_tls
  if cfg.tls ~= nil then
    use_tls = cfg.tls and true or false
  else
    use_tls = (parsed.scheme == "https")
  end

  local transport, terr = connect(parsed.host, parsed.port, use_tls)
  if not transport then
    return nil, terr
  end

  local req = build_request(method, path, headers, body)
  explo.debug("HTTP request to " .. parsed.host .. ":" .. parsed.port .. "\n" .. req)

  local written = transport.write(transport.handle, req)
  if not written or written <= 0 then
    transport.close(transport.handle)
    return nil, "failed to send request to " .. parsed.host .. ":" .. parsed.port
  end

  local raw = recv_response(transport)
  transport.close(transport.handle)

  explo.debug("HTTP response: " .. #raw .. " bytes")

  if cfg.raw then
    return raw, nil
  end
  return M.parse(raw), nil
end

-- Verb convenience wrappers: http.get(url, opts), http.post(url, opts), ...
local function verb(method)
  return function(url, opts)
    opts = opts or {}
    opts.url = url
    opts.method = method
    return M.request(opts)
  end
end

M.get = verb("GET")
M.post = verb("POST")
M.put = verb("PUT")
M.patch = verb("PATCH")
M.delete = verb("DELETE")
M.head = verb("HEAD")
M.options = verb("OPTIONS")

--------------------------------------------------------------------------------
-- backward-compatible globals
--------------------------------------------------------------------------------

---@param response string
---@return HTTPResponse
function ParseHTTPResponse(response)
  return M.parse(response)
end

-- Legacy helper: force plain HTTP, return the parsed table or a negative number.
---@param data HTTPConfig
---@return HTTPResponse|number
function SendHttp(data)
  data.tls = false
  local res, err = M.request(data)
  if not res then
    explo.error("SendHttp: " .. (err or "unknown error"))
    return -1
  end
  return res
end

-- Legacy helper: force TLS, return the parsed table or a negative number.
---@param data HTTPConfig
---@return HTTPResponse|number
function SendHttps(data)
  data.tls = true
  local res, err = M.request(data)
  if not res then
    explo.error("SendHttps: " .. (err or "unknown error"))
    return -2
  end
  return res
end

M.ParseHTTPResponse = ParseHTTPResponse
M.SendHttp = SendHttp
M.SendHttps = SendHttps

return M

-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
