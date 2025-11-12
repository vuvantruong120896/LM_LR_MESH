#include "cellular_http_client.h"
#include <esp_log.h>

const char* CellularHTTPClient::TAG = "CELLULAR_HTTP";

CellularHTTPClient::CellularHTTPClient(CellularTCPClient* tcpClient)
    : m_tcpClient(tcpClient)
    , m_userAgent("ESP32-A7682S/1.0")
    , m_defaultTimeout(30000)
    , m_keepAlive(false)
    , m_currentSocket(-1)
    , m_currentPort(0)
{
    memset(&m_stats, 0, sizeof(Stats));
    ESP_LOGI(TAG, "HTTP client initialized");
}

CellularHTTPClient::~CellularHTTPClient() {
    closeConnection();
}

// ===== Public API =====

CellularHTTPClient::HTTPResponse CellularHTTPClient::get(
    const String& url,
    const std::map<String, String>& headers,
    uint32_t timeout)
{
    HTTPRequest req;
    req.method = HTTPMethod::GET;
    req.url = url;
    req.headers = headers;
    req.timeout = timeout;
    return request(req);
}

CellularHTTPClient::HTTPResponse CellularHTTPClient::post(
    const String& url,
    const String& body,
    const std::map<String, String>& headers,
    uint32_t timeout)
{
    HTTPRequest req;
    req.method = HTTPMethod::POST;
    req.url = url;
    req.body = body;
    req.headers = headers;
    req.timeout = timeout;
    return request(req);
}

CellularHTTPClient::HTTPResponse CellularHTTPClient::put(
    const String& url,
    const String& body,
    const std::map<String, String>& headers,
    uint32_t timeout)
{
    HTTPRequest req;
    req.method = HTTPMethod::PUT;
    req.url = url;
    req.body = body;
    req.headers = headers;
    req.timeout = timeout;
    return request(req);
}

CellularHTTPClient::HTTPResponse CellularHTTPClient::deleteRequest(
    const String& url,
    const std::map<String, String>& headers,
    uint32_t timeout)
{
    HTTPRequest req;
    req.method = HTTPMethod::DELETE;
    req.url = url;
    req.headers = headers;
    req.timeout = timeout;
    return request(req);
}

