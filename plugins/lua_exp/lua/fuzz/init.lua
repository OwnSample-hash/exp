local c = {

  reset = "\27[0m",

  -- Text styles
  style = {
    bold = "\27[1m",
    dim = "\27[2m",
    italic = "\27[3m",
    underline = "\27[4m",
    blink = "\27[5m",
    reverse = "\27[7m",
    hidden = "\27[8m",
    strike = "\27[9m",
  },

  -- Foreground (text) colors
  fg = {
    black = "\27[30m",
    red = "\27[31m",
    green = "\27[32m",
    yellow = "\27[33m",
    blue = "\27[34m",
    magenta = "\27[35m",
    cyan = "\27[36m",
    white = "\27[37m",
    default = "\27[39m",

    -- Bright variants
    bright_black = "\27[90m",
    bright_red = "\27[91m",
    bright_green = "\27[92m",
    bright_yellow = "\27[93m",
    bright_blue = "\27[94m",
    bright_magenta = "\27[95m",
    bright_cyan = "\27[96m",
    bright_white = "\27[97m",
  },

  -- Background colors
  bg = {
    black = "\27[40m",
    red = "\27[41m",
    green = "\27[42m",
    yellow = "\27[43m",
    blue = "\27[44m",
    magenta = "\27[45m",
    cyan = "\27[46m",
    white = "\27[47m",
    default = "\27[49m",

    -- Bright variants
    bright_black = "\27[100m",
    bright_red = "\27[101m",
    bright_green = "\27[102m",
    bright_yellow = "\27[103m",
    bright_blue = "\27[104m",
    bright_magenta = "\27[105m",
    bright_cyan = "\27[106m",
    bright_white = "\27[107m",
  },
}

-- Helper: wrap a string with a color and auto-reset
function c.w(color_code, text)
  return color_code .. text .. c.reset
end

-- Helper: 256-color foreground  (n = 0–255)
function c.fg256(n)
  return string.format("\27[38;5;%dm", n)
end

-- Helper: 256-color background  (n = 0–255)
function c.bg256(n)
  return string.format("\27[48;5;%dm", n)
end

-- Helper: true-color (RGB) foreground
function c.fg_rgb(r, g, b)
  return string.format("\27[38;2;%d;%d;%dm", r, g, b)
end

-- Helper: true-color (RGB) background
function c.bg_rgb(r, g, b)
  return string.format("\27[48;2;%d;%d;%dm", r, g, b)
end

---@param tbl table
---@param indent string
---@param printfn function
---@param depth number|nil
function DumpTable(tbl, indent, printfn, depth)
  if not indent then
    indent = ""
  end
  if not printfn then
    printfn = print
  end
  if not depth then
    depth = 0
  end
  if depth > 5 then
    explo.logw("Max depth reached, stopping dump to prevent infinite recursion")
    return indent .. "{...}\n"
  end
  if type(tbl) ~= "table" then
    explo.loge("Not a table: " .. tostring(tbl))
    return "Not table type: " .. indent .. tostring(tbl) .. "\n"
  end
  for k, v in pairs(tbl) do
    local key = tostring(k)
    if type(v) == "table" then
      printfn(indent .. key .. ":")
      DumpTable(v, indent .. "  ", printfn, depth + 1)
    else
      printfn(indent .. key .. ": " .. tostring(v))
    end
  end
end

-- Coro to generate all possible payloads based on the provided dict and ammount
--- @param tbl table
--- @param x number
function GenFuzzPayloads(tbl, x)
  return coroutine.wrap(function()
    local n = #tbl
    local indices = {}

    -- Initialize index array to all 1s
    for i = 1, x do
      indices[i] = 1
    end

    while true do
      -- Build and yield the current combination
      local result = {}
      for i = 1, x do
        result[i] = tbl[indices[i]]
      end
      coroutine.yield(result)

      -- Increment indices (like counting in base-n from the right)
      local pos = x
      while pos >= 1 do
        indices[pos] = indices[pos] + 1
        if indices[pos] <= n then
          break
        end
        indices[pos] = 1
        pos = pos - 1
      end

      -- If we've wrapped all the way around, we're done
      if pos < 1 then
        return
      end
    end
  end)
end

