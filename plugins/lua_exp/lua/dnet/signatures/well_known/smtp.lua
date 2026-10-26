local signature_ = "wk.smtp"
local CLIENT_NAME = "explo.local"

-- Known applications: { lowercase lua pattern, display name }. Most specific first.
local products = {
    { "microsoft exchange", "Microsoft Exchange" },
    { "microsoft esmtp",    "Microsoft ESMTP" },
    { "lotus domino",       "Lotus Domino" },
    { "courier%-imap",      "Courier-IMAP" },
    { "courier%-pop3",      "Courier-POP3" },
    { "cyrus imap",         "Cyrus IMAP" },
    { "cyrus pop3",         "Cyrus POP3" },
    { "opensmtpd",          "OpenSMTPD" },
    { "hmailserver",        "hMailServer" },
    { "smartermail",        "SmarterMail" },
    { "mailenable",         "MailEnable" },
    { "postfix",            "Postfix" },
    { "postcow",            "Postcow" },
    { "sendmail",           "Sendmail" },
    { "exim",               "Exim" },
    { "qmail",              "qmail" },
    { "haraka",             "Haraka" },
    { "dovecot",            "Dovecot" },
    { "qpopper",            "Qpopper" },
    { "teapop",             "Teapop" },
    { "zimbra",             "Zimbra" },
    { "mdaemon",            "MDaemon" },
    { "axigen",             "Axigen" },
    { "kerio",              "Kerio Connect" },
    { "icewarp",            "IceWarp" },
    { "stalwart",           "Stalwart" },
    { "gsmtp",              "Gmail" },
    { "gimap",              "Gmail" },
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

-- Drops the "220 " prefix and the leading hostname so a hostname can't be mistaken for a product.
local function smtp_text(line)
    local t = line:gsub("^220[ %-]%s*", "")
    local first = t:match("^(%S+)")
    if first and first:find(".", 1, true) then
        t = t:sub(#first + 1)
    end
    return t
end

---@type Signature
return {
    name = "smtp",
    version = "1.1.0",
    description = "SMTP protocol signature",
    signature = signature_,
    ports = { { 25, SOCK_STREAM }, { 587, SOCK_STREAM }, { 2525, SOCK_STREAM }, },
    check = function(fd, host, port)
        explo.info("Checking SMTP signature for host: " .. host .. " on port: " .. port)
        local line = first_line(explo.read_block(fd, 1024))
        if line and not line:match("^220[ %-]") then
            line = nil
        end

        -- An FTP server also greets with 220; EHLO -> 250 confirms SMTP.
        explo.write(fd, "EHLO " .. CLIENT_NAME .. "\r\n")
        local resp = explo.read_block(fd, 2048)
        local ehlo_ok = resp and resp:match("^250[ %-]") ~= nil
        local looks_smtp = line and line:lower():find("smtp", 1, true) ~= nil

        if ehlo_ok or looks_smtp then
            explo.write(fd, "QUIT\r\n")
            return identify(line and smtp_text(line)) or "SMTP"
        end
        return nil
    end,
}
