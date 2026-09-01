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
