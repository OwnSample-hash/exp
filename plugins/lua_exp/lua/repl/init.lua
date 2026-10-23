function Contains(tbl, val)
  return tbl[val] ~= nil
end

---@type LuaTool
return {
  name = "lua",
  version = "1.0.0",
  description = "A REPL for Lua",
  uuid = "203161c5-04b4-4925-91df-bbcb2e28a22e",
  tags = { "tool" },
  vars = {
    prompt = "> ",
  },
  desc = {
    prompt = "The prompt to display in the REPL",
  },
  rootRequired = false,
  makeCommand = true,
  initialize = function()
    explo.info("Initializing REPL")
  end,
  shutdown = function()
    explo.info("Shutting down REPL")
  end,
  execute = function()
    local prompt = explo.var("prompt")
    if type(prompt) ~= "string" then
      explo.error("Variable 'prompt' is not a string")
      return 1
    end

    local history = {} -- persists for the lifetime of this REPL session

    -- Terminal mode helpers (POSIX terminals; requires `stty`)
    local saved_mode
    local function raw_on()
      local p = io.popen("stty -g 2>/dev/null")
      if p then
        saved_mode = p:read("*l"); p:close()
      end
      os.execute("stty raw -echo 2>/dev/null")
    end
    local function raw_off()
      if saved_mode and saved_mode ~= "" then
        os.execute("stty " .. saved_mode .. " 2>/dev/null")
      else
        os.execute("stty sane 2>/dev/null")
      end
    end

    -- Read one edited line. Returns the string, or nil on EOF (Ctrl-D on empty line).
    local function readline()
      local buf, pos = "", 0    -- pos = cursor offset within buf
      local hidx = #history + 1 -- position in history (#history+1 = new line)
      local draft = ""          -- what you were typing before browsing history

      local function redraw()
        io.write("\r\27[K", prompt, buf)
        local back = #buf - pos
        if back > 0 then io.write("\27[", back, "D") end
        io.stdout:flush()
      end

      local function set(s) buf, pos = s, #s end

      local function loop()
        redraw()
        while true do
          local c = io.read(1)
          if c == nil then return nil end
          local b = c:byte()

          if c == "\r" or c == "\n" then
            return buf
          elseif b == 4 then -- Ctrl-D
            if buf == "" then return nil end
            buf = buf:sub(1, pos) .. buf:sub(pos + 2)
          elseif b == 3 then -- Ctrl-C: abandon line
            io.write("^C\r\n")
            buf, pos, hidx = "", 0, #history + 1
          elseif b == 127 or b == 8 then -- Backspace
            if pos > 0 then
              buf = buf:sub(1, pos - 1) .. buf:sub(pos + 1)
              pos = pos - 1
            end
          elseif b == 1 then
            pos = 0                        -- Ctrl-A: home
          elseif b == 5 then
            pos = #buf                     -- Ctrl-E: end
          elseif b == 21 then
            buf, pos = buf:sub(pos + 1), 0 -- Ctrl-U: kill to start
          elseif b == 11 then
            buf = buf:sub(1, pos)          -- Ctrl-K: kill to end
          elseif b == 27 then              -- Escape sequence
            local nxt = io.read(1)
            if nxt == "[" or nxt == "O" then
              local seq = ""
              repeat
                local ch = io.read(1)
                if not ch then return nil end
                seq = seq .. ch
              until ch:match("[%a~]")

              if seq == "D" then     -- Left
                if pos > 0 then pos = pos - 1 end
              elseif seq == "C" then -- Right
                if pos < #buf then pos = pos + 1 end
              elseif seq == "A" then -- Up
                if hidx > 1 then
                  if hidx == #history + 1 then draft = buf end
                  hidx = hidx - 1
                  set(history[hidx])
                end
              elseif seq == "B" then -- Down
                if hidx <= #history then
                  hidx = hidx + 1
                  set(hidx == #history + 1 and draft or history[hidx])
                end
              elseif seq == "H" or seq == "1~" then
                pos = 0               -- Home
              elseif seq == "F" or seq == "4~" then
                pos = #buf            -- End
              elseif seq == "3~" then -- Delete
                buf = buf:sub(1, pos) .. buf:sub(pos + 2)
              end
            end
          elseif b >= 32 then -- Printable character
            buf = buf:sub(1, pos) .. c .. buf:sub(pos + 1)
            pos = pos + 1
          end

          redraw()
        end
      end

      raw_on()
      local ok, res = pcall(loop)
      raw_off() -- always restore the terminal
      io.write("\r\n")
      if not ok then error(res, 0) end
      return res
    end

    print(
      "Lua REPL. Type 'exit' or 'quit' to exit.\nA special variable '_le' holds the result of the last expression evaluated.")
    while true do
      local input = readline()
      if input == nil then break end

      if input:match("%S") then
        if history[#history] ~= input then -- skip consecutive duplicates
          history[#history + 1] = input
        end

        if input == "exit" or input == "quit" then
          break
        end
        if input:match("=") == nil or not Contains({
              ["for"] = true,
              ["while"] = true,
              ["if"] = true,
              ["function"] = true,
              ["local"] = true,
              ["repeat"] = true,
              ["do"] = true,
              ["return"] = true,
              ["break"] = true,
              ["goto"] = true,
              ["end"] = true,
              ["else"] = true,
              ["elseif"] = true,
            }, input:match("^%s*([%w_]+)")) then
          input = "_le = " .. input
        end
        explo.trace("Executing input: " .. input)
        local func, err = load(input)
        if not func then
          if type(_G[input]) ~= "nil" then
            print(_G[input])
          else
            explo.error("Error loading input: " .. err)
            print(err)
          end
        else
          local success, result = xpcall(func, debug.traceback)
          if not success then
            explo.error("Error executing input: " .. result)
            print(debug.traceback(result))
          elseif result ~= nil then
            print(result)
          elseif type(_G["_le"]) ~= "nil" then
            print(_G["_le"])
          end
        end
      end
    end

    return 0
  end
}

-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
