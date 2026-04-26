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
}

explo = {
  ---@type function
  log = function(message) end,
}
