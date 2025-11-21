/**
 * @file cellular_tcp_client.cpp
 * @brief TCP/IP Client Implementation for A7682S
 */

#include "cellular_tcp_client.h"
#include <esp_log.h>

const char* CellularTCPClient::TAG = "CELLULAR_TCP";

CellularTCPClient::CellularTCPClient(ATCommandHandler* atHandler)
    : m_atHandler(atHandler)
    , m_eventCallback(nullptr)
    , m_lastError(ConnectError::NONE)
{
    // Initialize all sockets to CLOSED state
    for (uint8_t i = 0; i < MAX_SOCKETS; i++) {
        m_sockets[i] = SocketInfo();
    }

    // Initialize statistics
    memset(&m_stats, 0, sizeof(Stats));

    // Initialize DNS state
    m_dnsState.pending = false;
    m_dnsState.success = false;

    // Register URC handler
    if (m_atHandler) {
        m_atHandler->registerURCCallback([this](const String& urc) {
            this->handleURC(urc);
        });
    }

    // Enable manual receive mode (required for AT+CIPRXGET=2 to work)
    ESP_LOGI(TAG, "Enabling manual TCP receive mode...");
    auto resp = m_atHandler->sendCommand("+CIPRXGET=1", 1000);
    if (resp.success) {
        ESP_LOGI(TAG, "✅ Manual receive mode enabled");
    } else {
        ESP_LOGW(TAG, "Failed to enable manual receive mode: %s", resp.errorMessage.c_str());
    }

    ESP_LOGI(TAG, "TCP client initialized (max %d sockets)", MAX_SOCKETS);
}

CellularTCPClient::~CellularTCPClient() {
    disconnectAll();
}

// ===== Connection Management =====

int CellularTCPClient::connect(const String& host, uint16_t port, uint32_t timeoutMs) {
    ESP_LOGI(TAG, "Connecting to %s:%u (timeout: %ums)", host.c_str(), port, timeoutMs);

    // Validate parameters
    if (host.length() == 0 || port == 0) {
        ESP_LOGE(TAG, "Invalid host or port");
        m_lastError = ConnectError::INVALID_PARAMETERS;
        return -1;
    }

    // Find available socket
    int linkNum = findAvailableSocket();
    if (linkNum < 0) {
        ESP_LOGE(TAG, "No available socket");
        m_lastError = ConnectError::SOCKET_NOT_AVAILABLE;
        return -1;
    }

    // Resolve hostname to IP if needed
    String ipAddress = host;
    if (!isValidIP(host)) {
        ESP_LOGI(TAG, "Resolving DNS for %s...", host.c_str());
        m_sockets[linkNum].state = SocketState::RESOLVING_DNS;
        
        if (!resolveHost(host, ipAddress, DEFAULT_DNS_TIMEOUT)) {
            ESP_LOGE(TAG, "DNS resolution failed for %s", host.c_str());
            m_lastError = ConnectError::DNS_FAILED;
            resetSocket(linkNum);
            return -1;
        }
        
        ESP_LOGI(TAG, "✅ DNS resolved: %s -> %s", host.c_str(), ipAddress.c_str());
    }

    // Update socket info
    m_sockets[linkNum].state = SocketState::OPENING;
    m_sockets[linkNum].remoteHost = host;
    m_sockets[linkNum].remoteIP = ipAddress;
    m_sockets[linkNum].remotePort = port;

    // Open TCP connection: AT+CIPOPEN=<link_num>,"TCP","<ip>",<port>
    String cmd = "+CIPOPEN=" + String(linkNum) + ",\"TCP\",\"" + ipAddress + "\"," + String(port);
    auto resp = m_atHandler->sendCommand(cmd, 5000);

    if (!resp.success) {
        ESP_LOGE(TAG, "AT+CIPOPEN failed: %s", resp.data.c_str());
        m_lastError = ConnectError::MODULE_ERROR;
        resetSocket(linkNum);
        m_stats.failedConnections++;
        return -1;
    }

    // Wait for +CIPOPEN URC via callback (no manual processURCs needed)
    ESP_LOGI(TAG, "Waiting for +CIPOPEN URC (timeout: %ums)...", timeoutMs);
    uint32_t startTime = millis();
    
    while (millis() - startTime < timeoutMs) {
        // URC callback will automatically update m_sockets[linkNum].state
        // No need to call processURCs() - it runs asynchronously
        delay(10);
        
        // Check socket state (updated by URC callback)
        if (m_sockets[linkNum].state == SocketState::CONNECTED) {
            m_sockets[linkNum].connectTime = millis();
            m_sockets[linkNum].lastActivityTime = millis();
            m_stats.totalConnections++;
            
            ESP_LOGI(TAG, "✅ Connected to %s:%u on socket %d (%.1fs)",
                     host.c_str(), port, linkNum, (millis() - startTime) / 1000.0f);
            
            m_lastError = ConnectError::NONE;
            return linkNum;
        } else if (m_sockets[linkNum].state == SocketState::CLOSED) {
            // Connection failed (URC indicated failure)
            ESP_LOGE(TAG, "Connection failed");
            m_lastError = ConnectError::CONNECTION_REFUSED;
            m_stats.failedConnections++;
            return -1;
        }
        
        delay(100);
    }

    // Timeout
    ESP_LOGE(TAG, "Connection timeout");
    m_lastError = ConnectError::CONNECTION_TIMEOUT;
    resetSocket(linkNum);
    m_stats.failedConnections++;
    return -1;
}

