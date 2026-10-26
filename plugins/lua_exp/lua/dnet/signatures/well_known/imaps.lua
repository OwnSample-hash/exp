local signature_ = "wk.imaps"

-- Known applications: { lowercase lua pattern, display name }. Most specific first.
local products = {
    { "microsoft exchange", "Microsoft Exchange" },
    { "microsoft esmtp", "Microsoft ESMTP" },
    { "lotus domino", "Lotus Domino" },
    { "courier%-imap", "Courier-IMAP" },
    { "courier%-pop3", "Courier-POP3" },
    { "cyrus imap", "Cyrus IMAP" },
    { "cyrus pop3", "Cyrus POP3" },
    { "opensmtpd", "OpenSMTPD" },
    { "hmailserver", "hMailServer" },
    { "smartermail", "SmarterMail" },
    { "mailenable", "MailEnable" },
    { "postfix", "Postfix" },
    { "sendmail", "Sendmail" },
    { "exim", "Exim" },
    { "qmail", "qmail" },
    { "haraka", "Haraka" },
    { "dovecot", "Dovecot" },
    { "qpopper", "Qpopper" },
    { "teapop", "Teapop" },
    { "zimbra", "Zimbra" },
    { "mdaemon", "MDaemon" },
    { "axigen", "Axigen" },
    { "kerio", "Kerio Connect" },
    { "icewarp", "IceWarp" },
    { "stalwart", "Stalwart" },
    { "gsmtp", "Gmail" },
    { "gimap", "Gmail" },
}

local function extract_version(rest)
    local v = rest:match("^[%s/]*[vV]?(%d+%.[%w%.%-_]*)")
        or rest:match("^[^%d]-[Vv]ersion[%s:]*(%d[%w%.%-_]*)")
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
    local clean = text:gsub("%b[]", ""):gsub("<[^>]*>", "")
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
    name = "imaps",
    version = "1.1.0",
    description = "IMAPS (implicit TLS) protocol signature",
    signature = signature_,
    ports = { { 993, SOCK_STREAM }, },
    check = function(fd, host, port)
        explo.info("Checking IMAPS signature for host: " .. host .. " on port: " .. port)
        explo.close(fd)
        local tls = explo.sconnect(host, port)
        if not tls then
            explo.error("Failed to establish TLS with " .. host .. ":" .. port .. ". Aborting signature check.")
            return nil
        end

        local result = nil
        local line = first_line(explo.sread(tls, 1024))
        if line and not (line:match("^%* OK") or line:match("^%* PREAUTH") or line:match("^%* BYE")) then
            line = nil
        end

        explo.swrite(tls, "a001 CAPABILITY\r\n")
        local resp = explo.sread(tls, 2048)
        local cap_ok = resp and (resp:match("%* CAPABILITY") or resp:match("a001 OK")
            or resp:match("a001 NO") or resp:match("a001 BAD")) ~= nil

        if line or cap_ok then
            explo.swrite(tls, "a002 LOGOUT\r\n")
            result = identify(line) or "IMAP"
        end

        if not explo.sclose(tls) then
            explo.error("Failed to close TLS socket for " .. host .. ":" .. port)
        end
        return result
    end,
}