CellularHTTPClient::HTTPResponse CellularHTTPClient::request(const HTTPRequest& request) {
    HTTPResponse response;
    uint32_t startTime = millis();
    
    m_stats.totalRequests++;

    // Parse URL
    String protocol, host, path;
    uint16_t port;
    
    if (!parseURL(request.url, protocol, host, port, path)) {
        ESP_LOGE(TAG, "Failed to parse URL: %s", request.url.c_str());
        response.errorMessage = "Invalid URL";
        m_stats.failedRequests++;
        return response;
    }

    ESP_LOGI(TAG, "%s %s", methodToString(request.method), request.url.c_str());
    ESP_LOGD(TAG, "  Host: %s:%u", host.c_str(), port);
    ESP_LOGD(TAG, "  Path: %s", path.c_str());

    // Get connection
    int socket = getConnection(host, port);
    if (socket < 0) {
        ESP_LOGE(TAG, "Failed to connect to %s:%u", host.c_str(), port);
        response.errorMessage = "Connection failed";
        m_stats.failedRequests++;
        return response;
    }

    // Build request
    String requestStr = buildRequest(request.method, path, host, request.headers, request.body);
    
    ESP_LOGD(TAG, "Request (%d bytes):", requestStr.length());
    ESP_LOGD(TAG, "%s", requestStr.c_str());

    // Send request
    int sent = m_tcpClient->send(socket, requestStr);
    if (sent <= 0) {
        ESP_LOGE(TAG, "Failed to send request");
        response.errorMessage = "Send failed";
        m_stats.failedRequests++;
        if (!m_keepAlive) {
            closeConnection();
        }
        return response;
    }

    m_stats.totalBytesSent += sent;

    // Receive response
    String responseData = "";
    uint32_t receiveStart = millis();
    bool headerReceived = false;
    size_t expectedLength = 0;
    uint32_t lastDataTime = millis();
    int emptyReads = 0;
    
    ESP_LOGI(TAG, "Waiting for HTTP response (timeout: %ums)...", request.timeout);
    
    while (millis() - receiveStart < request.timeout) {
        // Check if module reports no more data available
        uint16_t availableData = m_tcpClient->available(socket);
        
        // Try to read data (even if availableData == 0, first time might have data)
        uint8_t buffer[1024];
        int received = m_tcpClient->receive(socket, buffer, sizeof(buffer));
        
        if (received > 0) {
            lastDataTime = millis();
            emptyReads = 0;
            
            // Append to response (handle binary data properly)
            for (int i = 0; i < received; i++) {
                responseData += (char)buffer[i];
            }
            m_stats.totalBytesReceived += received;
            
            ESP_LOGD(TAG, "Received %d bytes (total: %d)", received, responseData.length());
            
            // Check if we have full headers
            if (!headerReceived && responseData.indexOf("\r\n\r\n") >= 0) {
                headerReceived = true;
                
                // Parse headers to get content length
                std::map<String, String> tempHeaders;
                int bodyStart;
                if (parseHeaders(responseData, tempHeaders, bodyStart)) {
                    if (tempHeaders.find("Content-Length") != tempHeaders.end()) {
                        expectedLength = tempHeaders["Content-Length"].toInt();
                        size_t currentBodyLength = responseData.length() - bodyStart;
                        
                        ESP_LOGI(TAG, "Content-Length: %u, received body: %u", 
                                expectedLength, currentBodyLength);
                        
                        if (currentBodyLength >= expectedLength) {
                            ESP_LOGI(TAG, "✅ Complete response received");
                            break;  // Got complete response
                        }
                    } else if (tempHeaders.find("Transfer-Encoding") != tempHeaders.end() &&
                              tempHeaders["Transfer-Encoding"] == "chunked") {
                        ESP_LOGI(TAG, "Chunked encoding detected");
                        // For chunked, wait for "0\r\n\r\n" terminator
                        if (responseData.endsWith("0\r\n\r\n")) {
                            ESP_LOGI(TAG, "✅ Complete chunked response received");
                            break;
                        }
                    } else {
                        // No Content-Length, check for Connection: close
                        if (tempHeaders.find("Connection") != tempHeaders.end() &&
                            tempHeaders["Connection"].indexOf("close") >= 0) {
                            ESP_LOGD(TAG, "Connection: close - waiting for socket close");
                        }
                    }
                }
            } else if (headerReceived && expectedLength > 0) {
                // Headers already parsed, check if we now have complete body
                std::map<String, String> tempHeaders;
                int bodyStart;
                if (parseHeaders(responseData, tempHeaders, bodyStart)) {
                    size_t currentBodyLength = responseData.length() - bodyStart;
                    ESP_LOGD(TAG, "Body progress: %u/%u bytes", currentBodyLength, expectedLength);
                    
                    if (currentBodyLength >= expectedLength) {
                        ESP_LOGI(TAG, "✅ Complete response received (body complete)");
                        break;
                    }
                }
            }
            
            // Data received, continue immediately (don't delay)
            continue;
        } else if (received == 0) {
            // No data received
            
            // CRITICAL: If module reports availableData == 0, stop immediately
            // This prevents endless polling when response is complete
            if (availableData == 0) {
                if (responseData.length() > 0) {
                    // We got some data, assume complete
                    ESP_LOGI(TAG, "✅ Module reports no more data (received %u bytes total)", 
                             responseData.length());
                    break;
                } else if (emptyReads >= 5) {
                    // No data at all after multiple tries
                    ESP_LOGW(TAG, "No response data received after %d attempts", emptyReads);
                    break;
                }
            }
            
            // No data available, increment counter
            emptyReads++;
            
            // If we have complete headers and body based on Content-Length, we're done
            if (headerReceived && expectedLength > 0) {
                std::map<String, String> tempHeaders;
                int bodyStart;
                if (parseHeaders(responseData, tempHeaders, bodyStart)) {
                    size_t currentBodyLength = responseData.length() - bodyStart;
                    if (currentBodyLength >= expectedLength) {
                        ESP_LOGI(TAG, "✅ Complete response received (verified)");
                        break;
                    }
                }
            }
            
            // If we have headers and haven't received data for a while, assume complete
            if (headerReceived && emptyReads > 5) {  // 500ms of no data
                ESP_LOGI(TAG, "✅ No more data, response complete");
                break;
            }
        } else {
            // Error reading
            ESP_LOGW(TAG, "receive() returned error: %d", received);
            break;
        }
        
        // Check socket state
        auto state = m_tcpClient->getSocketState(socket);
        if (state == CellularTCPClient::SocketState::CLOSED) {
            ESP_LOGI(TAG, "Socket closed by remote");
            if (responseData.length() > 0) {
                break;  // Have data, stop
            }
        }
        
        delay(100);  // Poll interval
    }

    if (responseData.length() == 0) {
        ESP_LOGE(TAG, "No response received");
        response.errorMessage = "No response";
        m_stats.failedRequests++;
        if (!m_keepAlive) {
            closeConnection();
        }
        return response;
    }

    ESP_LOGD(TAG, "Response received (%d bytes)", responseData.length());

    // Parse response
    if (!parseResponse(responseData, response)) {
        ESP_LOGE(TAG, "Failed to parse response");
        response.errorMessage = "Parse failed";
        m_stats.failedRequests++;
        if (!m_keepAlive) {
            closeConnection();
        }
        return response;
    }

    // Calculate response time
    response.responseTime = millis() - startTime;
    response.success = isSuccessStatus(response.statusCode);

    // Update statistics
    if (response.success) {
        m_stats.successfulRequests++;
    } else {
        m_stats.failedRequests++;
    }

    // Update average response time
    if (m_stats.successfulRequests > 0) {
        m_stats.avgResponseTime = 
            (m_stats.avgResponseTime * (m_stats.successfulRequests - 1) + response.responseTime) 
            / m_stats.successfulRequests;
    }

    ESP_LOGI(TAG, "✅ HTTP %d %s (%ums)", 
             response.statusCode, response.statusMessage.c_str(), response.responseTime);

    // Handle connection persistence
    if (!m_keepAlive || response.headers["Connection"] == "close") {
        closeConnection();
    }

    return response;
}

