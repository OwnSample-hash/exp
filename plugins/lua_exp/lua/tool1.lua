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
    explo.logi("Initializing tool1")
    if name == nil then
      explo.logw("Tool name is nil")
      for k, v in pairs(_G) do
        explo.logi("Global variable: " .. k .. " - " .. type(v))
        explo.logi("Value: " .. tostring(v))
      end
    else
      explo.logi("Tool name: " .. type(name) .. " - " .. name)
    end
  end,
  shutdown = function()
    explo.logi("Shutting down tool1")
  end,
  execute = function()
    explo.logd("Executing tool1")
    explo.logi("Executing tool1")
    explo.logw("Executing tool1")
    explo.loge("Executing tool1")
    local _str = explo.var("str")
    if _str == nil then
      explo.loge("Variable 'str' is nil")
    else
      explo.logi("Current value of 'str': " .. type(_str) .. " - " .. _str)
    end
    return 1
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2:
