local signature_ = "wk.mysql"

--- "10.11.6-MariaDB-1:10.11.6+maria~ubu2204" -> "MariaDB/10.11.6"
--- "8.0.35-0ubuntu0.22.04.1"                 -> "MySQL/8.0.35"
local function identify_version(ver)
    local lower = ver:lower()
    local name = "MySQL"
    if lower:find("mariadb", 1, true) then
        name = "MariaDB"
        -- MariaDB prefixes "5.5.5-" for replication compatibility with old MySQL clients.
        ver = ver:gsub("^5%.5%.5%-", "")
    elseif lower:find("percona", 1, true) then
        name = "Percona Server"
    end
    local version = ver:match("^(%d+%.%d+%.%d+)") or ver:match("^(%d+%.%d+)")
    if version then
        return name .. "/" .. version
    end
    return name
end

--- Name only, from the text of a pre-authentication error packet.
local function identify_error(code, msg)
    local lower = msg:lower()
    if lower:find("mariadb", 1, true) then
        return "MariaDB"
    elseif lower:find("mysql", 1, true) then
        return "MySQL"
    end
    -- 1040 too many connections, 1129 host blocked, 1130 host not allowed
    if code == 1040 or code == 1129 or code == 1130 then
        return "MySQL"
    end
    return nil
end

--- Parses the server's first packet. Layout: 3-byte LE length, 1-byte sequence (0),
--- then payload: 0x0a, server version\0, thread id (4), scramble (8), filler (0x00), ...
local function parse_packet(data)
    if not data or #data < 6 then
        return nil
    end
    local seq = data:byte(4)
    local kind = data:byte(5)
    if seq ~= 0 then
        return nil
    end

    if kind == 0x0a then
        local z = data:find("\0", 6, true)
        if not z then
            return nil
        end
        local ver = data:sub(6, z - 1)
        if not ver:match("^%d+%.%d+") then
            return nil
        end
        -- filler byte after thread id + first 8 scramble bytes must be 0x00
        if #data >= z + 13 and data:byte(z + 13) ~= 0 then
            return nil
        end
        return identify_version(ver)
    elseif kind == 0xff then
        if #data < 8 then
            return nil
        end
        local code = data:byte(6) + data:byte(7) * 256
        local msg = data:sub(8)
        if msg:sub(1, 1) == "#" then
            msg = msg:sub(7) -- skip '#' + 5-char SQLSTATE
        end
        return identify_error(code, msg)
    end
    return nil
end

---@type Signature
return {
    name = "mysql",
    version = "1.0.0",
    description = "MySQL/MariaDB protocol signature",
    signature = signature_,
    ports = { { 3306, SOCK_STREAM }, },
    check = function(fd, host, port)
        explo.info("Checking MySQL signature for host: " .. host .. " on port: " .. port)

        -- The server sends its handshake without waiting for the client.
        local result = parse_packet(explo.read_block(fd, 1024))
        if not result then
            explo.debug("Host " .. host .. ":" .. port .. " does not look like MySQL/MariaDB")
        end
        return result
    end,
}
