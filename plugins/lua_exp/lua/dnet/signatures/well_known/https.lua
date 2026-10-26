local signature_ = "wk.https"

local templates = {
    "GET / HTTP/1.1\r\n\r\n",
    "GET / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "HEAD / HTTP/1.1\r\n\r\n",
    "HEAD / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "OPTIONS / HTTP/1.1\r\n\r\n",
    "OPTIONS / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "TRACE / HTTP/1.1\r\n\r\n",
    "TRACE / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "POST / HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
    "POST / HTTP/1.0\r\nHost: %s\r\nContent-Length: 0\r\n\r\n",
    "PUT / HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
    "PUT / HTTP/1.0\r\nHost: %s\r\nContent-Length: 0\r\n\r\n",
    "DELETE / HTTP/1.1\r\n\r\n",
    "DELETE / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "CONNECT / HTTP/1.1\r\n\r\n",
    "CONNECT / HTTP/1.0\r\nHost: %s\r\n\r\n",
    "PATCH / HTTP/1.1\r\nContent-Length: 0\r\n\r\n",
    "PATCH / HTTP/1.0\r\nHost: %s\r\nContent-Length: 0\r\n\r\n",
}

---@type Signature
return {
    name = "https",
    version = "1.0.0",
    description = "HTTPS protocol signature",
    signature = signature_,
    ports = { { 443, SOCK_STREAM }, { 8443, SOCK_STREAM }, },
    check = function(fd, host, port)
        explo.info("Checking HTTP signature for host: " .. host .. " on port: " .. port)
        explo.close(fd)
        for i, template in ipairs(templates) do
            explo.info("Sending HTTPS request template " .. i .. " to host: " .. host .. " on port: " .. port)
            local tlsClient = explo.sconnect(host, port)
            if not tlsClient then
                explo.error("Failed to upgrade connection to TLS for host: " .. host .. " on port: " .. port ..
                    ". Aborting signature check.")
                return nil
            end
            explo.info("Sending HTTP request template " .. i .. " to host: " .. host .. " on port: " .. port)
            local request = string.format(template, host)
            explo.swrite(tlsClient, request)
            local response = explo.sread(tlsClient, 1024)
            if response then
                local server_version = string.match(response, "Server: ([^\r\n]+)")
                if server_version then
                    explo.info("Opened a new connection for this request. Closing fd.")
                    if not explo.sclose(tlsClient) then
                        explo.error(
                            "Failed to close socket after opening a new connection. Aborting signature check.")
                    end
                    return server_version
                end
            else
                explo.warn("No response received from host: " .. host .. " on port: " .. port)
            end
            if not explo.sclose(tlsClient) then
                explo.error("Failed to close socket after signature check. Aborting signature check.")
            end
        end
        return nil
    end,
}

-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
