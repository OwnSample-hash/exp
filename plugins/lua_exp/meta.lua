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
  ---@type function|string
  execute = function() end,
  ---@type function|string
  ---@param args InitArgs
  initialize = function(args) end,
  ---@type function|string
  shutdown = function() end,
  ---@type table<string, string|number|boolean|nil>
  vars = {},
}

explo = {
  ---@type function
  ---@param message string
  log = function(message) end,
  ---@type function
  ---@param name string
  var = function(name) end,
}