---@type function
---@param response string
---@return HTTPResponse
function ParseHTTPResponse(response)
  if #response == 0 then
    explo.logw("Empty response received")
    return {
      status_code = 444,
      headers = {},
      body = "Possibly a connection reset or no response from server",
    }
  end
  local status_code = response:match("HTTP/%d%.%d%s+(%d%d%d)")
  local headers = {}
  for key, value in response:gmatch("([%w-]+):%s*([^\r\n]+)") do
    headers[key] = value
  end
  local body = response:match("\r\n\r\n(.*)")
  local parsed = {
    status_code = tonumber(status_code),
    headers = headers,
    body = body,
  }
  if not parsed.status_code then
    explo.logw("Failed to parse status code from response, defaulting to 0")
    parsed.status_code = -1
  end
  if not parsed.headers then
    explo.logw("Failed to parse headers from response, defaulting to empty table")
    parsed.headers = {}
  end
  if not parsed.body then
    explo.logw("Failed to parse body from response, defaulting to empty string")
    parsed.body = ""
  end
  return parsed
end

---@type function
---@param data HTTPConfig
---@return HTTPResponse|number
function SendHttp(data)
  local fd = explo.socket(SOCK_STREAM)
  if not fd then
    explo.loge("Failed to create socket")
    return -1
  end

  local ip, port = data.url:match("://([^:/]+):?(%d*)")
  if ip and port then
    ip = ip
    port = tonumber(port) or 80
  else
    explo.logw("Failed to parse IP and port from URL, defaulting to localhost:80")
    ip = "localhost"
    port = 80
  end

  if not explo.connect(fd, ip, port) then
    explo.loge("Failed to connect to " .. ip .. ":" .. port)
    return -2
  end

  data.url = data.url:gsub("http[s]?://[^/]+", "") -- Remove scheme and host for the request line

  local body = ""
  if data.method ~= "GET" then
    data.headers["Content-Type"] = "application/x-www-form-urlencoded"
    for key, value in pairs(data.params) do
      body = body .. key .. "=" .. value .. "&"
    end
    body = body:sub(1, -2) -- Remove trailing '&'
    data.headers["Content-Length"] = tostring(#body)
  end

  local request = data.method .. " " .. data.url .. " HTTP/1.1\r\n"
  for key, value in pairs(data.headers) do
    request = request .. key .. ": " .. value .. "\r\n"
  end

  request = request .. "\r\n"
  if data.method ~= "GET" then
    request = request .. body
  end

  local bytes_written = explo.write(fd, request)
  if bytes_written <= 0 then
    explo.loge("Failed to send request")
    return -3
  end
  local response = explo.read(fd, 4096)
  if not response then
    explo.loge("Failed to read response")
    return -4
  end
  explo.logd("Received response: " .. response)
  return ParseHTTPResponse(response)
end

---@type function
---@param data HTTPConfig
---@return HTTPResponse|number
function SendHttps(data)
  local fd = 0
  local ip, port = data.url:match("://([^:/]+):?(%d*)")

  local tlc = debug.getregistry().tlsClient
  if tlc then
    explo.logi("Using TLS client from registry for HTTPS connection")
    goto existing_client
  end
  fd = explo.socket(SOCK_STREAM)
  if not fd then
    explo.loge("Failed to create socket")
    return -1
  end

  if ip and port then
    ip = ip
    port = tonumber(port) or 443
  else
    explo.logw("Failed to parse IP and port from URL, defaulting to localhost:443")
    ip = "localhost"
    port = 443
  end

  if not explo.sconnect(fd, ip, port) then
    explo.loge("Failed to connect to " .. ip .. ":" .. port)
    return -2
  end

  ::existing_client::
  data.url = data.url:gsub("http[s]?://[^/]+", "") -- Remove scheme and host for the request line

  local body = ""
  if data.method ~= "GET" then
    data.headers["Content-Type"] = "application/x-www-form-urlencoded"
    for key, value in pairs(data.params) do
      body = body .. key .. "=" .. value .. "&"
    end
    body = body:sub(1, -2) -- Remove trailing '&'
    data.headers["Content-Length"] = tostring(#body)
  end

  local request = data.method .. " " .. data.url .. " HTTP/1.1\r\n"
  for key, value in pairs(data.headers) do
    request = request .. key .. ": " .. value .. "\r\n"
  end

  request = request .. "\r\n"
  if data.method ~= "GET" then
    request = request .. body
  end

  local bytes_written = explo.swrite(request)
  if bytes_written <= 0 then
    explo.loge("Failed to send request")
    return -3
  end

  local response = explo.sread(4096)
  if not response then
    explo.loge("Failed to read response")
    return -4
  end

  local pr = ParseHTTPResponse(response)
  if
    (data.headers["Connection"] and data.headers["Connection"] == "Close")
    or (pr.headers["Connection"] and pr.headers["Connection"]:lower() == "close")
  then
    explo.logi("Closing TLS client due to Connection: Close header")
    explo.sclose()
    debug.getregistry().tlsClient = nil
  end

  return pr
end

---@type function
---@param tbl table
---@param val any
---@return boolean
function Contains(tbl, val)
  for _, v in ipairs(tbl) do
    if v == val then
      return true
    end
  end
  return false
end

---@type function
---@param response HTTPResponse
---@param call_data HTTPConfig
function HostAnalysis(response, call_data)
  if response.status_code == wrong_status_code then
    explo.logw("Received response with wrong status code: " .. response.status_code)
  elseif response.body and #response.body == wrong_response_size then
    explo.logw("Received response with wrong size: " .. #response.body)
  elseif Contains({ 200, 201, 202, 204, 205, 206 }, response.status_code) then
    print(
      "Host: "
        .. call_data.headers["Host"]
        .. " is up with: "
        .. c.w(c.fg.green, response.status_code)
        .. " and response size: "
        .. #response.body
    )
  elseif Contains({ 300, 301, 302 }, response.status_code) then
    explo.logi("Received redirection response with status code: " .. response.status_code)
    print(
      "Host: "
        .. call_data.headers["Host"]
        .. " is up with: "
        .. c.w(c.fg.yellow, response.status_code)
        .. " and response size: "
        .. #response.body
        .. " Location: "
        .. (response.headers["Location"] or "N/A")
    )
  elseif response.status_code == -1 then
    explo.logw("Failed to parse status code from response, treating as unknown response")
    print(
      "Host: "
        .. call_data.headers["Host"]
        .. " is up with: "
        .. c.w(c.fg.magenta, "Unknown Status Code")
        .. " and response size: "
        .. #response.body
    )
  else
    print(
      "Host: "
        .. call_data.headers["Host"]
        .. " is up with: "
        .. c.w(c.fg.red, response.status_code)
        .. " and response size: "
        .. #response.body
    )
  end
end

---@type function
---@param response HTTPResponse
---@param call_data HTTPConfig
function PathAnalysis(response, call_data)
  if response.status_code == wrong_status_code then
    explo.logw("Received response with wrong status code: " .. response.status_code)
  elseif response.body and #response.body == wrong_response_size then
    explo.logw("Received response with wrong size: " .. #response.body)
  elseif Contains({ 200, 201, 202, 204, 205, 206 }, response.status_code) then
    print(
      "Path: "
        .. call_data.url
        .. " is accessible with: "
        .. c.w(c.fg.green, response.status_code)
        .. " and response size: "
        .. #response.body
    )
  elseif Contains({ 300, 301, 302 }, response.status_code) then
    explo.logi("Received redirection response with status code: " .. response.status_code)
    print(
      "Path: "
        .. call_data.url
        .. " is accessible with: "
        .. c.w(c.fg.yellow, response.status_code)
        .. " and response size: "
        .. #response.body
        .. " Location: "
        .. (response.headers["Location"] or "N/A")
    )
  elseif response.status_code == -1 then
    explo.logw("Failed to parse status code from response, treating as unknown response")
    print(
      "Path: "
        .. call_data.url
        .. " is up with: "
        .. c.w(c.fg.magenta, "Unknown Status Code")
        .. " and response size: "
        .. #response.body
    )
  else
    print(
      "Path: "
        .. call_data.url
        .. " is accessible with: "
        .. c.w(c.fg.red, response.status_code)
        .. " and response size: "
        .. #response.body
    )
  end
end

---@type LuaTool
return {
  name = "fuzz",
  version = "0.1.0",
  description = "A simple fuzzing tool",
  tags = { "fuzz", "testing" },
  vars = {
    target = "http://localhost:9000",
    fuzz_key = "FUZZ",
    fuzz_ext_key = "FEXT",
    dict = "default.dict",
    ext_dict = "default.ext",
    kind = "GET",
    header = "Connection: Keep-Alive|User-Agent: fuzz-tool||Accept: */*",
    host = "FUZZ.localhost",
    data = "key=value&param=FUZZ",
    type_ = "host,path,data",
    wrong_status_code = 500,
    wrong_response_size = 1000,
    wrong_response_time = 1000,
    thread_delay = 10,
    threads = 4,
  },
  initialize = function()
    explo.logi("Initializing fuzz tool")
  end,
  shutdown = function()
    explo.logi("Shutting down fuzz tool")
  end,
  execute = function()
    local target = explo.var("target")
    local fuzz_key = explo.var("fuzz_key")
    local fuzz_ext_key = explo.var("fuzz_ext_key")
    local dict = explo.var("dict")
    local ext_dict = explo.var("ext_dict")
    local kind = explo.var("kind")
    local header = explo.var("header")
    local host = explo.var("host")
    local data = explo.var("data")
    local type_ = explo.var("type_")
    local wrong_status_code = explo.var("wrong_status_code")
    local wrong_response_size = explo.var("wrong_response_size")
    local wrong_response_time = explo.var("wrong_response_time")
    local thread_delay = explo.var("thread_delay")
    local threads = explo.var("threads")

    if
      type(target) ~= "string"
      or type(fuzz_key) ~= "string"
      or type(fuzz_ext_key) ~= "string"
      or type(dict) ~= "string"
      or type(ext_dict) ~= "string"
      or type(kind) ~= "string"
      or type(host) ~= "string"
      or type(header) ~= "string"
      or type(data) ~= "string"
      or type(type_) ~= "string"
    then
      explo.loge(
        "Invalid configuration: target, fuzz_key, fuzz_ext_key, dict, ext_dict, kind, header, host and data must be strings"
      )
      explo.logd(
        "Received types: "
          .. string.format(
            "target=%s, fuzz_key=%s, fuzz_ext_key=%s, dict=%s, ext_dict=%s kind=%s, header=%s, host=%s, data=%s, type_=%s",
            type(target),
            type(fuzz_key),
            type(fuzz_ext_key),
            type(dict),
            type(ext_dict),
            type(kind),
            type(header),
            type(host),
            type(data),
            type(type_)
          )
      )
      explo.logd(
        "Received values: "
          .. string.format(
            "target=%s, fuzz_key=%s, fuzz_ext_key=%s, dict=%s, ext_dict=%s kind=%s, header=%s, host=%s, data=%s, type_=%s",
            tostring(target),
            tostring(fuzz_key),
            tostring(fuzz_ext_key),
            tostring(dict),
            tostring(ext_dict),
            tostring(kind),
            tostring(header),
            tostring(host),
            tostring(data),
            tostring(type_)
          )
      )
      return 1
    end
    if
      type(wrong_status_code) ~= "number"
      or type(wrong_response_size) ~= "number"
      or type(wrong_response_time) ~= "number"
      or type(thread_delay) ~= "number"
      or type(threads) ~= "number"
    then
      explo.loge(
        "Invalid configuration: wrong_status_code, wrong_response_size, wrong_response_time, thread_delay and threads must be numbers"
      )
      explo.logd(
        "Received types: "
          .. string.format(
            "wrong_status_code=%s, wrong_response_size=%s, wrong_response_time=%s, thread_delay=%s, threads=%s",
            type(wrong_status_code),
            type(wrong_response_size),
            type(wrong_response_time),
            type(thread_delay),
            type(threads)
          )
      )
      return 2
    end

    _G.wrong_status_code = wrong_status_code
    _G.wrong_response_size = wrong_response_size
    _G.wrong_response_time = wrong_response_time

    explo.logi(string.format("Fuzzing %s with %d threads and delay of %d ms", target, threads, thread_delay))
    explo.logi(("Loading dict %s"):format(dict))
    local file = io.open(dict, "r")
    if not file then
      explo.loge(("Failed to open dict file: %s"):format(dict))
      return 3
    end

    local lines = {}
    for line in file:lines() do
      if line:find("^#") then
        explo.logd(("Skipping comment line: %s"):format(line))
        goto continue
      end
      table.insert(lines, line)
      ::continue::
    end
    file:close()

    if #lines == 0 then
      explo.loge("Dict file is empty or contains only comments")
      file:close()
      return 4
    end

    local exts = {}
    if type_:find("path") then
      file = io.open(ext_dict, "r")

      if not file then
        explo.loge(("Failed to open ext_dict file: %s"):format(ext_dict))
        return 3
      end

      for line in file:lines() do
        if line:find("^#") then
          explo.logd(("Skipping comment line in ext_dict: %s"):format(line))
          goto continue_ext
        end
        table.insert(exts, line)
        ::continue_ext::
      end

      file:close()
    end

    -- type_ = "host,path,data",
    local x = string.len(type_:gsub("%s+", "")) - string.len(type_:gsub("%s+", ""):gsub(",", "")) + 1
    if x <= 0 then
      explo.loge("Invalid type_ configuration, must contain at least one type (host, path, data)")
      return 5
    end
    if x > 5 then
      explo.logw("Too many types specified in type_, limiting to 5 for performance reasons")
      x = 5
    end

    local headers = {}
    for str in string.gmatch(header, "([^|]+)") do
      local key, value = str:match("([^:]+):%s*(.+)")
      if key and value then
        headers[key] = value
      else
        explo.logw(("Invalid header format: %s"):format(str))
      end
    end

    explo.logi(("Fuzzing with %d payloads per type"):format(x))
    local start = explo.clock()
    for words in GenFuzzPayloads(lines, x) do
      ---@type HTTPConfig
      local call_data = {
        url = target,
        method = kind,
        params = {},
        headers = headers,
      }
      local index = 1
      DumpTable(words, "dt: ", explo.logd)

      if type_:find("host") then
        explo.logi("Fuzzing host with payload: " .. words[index])
        if host then
          call_data.headers["Host"] = host:gsub(fuzz_key, words[index])
          index = index + 1
        else
          explo.logw("Host header not found, skipping host fuzzing")
        end
        local sender = nil
        if call_data.url:find("^https://") then
          call_data.url = call_data.url:gsub("^http://", "https://")
          explo.logi("Using HTTPS for fuzzing")
          explo.logd("Modified URL for HTTPS: " .. call_data.url)
          sender = SendHttps
        else
          sender = SendHttp
        end

        local response = sender(call_data)
        if type(response) == "number" then
          explo.loge("Error sending HTTP request, skipping response analysis")
          return 5
        end
        explo.logd("Received response with status code: " .. response.status_code)

        HostAnalysis(response, call_data)
      end

      if type_:find("path") then
        call_data.headers["Host"] = call_data.headers["Host"] or target:match("://([^:/]+)")

        for _, ext in ipairs(exts) do
          call_data.url = target

          local _, path_count = type_:gsub("path", "")
          explo.logt("Calculated path fuzzing count: " .. path_count)
          index = 1
          while path_count ~= 0 do
            call_data.url = call_data.url:gsub(fuzz_key, words[index], 1)
            index = index + 1
            path_count = path_count - 1
            explo.logt("Index: " .. index .. ", Remaining path fuzzing count: " .. path_count)
          end

          explo.logd("Fuzzing path with payload: " .. call_data.url)

          explo.logi("Fuzzing path with payload: " .. call_data.url .. " and extension: " .. ext)
          call_data.url = call_data.url:gsub(fuzz_ext_key, ext)
          index = index + 1
          local sender = nil
          if call_data.url:find("^https://") then
            call_data.url = call_data.url:gsub("^http://", "https://")
            explo.logi("Using HTTPS for fuzzing")
            explo.logd("Modified URL for HTTPS: " .. call_data.url)
            sender = SendHttps
          else
            sender = SendHttp
          end

          DumpTable(call_data, "call_data: ", explo.logd)
          local response = sender(call_data)
          if type(response) == "number" then
            explo.loge("Error sending HTTP request, skipping response analysis")
            return 5
          end
          explo.logd("Received response with status code: " .. response.status_code)

          PathAnalysis(response, call_data)
        end
      end

      -- if type_:find("data") then
      --   local data_count = string.len(type_:gsub("%s+", ""):gsub(",", ""):gsub("data", "")) / 4
      --
      --   for key, value in string.gmatch(data, "([^&=]+)=([^&=]+)") do
      --     explo.logi("Fuzzing data param '" .. key .. "' with payload: " .. words[index])
      --     call_data.params[key] = value:gsub(fuzz_key, words[index])
      --     if data_count then
      --       index = index + 1
      --       data_count = data_count - 1
      --     else
      --       break
      --     end
      --   end
      -- else
      --   for key, value in string.gmatch(data, "([^&=]+)=([^&=]+)") do
      --     call_data.params[key] = value
      --   end
      -- end

      explo.sleep(thread_delay)
    end

    if debug.getregistry().tlsClient then
      explo.logi("Closing TLS client from registry")
      explo.sclose()
    end

    explo.logi(string.format("Fuzzing completed in %.2f seconds", os.clock() - start))
    print("Fuzzing completed in " .. c.w(c.fg.cyan, string.format("%.2f seconds", (explo.clock() - start) / (10 ^ 9))))
    return 0
  end,
}

-- Vim: set expandtab tabstop=2 shiftwidth=2:
