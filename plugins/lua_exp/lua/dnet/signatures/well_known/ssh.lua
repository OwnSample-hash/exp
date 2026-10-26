local signature_ = "wk.ssh"

--- Extracts the software token from an identification string:
--- "SSH-2.0-OpenSSH_8.9p1 Ubuntu-3" -> "OpenSSH_8.9p1"
local function ssh_software(banner)
    if not banner then
        return nil
    end
    -- RFC 4253 allows other text lines before the identification string.
    return banner:match("^SSH%-%d%.%d+%-(%S+)") or banner:match("\nSSH%-%d%.%d+%-(%S+)")
end

--- "OpenSSH_8.9p1" -> "OpenSSH/8.9p1", "dropbear_2022.83" -> "dropbear/2022.83",
--- "ROSSSH" -> "ROSSSH"
local function format_software(token)
    local name, version = token:match("^(.-)[_%-]?(%d+%.[%w%.%-_]*)$")
    if not name or name == "" then
        return token
    end
    version = version:gsub("[%.%-_]+$", "")
    return name .. "/" .. version
end

---@type Signature
return {
    name = "ssh",
    version = "1.0.0",
    description = "SSH protocol signature",
    signature = signature_,
    ports = { { 22, SOCK_STREAM }, { 2222, SOCK_STREAM }, { 2022, SOCK_STREAM } },
    check = function(fd, host, port)
        explo.info("Checking SSH signature for host: " .. host .. " on port: " .. port)

        -- SSH servers normally speak first.
        local software = ssh_software(explo.read_block(fd, 1024))

        -- Some wait for the client's identification string first.
        if not software then
            explo.debug("No SSH banner from " .. host .. ":" .. port .. ", sending client identification")
            explo.write(fd, "SSH-2.0-explo\r\n")
            software = ssh_software(explo.read_block(fd, 1024))
        end

        if not software then
            explo.debug("Host " .. host .. ":" .. port .. " does not look like SSH")
            return nil
        end
        return format_software(software)
    end,
}
