local signature_ = "wk.ftp"

-- Known applications: { lowercase lua pattern, display name }. Most specific first.
local products = {
    { "microsoft ftp service", "Microsoft FTP Service" },
    { "filezilla server", "FileZilla Server" },
    { "pure%-ftpd", "Pure-FTPd" },
    { "vsftpd", "vsFTPd" },
    { "proftpd", "ProFTPD" },
    { "wu%-ftpd", "wu-ftpd" },
    { "serv%-u", "Serv-U" },
    { "titan ftp", "Titan FTP" },
    { "crushftp", "CrushFTP" },
    { "sftpgo", "SFTPGo" },
    { "pyftpdlib", "pyftpdlib" },
    { "bftpd", "bftpd" },
    { "glftpd", "glFTPd" },
    { "ncftpd", "NcFTPd" },
    { "tnftpd", "tnftpd" },
    { "gene6", "Gene6 FTP" },
    { "cerberus", "Cerberus FTP" },
    { "globalscape", "GlobalSCAPE" },
    { "xlight", "Xlight FTP" },
}

local function extract_version(rest)
    local v = rest:match("^[%s/]*[vV]?(%d+%.[%w%.%-_]*)")
        or rest:match("^[^%d]-[Vv]ersion[%s:]*(%d[%w%.%-_]*)")
        or rest:match("^[%s%a]-[vV](%d+%.[%w%.%-_]*)") -- e.g. "Serv-U FTP Server v15.2.3"
    if v then
        v = v:gsub("[%.%-_;,]+$", "")
    end
    return v
end

--- Turns a greeting into "<application>[/<version>]", or nil if the application is unknown.
local function identify(text)
    if not text then
        return nil
    end
    local clean = text:gsub("%b[]", "")
    local lower = clean:lower()
    for _, p in ipairs(products) do
        local _, e = lower:find("%f[%w]" .. p[1] .. "%f[%W]")
        if e then
            local version = extract_version(clean:sub(e + 1))
            if version and version ~= "" then
                return p[2] .. "/" .. version
            end
            return p[2]
        end
    end
    return nil
end

local function first_line(s)
    return s and s:match("^([^\r\n]+)")
end

---@type Signature
return {
    name = "ftp",
    version = "1.0.0",
    description = "FTP protocol signature",
    signature = signature_,
    ports = { { 21, SOCK_STREAM }, },
    check = function(fd, host, port)
        explo.info("Checking FTP signature for host: " .. host .. " on port: " .. port)

        -- FTP servers speak first: 220[- ]<text> (possibly multi-line)
        local banner = explo.read_block(fd, 1024)
        local line = first_line(banner)
        if not (line and line:match("^220[ %-]")) then
            banner = nil
        end

        -- SMTP also greets with 220, but answers NOOP with 250; FTP answers 200.
        explo.write(fd, "NOOP\r\n")
        local resp = explo.read_block(fd, 1024)
        local noop_ok = resp and (resp:match("^200[ %-]") or resp:find("\n200[ %-]")) ~= nil
        local looks_ftp = banner and banner:lower():find("ftp", 1, true) ~= nil

        if not (noop_ok or looks_ftp) then
            explo.debug("Host " .. host .. ":" .. port .. " does not look like FTP")
            return nil
        end

        explo.write(fd, "QUIT\r\n")
        return identify(banner) or "FTP"
    end,
}
