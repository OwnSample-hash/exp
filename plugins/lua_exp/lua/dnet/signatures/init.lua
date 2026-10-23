---@type table<number, table<number, table<string, Signature>>>
local Signatures = {}

local tool_path = explo.gVar("tool_path")
if not tool_path or type(tool_path) ~= "string" then
  explo.warn("tool_path variable is not set")
  return {}
end

local wkSigs = explo.listDir(tool_path .. "/signatures/well_known")

if wkSigs == nil then
  explo.warn("No well-known signatures found")
  explo.trace(type(wkSigs))
  explo.trace(tool_path .. "/signatures/well_known")
  return {}
end

for name, metadata in pairs(wkSigs) do
  explo.info(name)
  if metadata.is_regular_file then
    local sigPath = name
    local sigTableRaw, err = loadfile(sigPath)
    if err then
      explo.error("Failed to load signature file: " .. sigPath .. " Error: " .. err)
      goto continue
    end
    ---@type Signature|nil
    local sigTable = sigTableRaw and sigTableRaw()
    if type(sigTable) ~= "table" then
      explo.error("Signature file did not return a table: " .. sigPath)
      goto continue
    end
    explo.trace(("Loaded signature: %s from file: %s"):format(sigTable.signature, sigPath))
    for _, port in ipairs(sigTable.ports) do
      local port_, type = table.unpack(port)
      explo.trace(string.format("Adding signature: %s for port: %d and type: %s", sigTable.signature, port_, type))
      if not Signatures[type] then
        Signatures[type] = {}
      end
      if not Signatures[type][port_] then
        Signatures[type][port_] = {}
      end
      Signatures[type][port_][sigTable.signature] = sigTable
    end
  else
    explo.warn("Skipping non-regular file in well-known signatures directory: " .. name)
  end
  ::continue::
end

explo.info("Loaded well-known signatures: " .. #(wkSigs) .. " signatures")
explo.info("Signatures table: " .. #Signatures .. " types")

for type, port in pairs(Signatures) do
  for port_, sigs in pairs(port) do
    for sigName, sig in pairs(sigs) do
      explo.info(string.format("Loaded signature: %s for type: %s on port: %d (%s)", sigName, type, port_, sig.signature))
    end
  end
end

return Signatures

-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
