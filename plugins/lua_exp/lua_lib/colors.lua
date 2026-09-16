return {
  reset = "\27[0m",

  -- Text styles
  style = {
    bold = "\27[1m",
    dim = "\27[2m",
    italic = "\27[3m",
    underline = "\27[4m",
    blink = "\27[5m",
    reverse = "\27[7m",
    hidden = "\27[8m",
    strike = "\27[9m",
  },

  -- Foreground (text) colors
  fg = {
    black = "\27[30m",
    red = "\27[31m",
    green = "\27[32m",
    yellow = "\27[33m",
    blue = "\27[34m",
    magenta = "\27[35m",
    cyan = "\27[36m",
    white = "\27[37m",
    default = "\27[39m",

    -- Bright variants
    bright_black = "\27[90m",
    bright_red = "\27[91m",
    bright_green = "\27[92m",
    bright_yellow = "\27[93m",
    bright_blue = "\27[94m",
    bright_magenta = "\27[95m",
    bright_cyan = "\27[96m",
    bright_white = "\27[97m",
  },

  -- Background colors
  bg = {
    black = "\27[40m",
    red = "\27[41m",
    green = "\27[42m",
    yellow = "\27[43m",
    blue = "\27[44m",
    magenta = "\27[45m",
    cyan = "\27[46m",
    white = "\27[47m",
    default = "\27[49m",

    -- Bright variants
    bright_black = "\27[100m",
    bright_red = "\27[101m",
    bright_green = "\27[102m",
    bright_yellow = "\27[103m",
    bright_blue = "\27[104m",
    bright_magenta = "\27[105m",
    bright_cyan = "\27[106m",
    bright_white = "\27[107m",
  },
  c = function(color_code, text)
    return color_code .. text .. "\27[0m"
  end,
  fg256 = function(n)
    return string.format("\27[38;5;%dm", n)
  end,
  bg256 = function(n)
    return string.format("\27[48;5;%dm", n)
  end,

  fg_rgb = function(r, g, b)
    return string.format("\27[38;2;%d;%d;%dm", r, g, b)
  end,

  bg_rgb = function(r, g, b)
    return string.format("\27[48;2;%d;%d;%dm", r, g, b)
  end,
}
-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
