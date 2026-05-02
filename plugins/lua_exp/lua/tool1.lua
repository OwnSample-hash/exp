---@type LuaTool
return {
  name = "t1",
  version = "1.0.0",
  description = "A simple tool",
  tags = { "tool", "simple" },
  vars = {
    str = "test value",
    num = 42,
    bool = true,
    nop = nil,
  },
  initialize = function()
    explo.log("Initializing tool1")
  end,
  shutdown = function()
    explo.log("Shutting down tool1")
  end,
  execute = function(command)
    explo.log("tool1 executing command: " .. type(command) .. " - " .. tostring(command))
    local _str = explo.var("str")
    if _str == nil then
      explo.log("Variable 'str' is nil")
    else
      explo.log("Current value of 'str': " .. type(_str) .. " - " .. _str)
    end
    return 1
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2:
