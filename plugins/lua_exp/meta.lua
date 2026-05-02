---@meta

---@class InitArgs
InitArgs = {}

---@class LuaTool
LuaTool = {
  ---@type string
  name = "",
  ---@type string
  version = "",
  ---@type string
  description = "",
  ---@type string[]
  tags = {},
  ---@type table<string, string|number|boolean|nil>
  vars = {},
  ---@type boolean
  rootRequired = false,
  ---@type function|string
  execute = function() end,
  ---@type function|string
  ---@param args InitArgs
  initialize = function(args) end,
  ---@type function|string
  shutdown = function() end,
}

---@type string
name = ""

explo = {
  ---@type function
  ---@param message string
  logd = function(message) end,
  ---@type function
  ---@param message string
  logi = function(message) end,
  ---@type function
  ---@param message string
  logw = function(message) end,
  ---@type function
  ---@param message string
  loge = function(message) end,

  ---@type function
  ---@param name string
  ---@return string|number|boolean|nil
  var = function(name) end,
}
