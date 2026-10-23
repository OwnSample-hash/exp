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
        local opened_new_connection = false
        for i, template in ipairs(templates) do
            explo.info("Sending HTTPS request template " .. i .. " to host: " .. host .. " on port: " .. port)
            local tlsClient = explo.supgrade(fd, true)
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
                local connection_header = string.match(response, "Connection: ([^\r\n]+)")
                if server_version then
                    if opened_new_connection then
                        explo.info("Opened a new connection for this request. Closing fd.")
                        if not explo.sclose(tlsClient) then
                            explo.error(
                                "Failed to close socket after opening a new connection. Aborting signature check.")
                        end
                    end
                    return server_version
                end
                if connection_header then
                    opened_new_connection = true
                    if connection_header:lower() == "close" then
                        explo.info("Connection header indicates the server will close the connection. Closing fd.")
                        explo.sclose(tlsClient)
                        fd = explo.socket(SOCK_STREAM)
                        if not fd then
                            explo.error(
                                "Failed to create new socket after closing connection. Aborting signature check.")
                            return nil
                        end
                        if not explo.connect(fd, host, port) then
                            explo.error("Failed to reconnect to host: " .. host .. " on port: " .. port ..
                                " after closing connection. Aborting signature check.")
                            if opened_new_connection then
                                if not explo.close(fd) then
                                    explo.error(
                                        "Failed to close socket after failed reconnection. Aborting signature check.")
                                end
                            end
                            return nil
                        else
                            explo.info("Reconnected to host: " .. host .. " on port: " .. port ..
                                " after closing connection.")
                        end
                    else
                        explo.info(
                            "Connection header indicates the server will keep the connection alive. Continuing with the same fd.")
                    end
                end
            else
                explo.warn("No response received from host: " .. host .. " on port: " .. port)
            end
        end
        if opened_new_connection then
            if not explo.close(fd) then
                explo.error("Failed to close socket after signature check. Aborting signature check.")
            end
        end
        return nil
    end,
}

-- Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