bool CellularTCPClient::disconnect(uint8_t linkNum) {
    if (linkNum >= MAX_SOCKETS) {
        return false;
    }

    if (m_sockets[linkNum].state == SocketState::CLOSED) {
        ESP_LOGD(TAG, "Socket %d already closed", linkNum);
        return true;
    }

    ESP_LOGI(TAG, "Closing socket %d...", linkNum);
    m_sockets[linkNum].state = SocketState::CLOSING;

    // AT+CIPCLOSE=<link_num>
    String cmd = "+CIPCLOSE=" + String(linkNum);
    auto resp = m_atHandler->sendCommand(cmd, 5000);

    // Check for success or already closed
    if (resp.success || resp.data.indexOf("CLOSE OK") >= 0) {
        ESP_LOGI(TAG, "Socket %d closed", linkNum);
        resetSocket(linkNum);
        return true;
    }
    
    // Error 4 = "Already closed" - это нормально
    if (resp.data.indexOf("+CIPCLOSE: " + String(linkNum) + ",4") >= 0) {
        ESP_LOGD(TAG, "Socket %d already closed by remote", linkNum);
        resetSocket(linkNum);
        return true;
    }

    // Force reset even if command failed
    ESP_LOGW(TAG, "Socket %d close failed: %s", linkNum, resp.errorMessage.c_str());
    resetSocket(linkNum);
    return false;
}

void CellularTCPClient::disconnectAll() {
    ESP_LOGI(TAG, "Closing all sockets...");
    
    for (uint8_t i = 0; i < MAX_SOCKETS; i++) {
        if (m_sockets[i].state != SocketState::CLOSED) {
            disconnect(i);
        }
    }
}

bool CellularTCPClient::connected(uint8_t linkNum) const {
    if (linkNum >= MAX_SOCKETS) {
        return false;
    }
    return m_sockets[linkNum].state == SocketState::CONNECTED;
}

// ===== Data Transfer =====

int CellularTCPClient::send(uint8_t linkNum, const uint8_t* data, size_t length) {
    if (linkNum >= MAX_SOCKETS) {
        ESP_LOGE(TAG, "Invalid socket number: %d", linkNum);
        return -1;
    }

    if (m_sockets[linkNum].state != SocketState::CONNECTED) {
        ESP_LOGE(TAG, "Socket %d not connected", linkNum);
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    ESP_LOGD(TAG, "Sending %u bytes on socket %d...", length, linkNum);

    // AT+CIPSEND=<link_num>,<length>
    // This command returns '>' prompt instead of OK
    String cmd = "+CIPSEND=" + String(linkNum) + "," + String(length);
    auto resp = m_atHandler->sendCommand(cmd, 5000, false);  // Don't expect OK

    // Check for '>' prompt
    if (resp.data.indexOf('>') < 0) {
        ESP_LOGE(TAG, "Failed to get '>' prompt: %s", resp.data.c_str());
        return -1;
    }

    ESP_LOGD(TAG, "Got '>' prompt, sending raw data...");

    // Send raw data
    size_t written = m_atHandler->getUART()->write(data, length);
    
    if (written != length) {
        ESP_LOGE(TAG, "Failed to send all data (%u/%u)", written, length);
        return -1;
    }

    // Wait for confirmation: OK or +CIPSEND: or SEND OK
    delay(50);  // Give module time to process
    
    // Read response
    char buffer[256];
    uint32_t startTime = millis();
    bool sendConfirmed = false;
    
    while (millis() - startTime < 5000) {
        size_t len = m_atHandler->getUART()->readLine(buffer, sizeof(buffer), 100);
        if (len > 0) {
            String response = String(buffer);
            response.trim();
            
            if (response.length() > 0) {
                ESP_LOGD(TAG, "Send response: %s", response.c_str());
            }
            
            // Check for success indicators
            if (response == "OK" || 
                response.indexOf("SEND OK") >= 0 || 
                response.startsWith("+CIPSEND:")) {
                sendConfirmed = true;
                // Don't break yet - let buffer drain
            } else if (response.indexOf("SEND FAIL") >= 0 || 
                       response.indexOf("ERROR") >= 0) {
                ESP_LOGE(TAG, "Send failed: %s", response.c_str());
                return -1;
            }
            
            // If we got confirmation and now receiving data or close, we're done
            if (sendConfirmed && (response.startsWith("+IPD") || 
                                  response.startsWith("RECV FROM") ||
                                  response.startsWith("+IPCLOSE"))) {
                break;
            }
        }
        delay(10);
    }
    
    if (!sendConfirmed) {
        ESP_LOGW(TAG, "Send timeout (no confirmation)");
        return -1;
    }

    // Update statistics
    m_sockets[linkNum].bytesSent += length;
    m_sockets[linkNum].lastActivityTime = millis();
    m_stats.totalBytesSent += length;
    ESP_LOGI(TAG, "✅ Sent %u bytes on socket %d", length, linkNum);
    return length;
}

int CellularTCPClient::send(uint8_t linkNum, const String& data) {
    return send(linkNum, (const uint8_t*)data.c_str(), data.length());
}

int CellularTCPClient::receive(uint8_t linkNum, uint8_t* buffer, size_t maxLength) {
    if (linkNum >= MAX_SOCKETS) {
        ESP_LOGE(TAG, "Invalid socket number: %d", linkNum);
        return -1;
    }

    if (m_sockets[linkNum].state != SocketState::CONNECTED) {
        ESP_LOGE(TAG, "Socket %d not connected", linkNum);
        return -1;
    }

    // A7682S doesn't send +CIPRCV URC reliably, so always try to read
    // even if availableData == 0
    uint16_t toRead = maxLength;
    
    ESP_LOGD(TAG, "Trying to read up to %u bytes from socket %d...", toRead, linkNum);

    // AT+CIPRXGET=2,<link_num>,<length>
    // Must enable manual mode first with AT+CIPRXGET=1
    String cmd = "+CIPRXGET=2," + String(linkNum) + "," + String(toRead);
    auto resp = m_atHandler->sendCommand(cmd, 5000);

    if (!resp.success) {
        // If error contains "no data", it's normal (no data available)
        if (resp.data.indexOf("no data") >= 0 || resp.errorMessage.indexOf("no data") >= 0) {
            return 0;  // No data available
        }
        ESP_LOGD(TAG, "AT+CIPRXGET=2 returned: %s", resp.errorMessage.c_str());
        return 0;  // Treat as no data instead of error
    }

    // Parse response: +CIPRXGET: 2,<link_num>,<length>,<remaining>\r\n<data>
    // Example: +CIPRXGET: 2,0,350,0\r\n<350 bytes of data>
    int dataStart = resp.data.indexOf('\n');
    if (dataStart < 0) {
        ESP_LOGW(TAG, "No data separator in CIPRXGET response");
        return 0;
    }
    dataStart++;  // Skip the \n

    // Extract length from header: +CIPRXGET: 2,<link>,<length_sent>,<length_remaining>
    int reportedLength = 0;
    int remainingData = 0;
    int firstComma = resp.data.indexOf(',');
    if (firstComma > 0) {
        int secondComma = resp.data.indexOf(',', firstComma + 1);
        int thirdComma = resp.data.indexOf(',', secondComma + 1);
        if (thirdComma > secondComma) {
            String lengthStr = resp.data.substring(secondComma + 1, thirdComma);
            reportedLength = lengthStr.toInt();
            
            // Get remaining data length
            int fourthComma = resp.data.indexOf(',', thirdComma + 1);
            if (fourthComma < 0) {
                fourthComma = resp.data.indexOf('\r', thirdComma + 1);
            }
            if (fourthComma > thirdComma) {
                String remainingStr = resp.data.substring(thirdComma + 1, fourthComma);
                remainingData = remainingStr.toInt();
            }
            
            ESP_LOGD(TAG, "CIPRXGET: %d bytes sent, %d bytes remaining in buffer", 
                     reportedLength, remainingData);
        }
    }

    // Extract data
    int actualLength = resp.data.length() - dataStart;
    if (actualLength > (int)maxLength) {
        actualLength = maxLength;
    }

    if (actualLength > 0) {
        memcpy(buffer, resp.data.c_str() + dataStart, actualLength);
        
        // Update counters - set available data to what module reports as remaining
        m_sockets[linkNum].availableData = remainingData;
        m_sockets[linkNum].bytesReceived += actualLength;
        m_sockets[linkNum].lastActivityTime = millis();
        m_stats.totalBytesReceived += actualLength;
        
        ESP_LOGD(TAG, "✅ Received %d bytes from socket %d (%d bytes still in buffer)", 
                 actualLength, linkNum, remainingData);
        return actualLength;
    }

    return 0;
}

uint16_t CellularTCPClient::available(uint8_t linkNum) const {
    if (linkNum >= MAX_SOCKETS) {
        return 0;
    }
    return m_sockets[linkNum].availableData;
}

bool CellularTCPClient::queryAvailableData(uint8_t linkNum) {
    // A7682S doesn't support AT+CIPRXGET=4 (operation not supported)
    // Instead, data arrives automatically and we must use AT+CIPRCV to read it
    // For now, just check if socket is still connected
    if (linkNum >= MAX_SOCKETS) {
        return false;
    }
    
    // Simply return true if socket is connected
    // Actual data reading happens via AT+CIPRCV in receive()
    return (m_sockets[linkNum].state == SocketState::CONNECTED);
}

int CellularTCPClient::peek(uint8_t linkNum) {
    if (linkNum >= MAX_SOCKETS || m_sockets[linkNum].availableData == 0) {
        return -1;
    }

    // Read 1 byte without consuming
    uint8_t byte;
    int result = receive(linkNum, &byte, 1);
    
    if (result == 1) {
        // Put it back (increment available count)
        m_sockets[linkNum].availableData++;
        return byte;
    }
    
    return -1;
}

void CellularTCPClient::flush(uint8_t linkNum) {
    if (linkNum >= MAX_SOCKETS) {
        return;
    }

    if (m_sockets[linkNum].availableData > 0) {
        ESP_LOGD(TAG, "Flushing %u bytes from socket %d", 
                 m_sockets[linkNum].availableData, linkNum);
        
        // Read and discard all available data
        uint8_t dummy[256];
        while (m_sockets[linkNum].availableData > 0) {
            receive(linkNum, dummy, sizeof(dummy));
        }
    }
}

// ===== DNS Resolution =====

bool CellularTCPClient::resolveHost(const String& hostname, String& resultIP, uint32_t timeoutMs) {
    ESP_LOGI(TAG, "Resolving %s...", hostname.c_str());
    m_stats.dnsQueries++;

    // Check if already an IP
    if (isValidIP(hostname)) {
        resultIP = hostname;
        return true;
    }

    // Initialize DNS state
    m_dnsState.pending = true;
    m_dnsState.hostname = hostname;
    m_dnsState.resultIP = "";
    m_dnsState.success = false;

    // AT+CDNSGIP=<hostname>
    String cmd = "+CDNSGIP=\"" + hostname + "\"";
    auto resp = m_atHandler->sendCommand(cmd, 5000);

    if (!resp.success) {
        ESP_LOGE(TAG, "AT+CDNSGIP failed");
        m_dnsState.pending = false;
        m_stats.dnsFailed++;
        return false;
    }

    // Parse URCs from response data
    // Response can contain: +CDNSGIP: 1,"host","ip1"\n+CDNSGIP: 2,"host","ip2"...
    if (resp.data.indexOf("+CDNSGIP:") >= 0) {
        // Split response by newlines
        int startIdx = 0;
        while (startIdx < resp.data.length()) {
            int endIdx = resp.data.indexOf('\n', startIdx);
            if (endIdx < 0) endIdx = resp.data.length();
            
            String line = resp.data.substring(startIdx, endIdx);
            line.trim();
            
            // Check if this line is a CDNSGIP URC
            if (line.startsWith("+CDNSGIP:")) {
                // Parse and handle it
                String value = ATCommandHandler::extractValue(line, "+CDNSGIP:");
                auto parts = ATCommandHandler::splitValues(value);
                
                if (parts.size() >= 3) {
                    int result = parts[0].toInt();
                    String host = parts[1];
                    host.replace("\"", "");
                    String ip = parts[2];
                    ip.replace("\"", "");
                    
                    // Accept first valid IP (result >= 1)
                    if (result >= 1 && host == hostname) {
                        resultIP = ip;
                        m_dnsState.pending = false;
                        m_dnsState.success = true;
                        m_dnsState.resultIP = ip;
                        ESP_LOGI(TAG, "✅ DNS: %s -> %s (from response)", hostname.c_str(), ip.c_str());
                        return true;
                    }
                }
            }
            
            startIdx = endIdx + 1;
        }
    }

    // Wait for +CDNSGIP URC (fallback if not in immediate response)
    // URC callback will automatically update m_dnsState
    uint32_t startTime = millis();
    
    while (millis() - startTime < timeoutMs) {
        // No need to call processURCs() - URC callback handles it asynchronously
        delay(100);
        
        if (!m_dnsState.pending) {
            if (m_dnsState.success) {
                resultIP = m_dnsState.resultIP;
                ESP_LOGI(TAG, "✅ DNS: %s -> %s", hostname.c_str(), resultIP.c_str());
                return true;
            } else {
                ESP_LOGE(TAG, "DNS resolution failed");
                m_stats.dnsFailed++;
                return false;
            }
        }
    }

    ESP_LOGE(TAG, "DNS timeout");
    m_dnsState.pending = false;
    m_stats.dnsFailed++;
    return false;
}

// ===== Status & Information =====

CellularTCPClient::SocketInfo CellularTCPClient::getSocketInfo(uint8_t linkNum) const {
    if (linkNum >= MAX_SOCKETS) {
        return SocketInfo();
    }
    return m_sockets[linkNum];
}

CellularTCPClient::SocketState CellularTCPClient::getSocketState(uint8_t linkNum) const {
    if (linkNum >= MAX_SOCKETS) {
        return SocketState::CLOSED;
    }
    return m_sockets[linkNum].state;
}

CellularTCPClient::Stats CellularTCPClient::getStats() const {
    return m_stats;
}

uint8_t CellularTCPClient::getActiveConnectionCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SOCKETS; i++) {
        if (m_sockets[i].state == SocketState::CONNECTED) {
            count++;
        }
    }
    return count;
}

int CellularTCPClient::findAvailableSocket() const {
    for (uint8_t i = 0; i < MAX_SOCKETS; i++) {
        if (m_sockets[i].state == SocketState::CLOSED) {
            return i;
        }
    }
    return -1;
}

// ===== Event Handling =====

void CellularTCPClient::onEvent(EventCallback callback) {
    m_eventCallback = callback;
}

void CellularTCPClient::update() {
    // URC callbacks already handle asynchronous processing
    // No manual processURCs() needed - events are handled via registered callbacks
}

CellularTCPClient::ConnectError CellularTCPClient::getLastError() const {
    return m_lastError;
}

// ===== Private Methods =====

void CellularTCPClient::handleURC(const String& urc) {
    if (urc.startsWith("+CIPOPEN:")) {
        // +CIPOPEN: <link_num>,<err>
        String value = ATCommandHandler::extractValue(urc, "+CIPOPEN:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            uint8_t linkNum = parts[0].toInt();
            int errorCode = parts[1].toInt();
            handleCIPOPEN(linkNum, errorCode);
        }
    } else if (urc.startsWith("+CIPCLOSE:")) {
        // +CIPCLOSE: <link_num>,<err>
        String value = ATCommandHandler::extractValue(urc, "+CIPCLOSE:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            uint8_t linkNum = parts[0].toInt();
            int errorCode = parts[1].toInt();
            handleCIPCLOSE(linkNum, errorCode);
        }
    } else if (urc.startsWith("+CIPRCV:")) {
        // +CIPRCV: <link_num>,<length>
        String value = ATCommandHandler::extractValue(urc, "+CIPRCV:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            uint8_t linkNum = parts[0].toInt();
            uint16_t length = parts[1].toInt();
            handleCIPRCV(linkNum, length);
        }
    } else if (urc.startsWith("+CDNSGIP:")) {
        // +CDNSGIP: <result>,"<hostname>","<ip>"
        String value = ATCommandHandler::extractValue(urc, "+CDNSGIP:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 3) {
            int result = parts[0].toInt();
            String hostname = parts[1];
            hostname.replace("\"", "");
            String ip = parts[2];
            ip.replace("\"", "");
            handleCDNSGIP(result, hostname, ip);
        }
    }
}

void CellularTCPClient::handleCIPOPEN(uint8_t linkNum, int errorCode) {
    if (linkNum >= MAX_SOCKETS) {
        return;
    }

    ESP_LOGI(TAG, "URC: +CIPOPEN: link=%d, err=%d", linkNum, errorCode);

    if (errorCode == 0) {
        // Success
        m_sockets[linkNum].state = SocketState::CONNECTED;
        triggerEvent(linkNum, Event::CONNECTED, 0);
    } else {
        // Failed
        ESP_LOGE(TAG, "Socket %d connection failed (error %d)", linkNum, errorCode);
        resetSocket(linkNum);
        triggerEvent(linkNum, Event::ERROR, errorCode);
    }
}

void CellularTCPClient::handleCIPCLOSE(uint8_t linkNum, int errorCode) {
    if (linkNum >= MAX_SOCKETS) {
        return;
    }

    ESP_LOGI(TAG, "URC: +CIPCLOSE: link=%d, err=%d", linkNum, errorCode);
    
    resetSocket(linkNum);
    triggerEvent(linkNum, Event::DISCONNECTED, errorCode);
}

void CellularTCPClient::handleCIPRCV(uint8_t linkNum, uint16_t dataLength) {
    if (linkNum >= MAX_SOCKETS) {
        return;
    }

    ESP_LOGD(TAG, "URC: +CIPRCV: link=%d, length=%u", linkNum, dataLength);

    m_sockets[linkNum].availableData += dataLength;
    m_sockets[linkNum].lastActivityTime = millis();
    
    triggerEvent(linkNum, Event::DATA_AVAILABLE, dataLength);
}

void CellularTCPClient::handleCDNSGIP(int result, const String& hostname, const String& ip) {
    ESP_LOGI(TAG, "URC: +CDNSGIP: result=%d, host=%s, ip=%s", 
             result, hostname.c_str(), ip.c_str());

    if (m_dnsState.pending && m_dnsState.hostname == hostname) {
        // Result is the sequence number (1, 2, 3, etc.) for multiple IPs
        // Accept any result >= 1 as success (use first IP received)
        if (result >= 1 && m_dnsState.resultIP.length() == 0) {
            m_dnsState.success = true;
            m_dnsState.resultIP = ip;
            m_dnsState.pending = false; // Stop waiting after first IP
            ESP_LOGI(TAG, "✅ DNS resolved: %s -> %s (response %d)", 
                     hostname.c_str(), ip.c_str(), result);
        }
    }
}

void CellularTCPClient::triggerEvent(uint8_t linkNum, Event event, int errorCode) {
    if (m_eventCallback) {
        m_eventCallback(linkNum, event, errorCode);
    }
}

bool CellularTCPClient::isValidIP(const String& str) const {
    // Simple check for x.x.x.x format
    int dotCount = 0;
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (c == '.') {
            dotCount++;
        } else if (!isdigit(c)) {
            return false;
        }
    }
    return dotCount == 3;
}

void CellularTCPClient::resetSocket(uint8_t linkNum) {
    if (linkNum >= MAX_SOCKETS) {
        return;
    }

    m_sockets[linkNum] = SocketInfo();
}

// ===== Utility String Converters =====

const char* CellularTCPClient::errorToString(ConnectError error) {
    switch (error) {
        case ConnectError::NONE: return "NONE";
        case ConnectError::DNS_FAILED: return "DNS_FAILED";
        case ConnectError::CONNECTION_TIMEOUT: return "CONNECTION_TIMEOUT";
        case ConnectError::CONNECTION_REFUSED: return "CONNECTION_REFUSED";
        case ConnectError::NETWORK_ERROR: return "NETWORK_ERROR";
        case ConnectError::SOCKET_NOT_AVAILABLE: return "SOCKET_NOT_AVAILABLE";
        case ConnectError::ALREADY_CONNECTED: return "ALREADY_CONNECTED";
        case ConnectError::INVALID_PARAMETERS: return "INVALID_PARAMETERS";
        case ConnectError::MODULE_ERROR: return "MODULE_ERROR";
        default: return "UNKNOWN";
    }
}

const char* CellularTCPClient::stateToString(SocketState state) {
    switch (state) {
        case SocketState::CLOSED: return "CLOSED";
        case SocketState::RESOLVING_DNS: return "RESOLVING_DNS";
        case SocketState::OPENING: return "OPENING";
        case SocketState::CONNECTED: return "CONNECTED";
        case SocketState::CLOSING: return "CLOSING";
        default: return "UNKNOWN";
    }
}