void CellularHTTPClient::setUserAgent(const String& userAgent) {
    m_userAgent = userAgent;
}

void CellularHTTPClient::setDefaultTimeout(uint32_t timeout) {
    m_defaultTimeout = timeout;
}

void CellularHTTPClient::setKeepAlive(bool enable) {
    m_keepAlive = enable;
    if (!enable) {
        closeConnection();
    }
}

void CellularHTTPClient::resetStats() {
    memset(&m_stats, 0, sizeof(Stats));
}

void CellularHTTPClient::closeConnection() {
    if (m_currentSocket >= 0) {
        m_tcpClient->disconnect(m_currentSocket);
        m_currentSocket = -1;
        m_currentHost = "";
        m_currentPort = 0;
    }
}

// ===== Private Helper Methods =====

bool CellularHTTPClient::parseURL(const String& url, String& protocol, 
                                   String& host, uint16_t& port, String& path) {
    // Format: http://host:port/path or http://host/path
    int protocolEnd = url.indexOf("://");
    if (protocolEnd < 0) {
        return false;
    }

    protocol = url.substring(0, protocolEnd);
    protocol.toLowerCase();

    int hostStart = protocolEnd + 3;
    int pathStart = url.indexOf('/', hostStart);
    
    if (pathStart < 0) {
        pathStart = url.length();
        path = "/";
    } else {
        path = url.substring(pathStart);
    }

    String hostPort = url.substring(hostStart, pathStart);
    int portStart = hostPort.indexOf(':');
    
    if (portStart >= 0) {
        host = hostPort.substring(0, portStart);
        port = hostPort.substring(portStart + 1).toInt();
    } else {
        host = hostPort;
        port = (protocol == "https") ? HTTPS_DEFAULT_PORT : HTTP_DEFAULT_PORT;
    }

    return true;
}

String CellularHTTPClient::buildRequest(HTTPMethod method, const String& path,
                                         const String& host,
                                         const std::map<String, String>& headers,
                                         const String& body) {
    String request = "";
    
    // Request line
    request += methodToString(method);
    request += " ";
    request += path;
    request += " HTTP/1.1\r\n";
    
    // Required headers
    request += "Host: " + host + "\r\n";
    request += "User-Agent: " + m_userAgent + "\r\n";
    
    // Content-Length for requests with body
    if (body.length() > 0) {
        request += "Content-Length: " + String(body.length()) + "\r\n";
    }
    
    // Connection header
    if (m_keepAlive) {
        request += "Connection: keep-alive\r\n";
    } else {
        request += "Connection: close\r\n";
    }
    
    // Custom headers
    for (const auto& header : headers) {
        request += header.first + ": " + header.second + "\r\n";
    }
    
    // End of headers
    request += "\r\n";
    
    // Body
    if (body.length() > 0) {
        request += body;
    }
    
    return request;
}

bool CellularHTTPClient::parseResponse(const String& data, HTTPResponse& response) {
    // Find end of headers - try multiple separators
    int headerEnd = data.indexOf("\r\n\r\n");
    int separatorLen = 4;
    
    if (headerEnd < 0) {
        // Try \n\n
        headerEnd = data.indexOf("\n\n");
        separatorLen = 2;
    }
    
    if (headerEnd < 0) {
        // Try finding body start with < (HTML/XML)
        headerEnd = data.indexOf('<');
        if (headerEnd > 0) {
            separatorLen = 0;
        }
    }
    
    if (headerEnd < 0) {
        ESP_LOGW(TAG, "Could not find header/body separator");
        return false;
    }

    String headerSection = data.substring(0, headerEnd);
    String bodySection = data.substring(headerEnd + separatorLen);

    // Parse status line
    int firstLineEnd = headerSection.indexOf("\r\n");
    if (firstLineEnd < 0) {
        firstLineEnd = headerSection.indexOf("\n");
    }
    if (firstLineEnd < 0) {
        ESP_LOGE(TAG, "Cannot find end of status line");
        return false;
    }

    String statusLine = headerSection.substring(0, firstLineEnd);
    if (!parseStatusLine(statusLine, response.statusCode, response.statusMessage)) {
        return false;
    }

    // Parse headers
    int bodyStart;
    if (!parseHeaders(data, response.headers, bodyStart)) {
        return false;
    }

    // Get content length
    if (response.headers.find("Content-Length") != response.headers.end()) {
        response.contentLength = response.headers["Content-Length"].toInt();
    }

    // Check for chunked encoding
    if (response.headers.find("Transfer-Encoding") != response.headers.end() &&
        response.headers["Transfer-Encoding"] == "chunked") {
        response.chunked = true;
        
        // Decode chunked data
        String decodedBody;
        if (decodeChunked(bodySection, decodedBody)) {
            response.body = decodedBody;
        } else {
            ESP_LOGW(TAG, "Failed to decode chunked data, using raw");
            response.body = bodySection;
        }
    } else {
        response.body = bodySection;
    }

    return true;
}

bool CellularHTTPClient::parseStatusLine(const String& line, int& statusCode, 
                                          String& statusMessage) {
    // Format: HTTP/1.1 200 OK
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) {
        return false;
    }

    int secondSpace = line.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
        return false;
    }

    String codeStr = line.substring(firstSpace + 1, secondSpace);
    statusCode = codeStr.toInt();
    statusMessage = line.substring(secondSpace + 1);
    statusMessage.trim();

    return statusCode > 0;
}

bool CellularHTTPClient::parseHeaders(const String& data, 
                                       std::map<String, String>& headers,
                                       int& bodyStart) {
    // Find header/body separator - try multiple formats
    bodyStart = data.indexOf("\r\n\r\n");
    int separatorLen = 4;
    
    if (bodyStart < 0) {
        bodyStart = data.indexOf("\n\n");
        separatorLen = 2;
    }
    
    if (bodyStart < 0) {
        // Try finding body start with < (HTML/XML)
        bodyStart = data.indexOf('<');
        if (bodyStart > 0) {
            separatorLen = 0;
        }
    }
    
    if (bodyStart < 0) {
        ESP_LOGW(TAG, "parseHeaders: Cannot find header/body separator");
        return false;
    }

    String headerSection = data.substring(0, bodyStart);
    bodyStart += separatorLen;

    // Skip status line
    int lineStart = headerSection.indexOf("\r\n");
    if (lineStart < 0) {
        lineStart = headerSection.indexOf("\n");
    }
    if (lineStart < 0) {
        ESP_LOGE(TAG, "parseHeaders: Cannot find end of status line");
        return false;
    }
    lineStart += (headerSection.indexOf("\r\n") >= 0) ? 2 : 1;  // Skip \r\n or \n

    // Parse each header
    while (lineStart < headerSection.length()) {
        int lineEnd = headerSection.indexOf("\r\n", lineStart);
        int skipLen = 2;
        if (lineEnd < 0) {
            lineEnd = headerSection.indexOf("\n", lineStart);
            skipLen = 1;
        }
        if (lineEnd < 0) {
            lineEnd = headerSection.length();
            skipLen = 0;
        }

        String line = headerSection.substring(lineStart, lineEnd);
        int colonPos = line.indexOf(':');
        
        if (colonPos > 0) {
            String name = line.substring(0, colonPos);
            String value = line.substring(colonPos + 1);
            name.trim();
            value.trim();
            headers[name] = value;
        }

        lineStart = lineEnd + skipLen;
    }

    return true;
}

bool CellularHTTPClient::decodeChunked(const String& chunkedData, String& decodedData) {
    decodedData = "";
    int pos = 0;

    while (pos < chunkedData.length()) {
        // Read chunk size (hex)
        int crlfPos = chunkedData.indexOf("\r\n", pos);
        if (crlfPos < 0) {
            break;
        }

        String sizeStr = chunkedData.substring(pos, crlfPos);
        sizeStr.trim();
        
        // Convert hex to int
        int chunkSize = (int)strtol(sizeStr.c_str(), NULL, 16);
        
        if (chunkSize == 0) {
            // Last chunk
            break;
        }

        // Read chunk data
        int dataStart = crlfPos + 2;
        if (dataStart + chunkSize > chunkedData.length()) {
            ESP_LOGW(TAG, "Incomplete chunk: expected %d bytes", chunkSize);
            break;
        }

        decodedData += chunkedData.substring(dataStart, dataStart + chunkSize);
        
        // Move to next chunk (skip trailing \r\n)
        pos = dataStart + chunkSize + 2;
    }

    return decodedData.length() > 0;
}

int CellularHTTPClient::getConnection(const String& host, uint16_t port) {
    // Check if we can reuse existing connection
    if (m_keepAlive && m_currentSocket >= 0 && 
        m_currentHost == host && m_currentPort == port) {
        auto state = m_tcpClient->getSocketState(m_currentSocket);
        if (state == CellularTCPClient::SocketState::CONNECTED) {
            ESP_LOGD(TAG, "Reusing connection to %s:%u", host.c_str(), port);
            return m_currentSocket;
        }
    }

    // Close old connection
    closeConnection();

    // Create new connection
    ESP_LOGI(TAG, "Connecting to %s:%u...", host.c_str(), port);
    m_currentSocket = m_tcpClient->connect(host, port);
    
    if (m_currentSocket >= 0) {
        m_currentHost = host;
        m_currentPort = port;
    }

    return m_currentSocket;
}

const char* CellularHTTPClient::methodToString(HTTPMethod method) {
    switch (method) {
        case HTTPMethod::GET:    return "GET";
        case HTTPMethod::POST:   return "POST";
        case HTTPMethod::PUT:    return "PUT";
        case HTTPMethod::DELETE: return "DELETE";
        case HTTPMethod::PATCH:  return "PATCH";
        case HTTPMethod::HEAD:   return "HEAD";
        default:                 return "UNKNOWN";
    }
}
