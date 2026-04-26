---@type LuaTool
return {
  name = "t1",
  version = "1.0.0",
  description = "A simple tool",
  tags = { "tool", "simple" },
  initialize = function()
    print("Initializing tool1")
  end,
  shutdown = function()
    print("Shutting down tool1")
  end,
  execute = function(command)
    -- local explo = require("explo")
    explo.log("tool1 executing command: " .. type(command) .. " - " .. tostring(command))
    print("Executing command: " .. command)
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2:
