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
---@param data HTTPConfig
---@return number
function SendHttp(data)
  local fd = explo.socket()
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
  return #response
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
    dict = "default.dict",
    kind = "GET",
    header = "Connection: Close|User-Agent: fuzz-tool||Accept: */*",
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
    local dict = explo.var("dict")
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
      or type(dict) ~= "string"
      or type(kind) ~= "string"
      or type(host) ~= "string"
      or type(header) ~= "string"
      or type(data) ~= "string"
      or type(type_) ~= "string"
    then
      explo.loge("Invalid configuration: target, fuzz_key, dict, kind, header, host and data must be strings")
      explo.logd(
        "Received types: "
          .. string.format(
            "target=%s, fuzz_key=%s, dict=%s, kind=%s, header=%s, host=%s, data=%s, type_=%s",
            type(target),
            type(fuzz_key),
            type(dict),
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
            "target=%s, fuzz_key=%s, dict=%s, kind=%s, header=%s, host=%s, data=%s, type_=%s",
            target,
            fuzz_key,
            dict,
            kind,
            header,
            host,
            data,
            type_
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

    if #lines == 0 then
      explo.loge("Dict file is empty or contains only comments")
      file:close()
      return 4
    end
    DumpTable(lines, "Loaded payload: ", explo.logd)

    -- type_ = "host,path,data",
    local x = string.len(type_:gsub("%s+", "")) - string.len(type_:gsub("%s+", ""):gsub(",", "")) + 1
    if x <= 0 then
      explo.loge("Invalid type_ configuration, must contain at least one type (host, path, data)")
      return 5
    end
    if x > 3 then
      explo.logw("Too many types specified in type_, limiting to 3 for performance reasons")
      x = 3
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
    for words in GenFuzzPayloads(lines, x) do
      ---@type HTTPConfig
      local call_data = {
        url = "",
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
      end

      if type_:find("path") then
        explo.logi("Fuzzing path with payload: " .. words[index])
        call_data.url = target:gsub(fuzz_key, words[index])
        index = index + 1
      else
        call_data.url = target
      end

      if type_:find("data") then
        local data_count = string.len(type_:gsub("%s+", ""):gsub(",", ""):gsub("data", "")) / 4

        for key, value in string.gmatch(data, "([^&=]+)=([^&=]+)") do
          explo.logi("Fuzzing data param '" .. key .. "' with payload: " .. words[index])
          call_data.params[key] = value:gsub(fuzz_key, words[index])
          if data_count then
            index = index + 1
            data_count = data_count - 1
          else
            break
          end
        end
      else
        for key, value in string.gmatch(data, "([^&=]+)=([^&=]+)") do
          call_data.params[key] = value
        end
      end

      explo.logd("Prepared call data:")
      DumpTable(call_data, " ", explo.logd)

      -- explo.call("cpr", call_data)
      local response_size = SendHttp(call_data)
      if response_size < 0 then
        explo.loge("Error sending HTTP request, skipping response analysis")
        return 5
      else
        explo.logd("Received response of size: " .. response_size)
        if response_size == wrong_response_size then
          explo.logw("Received response with wrong size: " .. response_size)
        end
      end
      explo.sleep(thread_delay)
    end

    file:close()
    return 0
  end,
}

-- Vim: set expandtab tabstop=2 shiftwidth=2:
