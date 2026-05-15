---@type function
---@param target string
---@param port string
---@param method string
---@return integer
function PortScan(target, port, method)
  local ports = {}

  if string.find(port, "-") then
    explo.logi("Port range detected: " .. port)
    local start, finish = string.match(port, "(%d+)-(%d+)")
    if start == nil or finish == nil then
      explo.loge("Invalid port range format")
      return 1
    end
    start = tonumber(start)
    finish = tonumber(finish)
    if start == nil or finish == nil then
      explo.loge("Port range contains non-numeric values")
      return 1
    end
    if start < 0 or finish > 65535 or start > finish then
      explo.loge("Invalid port range values")
      return 1
    end
    for i = start, finish do
      table.insert(ports, i)
    end
  else
    explo.logi("Single port detected: " .. port)
    table.insert(ports, tonumber(port))
  end

  local openPorts = {}
  if method == "tcp" then
    explo.logi("Performing TCP scan on " .. target .. " for ports " .. port)
    print("Performing TCP scan on " .. target .. " for ports " .. port)
    for _, p in ipairs(ports) do
      local fd = explo.socket(SOCK_STREAM)
      if fd < 0 then
        explo.loge("Failed to create socket")
        return 1
      end
      if explo.connect(fd, target, p) then
        explo.logi("Port " .. p .. " is open")
        table.insert(openPorts, p)
      else
        explo.logi("Port " .. p .. " is closed")
      end
      explo.close(fd)
    end
  elseif method == "udp" then
    explo.logi("Performing UDP scan on " .. target .. " for ports " .. port)
    print("Performing UDP scan on " .. target .. " for ports " .. port)
    for _, p in ipairs(ports) do
      local fd = explo.socket(SOCK_DGRAM)
      if fd < 0 then
        explo.loge("Failed to create socket")
        return 1
      end
      if explo.connect(fd, target, p) then
        explo.logi("Port " .. p .. " is open")
        table.insert(openPorts, p)
      end
      explo.close(fd)
    end
  else
    explo.logw("Unknown method: " .. method)
    return 1
  end

  if #openPorts == 0 then
    explo.logi("No open ports found")
    print("No open ports found")
    return 0
  end

  for _, p in ipairs(openPorts) do
    print("Open port: " .. p)
  end
  return 0
end

---@type function
---@param target string
---@return integer
function NetworkScan(target)
  explo.logi("Performing network scan on " .. target)
  print("Performing network scan on " .. target)
  local ip, mask = string.match(target, "([^/]+)/(%d+)")
  if ip == nil or mask == nil then
    explo.loge("Invalid network format")
    return 1
  end
  local maskNum = tonumber(mask)
  if maskNum == nil or maskNum < 0 or maskNum > 32 then
    explo.loge("Invalid subnet mask")
    return 1
  end
  explo.logi("IP: " .. ip .. ", Mask: " .. mask)

  local function ipToNum(ip_)
    local num = 0
    for octet in string.gmatch(ip_, "%d+") do
      num = num * 256 + tonumber(octet)
    end
    return num
  end

  local function numToIp(num)
    local octets = {}
    for _ = 1, 4 do
      table.insert(octets, 1, num % 256)
      num = math.floor(num / 256)
    end
    return table.concat(octets, ".")
  end

  local ipNum = ipToNum(ip)
  maskNum = 0xFFFFFFFF - (2 ^ (32 - maskNum) - 2)
  local networkNum = ipNum & maskNum
  local broadcastNum = networkNum + (0xFFFFFFFF - maskNum)

  local activeHosts = {}
  for i = networkNum + 1, broadcastNum - 1 do
    local hostIp = numToIp(i)
    local fd = explo.socket(SOCK_STREAM)
    if fd >= 0 then
      table.insert(activeHosts, hostIp)
    end
  end

  return 0
end

---@type LuaTool
return {
  name = "dnet",
  version = "1.0.0",
  description = "A nmap like tool",
  tags = { "tool", "nmap" },
  vars = {
    target = "127.0.0.1",
    ports = "0-1024",
    method = "tcp",
  },
  initialize = function()
    explo.logi("Initializing pmap")
  end,
  shutdown = function()
    explo.logi("Shutting down pmap")
  end,
  execute = function()
    explo.logi("Executing pmap")
    local target = explo.var("target")
    local port = explo.var("ports")
    local method = explo.var("method")
    explo.logi("Target: " .. target)
    explo.logi("Ports: " .. port)
    explo.logi("Method: " .. method)
    if target == nil or port == nil or method == nil then
      explo.loge("One or more variables are nil")
      return 1
    end

    if type(target) ~= "string" or type(port) ~= "string" or type(method) ~= "string" then
      explo.loge("Variable 'target' is not a string")
      return 1
    end

    local result = -1

    if #port > 0 then
      result = PortScan(target, port, method)
    else
      result = NetworkScan(target)
    end

    return result
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2:
