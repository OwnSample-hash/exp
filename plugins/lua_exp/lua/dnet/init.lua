require("utils")
local c = require("colors")

---@type LuaTool
return {
  name = "dnet",
  version = "1.0.0",
  description = "A nmap like tool",
  tags = { "tool", "nmap" },
  vars = {
    target = "127.0.0.1",
    ports = "1-10000",
    test_port = "443",
    method = "tcp",
  },
  initialize = function()
    explo.info("Initializing pmap")
  end,
  shutdown = function()
    explo.info("Shutting down pmap")
  end,
  execute = function()
    local start = explo.clock()
    explo.info("Executing dnet")
    local target = explo.var("target")
    local port = explo.var("ports")
    local test_port = explo.var("test_port")
    local method = explo.var("method")
    explo.info("Target: " .. target)
    explo.info("Ports: " .. port)
    explo.info("Test Port: " .. test_port)
    explo.info("Method: " .. method)
    if target == nil or port == nil or method == nil then
      explo.error("One or more variables are nil")
      return 1
    end

    if type(target) ~= "string" or type(port) ~= "string" or type(method) ~= "string" then
      explo.error("Variable 'target' is not a string")
      return 1
    end

    local result = -1

    local ips = MakeIps(target)
    local ports = MakePortRange(port)

    local longestPort = 0
    for _, p in pairs(ports) do
      if #tostring(p) > longestPort then
        longestPort = #tostring(p)
      end
    end

    local coroUpF = function()
      for w in method:gmatch("([^,]+)") do
        local type_ = TypeToNum(w:lower())
        for _, ip in ipairs(ips) do
          local domain = IpToDomain(ip)
          coroutine.yield(ip, test_port, type_, domain)
        end
      end
    end

    local types = {}
    for w in method:gmatch("([^,]+)") do
      local type_ = TypeToNum(w:lower())
      if type_ then
        table.insert(types, type_)
      end
    end

    local scan_start = explo.clock()
    local upRes = explo.async_scan(#ips * #types, coroutine.create(coroUpF))
    local took = (explo.clock() - scan_start) / 1000000000
    explo.info(string.format("Host up scan took %.2f seconds", took))

    if not upRes then
      explo.error("Up scan failed")
      return 1
    end

    local down_hosts = {}

    for k, v in pairs(upRes) do
      local dom, proto, ip, port_ = k:match("^(%g+):(%g+)://(%g+):(%g+)$")
      if dom and proto and ip and port_ then
        if
          v == explo.ConnectionStatus.HostUnreachable
          or v == explo.ConnectionStatus.NetworkUnreachable
          or v == explo.ConnectionStatus.Error
        then
          table.insert(down_hosts, ip)
          down_hosts[ip] = v
        end
      else
        explo.error("Invalid scan result format: " .. k)
      end
    end

    explo.info("Found " .. #down_hosts .. " down hosts")
    print("Found " .. #down_hosts .. " down hosts")

    local filtered_ip = {}
    for _, ip in ipairs(ips) do
      if not down_hosts[ip] then
        table.insert(filtered_ip, ip)
      end
    end

    explo.info("Scanning " .. #ips - #down_hosts .. " IPs and " .. #ports .. " ports with method: " .. method)
    -- TODO: Rewrite so if multiple addresses are given, scan them in parallel.
    local coroF = function()
      for w in method:gmatch("([^,]+)") do
        local type_ = TypeToNum(w:lower())
        for _, ip in pairs(filtered_ip) do
          local dom = IpToDomain(ip)
          for _, p in ipairs(ports) do
            coroutine.yield(ip, p, type_, dom)
          end
        end
      end
    end

    start = explo.clock()
    local scanRes = explo.async_scan((#ips - #down_hosts) * #ports * #types, coroutine.create(coroF))
    took = (explo.clock() - scan_start) / 1000000000
    explo.info(string.format("Scan took %.2f seconds", took))

    if not scanRes then
      explo.error("Scan failed")
      return 1
    end

    local stats = {}

    for k, v in pairs(scanRes) do
      local dom, proto, ip, port_ = k:match("^(%g+):(%g+)://(%g+):(%g+)$")
      if dom and proto and ip and port_ then
        stats[proto] = stats[proto] or {}
        stats[proto][ip] = stats[proto][ip] or {}
        stats[proto][ip][port_] = v
      else
        explo.error("Invalid scan result format: " .. k)
      end
    end

    local format_str = ("    Port: " .. c.fg.cyan .. "%%-%ds" .. c.reset .. " => Status: %%s"):format(longestPort)

    print("Scan completed. Summary:")
    for proto, ips_ in pairs(stats) do
      print(string.format("Protocol: " .. c.fg.magenta .. "%s" .. c.reset, proto))
      for ip, ports_ in pairs(ips_) do
        local kports = {}
        for port_ in pairs(ports_) do
          table.insert(kports, port_)
        end
        table.sort(kports, function(a, b)
          return tonumber(a) < tonumber(b)
        end)

        print(string.format("  IP: " .. c.fg.magenta .. "%s" .. c.reset, ip))
        local errors = {}
        for _, key in ipairs(kports) do
          local port_ = key
          local status = ports_[key]
          if status <= 1 then
            print(string.format(format_str, port_, S2c(status)))
          else
            errors[status] = errors[status] or 0
            errors[status] = errors[status] + 1
          end
        end
        if next(errors) then
          print(c.c(c.fg.red, "    Errors:"))
          for err, count in pairs(errors) do
            print(string.format("      Error code: %s => %d", S2c(err), count))
          end
        end
      end
    end

    print("    " .. #down_hosts .. c.c(c.fg.red, " hosts were down"))

    result = 0
    local main_time = (explo.clock() - start) / 1000000000
    print("Scan took " .. took .. " seconds and total execution time was " .. main_time .. " seconds.")
    return result
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2:
