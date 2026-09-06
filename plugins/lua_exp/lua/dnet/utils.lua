local c = require("colors")

---@type function
---@param cidr string
---@return table
function CIDRToIPs(cidr)
  local ip, mask = cidr:match("^(%d+%.%d+%.%d+%.%d+)/(%d+)$")
  if not ip or not mask then
    return { "Invalid CIDR format" }
  end

  local maskNum = tonumber(mask)

  local function ipToNum(ip_)
    local num = 0
    for octet in string.gmatch(ip_, "%d+") do
      if tonumber(octet) < 0 or tonumber(octet) > 255 then
        return nil
      end
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
  if not ipNum then
    return { "Invalid IP address" }
  end
  maskNum = 0xFFFFFFFF - (2 ^ (32 - maskNum) - 2)
  local networkNum = ipNum & maskNum
  local broadcastNum = networkNum + (0xFFFFFFFF - maskNum)

  local IPs = {}
  for i = networkNum + 1, broadcastNum do
    table.insert(IPs, numToIp(i))
  end
  return IPs
end

function MakeIps(target)
  local ips = {}
  for w in target:gmatch("([^,]+)") do
    if w:find("/") then
      local cidrIps = CIDRToIPs(w)
      for _, ip in ipairs(cidrIps) do
        table.insert(ips, ip)
      end
    else
      table.insert(ips, w)
    end
  end
  return ips
end

---@tpye function
---@param portRanges string
---@return table
function MakePortRange(portRanges)
  local ports = {}
  for por in portRanges:gmatch("([^,]+)") do
    local startPort, endPort = por:match("(%d+)-(%d+)")
    if startPort and endPort then
      for p = tonumber(startPort), tonumber(endPort) do
        ports[p] = p
      end
    else
      local singlePort = tonumber(por)
      if singlePort then
        ports[singlePort] = singlePort
      end
    end
  end
  return ports
end

function IsV4(ip)
  if type(ip) ~= "string" then
    return false
  end
  local octets = { ip:match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$") }
  if #octets ~= 4 then
    return false
  end
  for _, octet in ipairs(octets) do
    local num = tonumber(octet)
    if not num or num < 0 or num > 255 then
      return false
    end
  end
  return true
end

function IsV6(ip)
  if type(ip) ~= "string" then
    return false
  end
  local segments = { ip:match("^(%x+):(%x+):(%x+):(%x+):(%x+):(%x+):(%x+):(%x+)$") }
  if #segments ~= 8 then
    return false
  end
  for _, segment in ipairs(segments) do
    if #segment > 4 then
      return false
    end
  end
  return true
end

function IpToDomain(ip)
  if IsV4(ip) then
    return AF_INET
  end
  if IsV6(ip) then
    return AF_INET6
  end
  return nil
end

function TypeToNum(type)
  if type == "tcp" then
    return SOCK_STREAM
  elseif type == "udp" then
    return SOCK_DGRAM
  else
    return nil
  end
end

S2a = function(status)
  local str = ""
  for k, v in pairs(explo.ConnectionStatus) do
    if v == status then
      str = k
      break
    end
  end
  return str
end

function Contains(tbl, val)
  return tbl[val] ~= nil
end

function S2c(status)
  local red = {
    [explo.ConnectionStatus.Closed] = true,
    [explo.ConnectionStatus.Timeout] = true,
    [explo.ConnectionStatus.Refused] = true,
    [explo.ConnectionStatus.HostUnreachable] = true,
    [explo.ConnectionStatus.NetworkUnreachable] = true,
  }
  if status == explo.ConnectionStatus.Open then
    return c.c(c.fg.green, S2a(status))
  elseif Contains(red, status) then
    return c.c(c.fg.red, S2a(status))
  elseif status == explo.ConnectionStatus.Filtered then
    return c.c(c.fg.yellow, S2a(status))
  elseif status == explo.ConnectionStatus.Unreachable then
    return c.c(c.fg.magenta, S2a(status))
  else
    return c.c(c.fg.white, S2a(status))
  end
end

function HostUp(ip)
  local sock = explo.socket(SOCK_STREAM)
  if not sock then
    return false
  end
  local connected = explo.connect(sock, ip, 80)
  while connected == EINPROGRESS do
    explo.sleep(0.1)
    connected = explo.connect(sock, ip, 80)
  end
  local res = explo.getsockopt(sock, SOL_SOCKET, SO_ERROR)
  if res == 0 then
    explo.close(sock)
    return 0
  else
    explo.close(sock)
    return res
  end
end
