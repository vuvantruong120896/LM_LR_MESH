/**
 * @file cellular_ssl_client.cpp
 * @brief SSL/TLS Client Implementation for A7682S
 */

#include "cellular_ssl_client.h"
#include "at_command_handler.h"
#include <esp_log.h>

static const char* TAG = "SSL_CLIENT";

CellularSSLClient::CellularSSLClient(CellularConnectionService* connectionService)
    : m_connectionService(connectionService),
      m_atHandler(nullptr),
      m_state(State::CLOSED),
      m_sessionId(-1),
      m_httpServiceStarted(false),
      m_cchOpenReceived(false),
      m_cchOpenSessionId(-1),
    m_cchOpenErrorCode(-1) {
    
    if (m_connectionService) {
        m_atHandler = m_connectionService->getATHandler();
        
        // Register as secondary URC handler (non-interfering with ConnectionService)
        m_connectionService->registerSecondaryURCCallback([this](const String& urc) {
            this->handleURC(urc);
        });
    }
}

CellularSSLClient::~CellularSSLClient() {
    if (m_httpServiceStarted) {
        stopHTTPService();
    }
}

bool CellularSSLClient::initialize() {
    if (!m_atHandler || !m_connectionService) {
        ESP_LOGE(TAG, "AT handler or connection service not available");
        return false;
    }

    if (!m_connectionService->isConnected()) {
        ESP_LOGE(TAG, "Cellular not connected");
        return false;
    }

    // Configure SSL settings
    if (!configureSSL()) {
        ESP_LOGE(TAG, "Failed to configure SSL");
        return false;
    }

    // Start HTTP service
    if (!startHTTPService()) {
        ESP_LOGE(TAG, "Failed to start HTTP service");
        return false;
    }

    ESP_LOGI(TAG, "SSL client initialized");
    return true;
}

bool CellularSSLClient::configureSSL() {
    // Set SSL version to TLS 1.2 (context index 0)
    // AT+CSSLCFG="sslversion",0,4  (4 = TLS 1.2)
    ATCommandHandler::Response resp = m_atHandler->sendCommand("+CSSLCFG=\"sslversion\",0,4", 2000);
    if (!resp.success) {
        ESP_LOGW(TAG, "Failed to set SSL version, continuing anyway");
    }

    // Set auth mode to no verification (for Firebase)
    // AT+CSSLCFG="authmode",0,0  (0 = no auth)
    resp = m_atHandler->sendCommand("+CSSLCFG=\"authmode\",0,0", 2000);
    if (!resp.success) {
        ESP_LOGW(TAG, "Failed to set auth mode, continuing anyway");
    }

    // // Enable SNI (Server Name Indication) so the server can match hostname to certificate
    // // AT+CSSLCFG="sni",0,1
    // resp = m_atHandler->sendCommand("+CSSLCFG=\"sni\",0,1", 2000);
    // if (!resp.success) {
    //     ESP_LOGW(TAG, "Failed to enable SNI, continuing anyway");
    // }

    ESP_LOGI(TAG, "SSL configured: TLS 1.2, no cert verification");
    return true;
}

bool CellularSSLClient::startHTTPService() {
    if (m_httpServiceStarted) {
        ESP_LOGD(TAG, "HTTP service already started");
        return true;
    }

    // Start HTTP service
    // AT+CCHSTART
    ATCommandHandler::Response resp = m_atHandler->sendCommand("+CCHSTART", 5000);
    if (!resp.success) {
        ESP_LOGE(TAG, "Failed to start HTTP service - response: '%s'", resp.data.c_str());
        return false;
    }

    m_httpServiceStarted = true;
    ESP_LOGI(TAG, "HTTP service started - response: '%s'", resp.data.c_str());
    return true;
}

bool CellularSSLClient::stopHTTPService() {
    if (!m_httpServiceStarted) {
        return true;
    }

    // Stop HTTP service
    // AT+CCHSTOP
    ATCommandHandler::Response resp = m_atHandler->sendCommand("+CCHSTOP", 5000);
    if (!resp.success) {
        ESP_LOGW(TAG, "Failed to stop HTTP service");
        return false;
    }

    m_httpServiceStarted = false;
    ESP_LOGI(TAG, "HTTP service stopped");
    return true;
}

bool CellularSSLClient::connect(const String& host, uint16_t port, uint32_t timeoutMs) {
    if (m_state == State::CONNECTED) {
        ESP_LOGW(TAG, "Already connected, disconnecting first");
        disconnect();
    }

    if (!m_httpServiceStarted && !initialize()) {
        ESP_LOGE(TAG, "Failed to initialize SSL client");
        return false;
    }

    m_state = State::OPENING;

    // Open HTTPS connection
    // AT+CCHOPEN=<pdpidx>,"<url>",<port>,2
    // Mode 2 = HTTPS
    String cmd = "+CCHOPEN=0,\"";
    cmd += host;
    cmd += "\",";
    cmd += String(port);
    cmd += ",2";  // HTTPS mode

    ESP_LOGI(TAG, "Opening HTTPS connection to %s:%d", host.c_str(), port);

    // AT+CCHOPEN can take 5-10 seconds to establish SSL connection
    // Increased from 5000ms to 10000ms
    ATCommandHandler::Response resp = m_atHandler->sendCommand(cmd.c_str(), 10000);
    if (!resp.success) {
        ESP_LOGE(TAG, "Failed to send AT+CCHOPEN command");
        m_state = State::ERROR;
        return false;
    }

    // Log response to see if session ID is in response data
    ESP_LOGD(TAG, "AT+CCHOPEN response: success=%d, data='%s'", resp.success, resp.data.c_str());
    
    // Try to parse session ID from response
    // Some firmware versions return: +CCHOPEN: <sessionid>,<err> in data
    int sessionId = 0;
    if (resp.data.indexOf("+CCHOPEN:") >= 0) {
        int idx = resp.data.indexOf("+CCHOPEN:");
        int comma = resp.data.indexOf(',', idx);
        if (comma > idx) {
            String idStr = resp.data.substring(idx + 9, comma);
            idStr.trim();
            sessionId = idStr.toInt();
            ESP_LOGI(TAG, "Parsed session ID from response: %d", sessionId);
        }
    }
    
    // A7682S behavior analysis:
    // - AT+CCHOPEN returns OK immediately (within ~2 seconds)
    // - Unlike TCP (+CIPOPEN), HTTP/HTTPS does NOT send +CCHOPEN URC
    // - Session ID is ALWAYS 0 for the first connection
    // 
    // WORKAROUND: Assume session ID = 0 if OK received
    // We'll validate connection by attempting to send request
    
    ESP_LOGI(TAG, "✅ AT+CCHOPEN returned OK, using session ID = %d", sessionId);
    m_sessionId = sessionId;
    m_state = State::CONNECTED;
    m_availableBytes = 0; // reset any pending counters
    
    // Optional: Add longer delay to let modem complete TLS handshake for cellular
    delay(1500);  // increased from 1200ms to 1500ms for better cellular reliability
    
    return true;
    
    /* ORIGINAL URC-BASED APPROACH (doesn't work for CCHOPEN):
    
    // Wait for +CCHOPEN URC (asynchronous response)
    // URC format: +CCHOPEN: <sessionid>,<err>
    ESP_LOGI(TAG, "Waiting for +CCHOPEN URC (timeout: %ums)...", timeoutMs);
    
    // Reset URC flags
    m_cchOpenReceived = false;
    m_cchOpenSessionId = -1;
    m_cchOpenErrorCode = -1;
    
    uint32_t startTime = millis();
    
    while (millis() - startTime < timeoutMs) {
        // Process URCs (this will trigger handleURC callback)
        m_atHandler->processURCs();
        
        // Check if URC was received
        if (m_cchOpenReceived) {
            if (m_cchOpenErrorCode == 0) {
                m_sessionId = m_cchOpenSessionId;
                m_state = State::CONNECTED;
                ESP_LOGI(TAG, "✅ HTTPS connection opened, session ID: %d", m_sessionId);
                return true;
            } else {
                ESP_LOGE(TAG, "HTTPS connection failed with error code: %d", m_cchOpenErrorCode);
                m_state = State::ERROR;
                return false;
            }
        }
        
        delay(100);  // Small delay between URC checks
    }

    ESP_LOGE(TAG, "Timeout waiting for +CCHOPEN URC");
    m_state = State::ERROR;
    return false;
    */
}

int CellularSSLClient::send(const String& request) {
    if (m_state != State::CONNECTED) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    int dataLength = request.length();
    
    // Send HTTPS request using sendDataCommand
    // AT+CCHSEND=<sessionid>,<length>
    String cmd = "+CCHSEND=";
    cmd += String(m_sessionId);
    cmd += ",";
    cmd += String(dataLength);

    ESP_LOGD(TAG, "Sending %d bytes via HTTPS", dataLength);

    ATCommandHandler::Response resp = m_atHandler->sendDataCommand(
        cmd, 
        request, 
        '>',      // Prompt character to wait for
        10000     // Timeout
    );
    
    if (!resp.success) {
        ESP_LOGE(TAG, "Failed to send data");
        return -1;
    }

    // Log raw modem response after CCHSEND to check for server response indication
    ESP_LOGD(TAG, "Send response: %s", resp.data.c_str());
    ESP_LOGD(TAG, "HTTPS send successful");
    return dataLength;
}

int CellularSSLClient::receive(char* buffer, size_t maxLength, uint32_t timeoutMs) {

    if (m_state != State::CONNECTED) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    // Prefer URC-gated receive: wait briefly while processing URCs
    uint32_t start = millis();
    const uint32_t urcWaitMs = timeoutMs > 0 ? min<uint32_t>(timeoutMs, 2000) : 0; // increased from 1.5s to 2s for cellular
    if (m_availableBytes <= 0 && urcWaitMs > 0) {
        while ((millis() - start) < urcWaitMs) {
            // Pump URCs so +CCHRECV indications are captured
            if (m_atHandler) m_atHandler->processURCs();
            if (m_availableBytes > 0) break;
            delay(30);  // increased from 20ms to 30ms for cellular processing
        }
    }

    // Hybrid fallback: bounded polling if no URC arrived
    // DISABLED: Polling causes AT+CCHRECV spam with ERROR responses
    // Just wait for URC instead
    if (false && m_availableBytes <= 0 && timeoutMs > urcWaitMs) {
        uint32_t pollStart = millis();
        uint32_t backoff = 200; // ms (increased from 100ms for cellular)
        const uint32_t maxBackoff = 800;  // increased from 500ms
        // Try a few light polls within the remaining timeout budget
        while ((millis() - pollStart) < (timeoutMs - urcWaitMs)) {
            // Request larger chunk for HTTP responses (typically 1-2KB)
            // Instead of 1-byte probe, read 512 bytes to get full response
            String probe = "+CCHRECV=" + String(m_sessionId) + ",512";
            
            ATCommandHandler::Response r = m_atHandler->sendCommand(probe.c_str(), 800);  // increased from 500ms
            
            // Check if data was received
            int dataLength = 0;
            if (r.data.indexOf("+CCHRECV:") >= 0) {
                int tag = r.data.indexOf("+CCHRECV:");
                int headerEnd = r.data.indexOf('\n', tag);
                String header = headerEnd > tag ? r.data.substring(tag + 9, headerEnd) : r.data.substring(tag + 9);
                header.trim();
                int comma = header.indexOf(',');
                if (comma >= 0) {
                    String lenStr = header.substring(comma + 1); lenStr.trim();
                    dataLength = lenStr.toInt();
                }
            }
            
            if (r.success && r.data.indexOf("+CCHRECV:") >= 0 && dataLength > 0) {
                // We received at least 1 byte; fold it into available buffer
                // Parse length from header and place into m_availableBytes
                int tag = r.data.indexOf("+CCHRECV:");
                int headerEnd = r.data.indexOf('\n', tag);
                String header = headerEnd > tag ? r.data.substring(tag + 9, headerEnd) : r.data.substring(tag + 9);
                header.trim();
                int rxLen = 0;
                int comma = header.indexOf(',');
                if (comma >= 0) {
                    String lenStr = header.substring(comma + 1); lenStr.trim();
                    rxLen = lenStr.toInt();
                } else {
                    rxLen = header.toInt();
                }
                // Copy data payload into buffer immediately if caller provided space
                int dataStart = r.data.indexOf('\n', tag);
                if (dataStart >= 0) {
                    dataStart++;
                    if (rxLen > 0) {
                        int maxCopy = (int)maxLength - 1;
                        int bytesToCopy = (rxLen < maxCopy) ? rxLen : maxCopy;
                        memcpy(buffer, r.data.c_str() + dataStart, bytesToCopy);
                        buffer[bytesToCopy] = '\0';
                        // No need to set m_availableBytes since we already consumed
                        return bytesToCopy;
                    }
                }
            }
            // No data yet — gentle backoff with longer delays for cellular
            delay(backoff);
            if (backoff < 800) backoff += 150;  // increased max backoff to 800ms for cellular
            if (m_atHandler) m_atHandler->processURCs();
            if (m_availableBytes > 0) break; // URC may arrive during fallback
        }
    }

    if (m_availableBytes <= 0) {
        // Still no data available
        return 0;
    }

    // Determine read length based on available bytes
    int avail = m_availableBytes;
    int reqLen = (int)maxLength;
    int toReadInt = (avail < reqLen) ? avail : reqLen;
    if (toReadInt < 0) toReadInt = 0;
    size_t toRead = (size_t)toReadInt;

    // AT+CCHRECV=<sessionid>,<length>
    String cmd = "+CCHRECV=";
    cmd += String(m_sessionId);
    cmd += ",";
    cmd += String((int)toRead);

    ATCommandHandler::Response resp = m_atHandler->sendCommand(cmd.c_str(), timeoutMs);
    if (!resp.success) {
        // Treat as no data available to avoid log spam
        return 0;
    }

    int tag = resp.data.indexOf("+CCHRECV:");
    if (tag < 0) {
        ESP_LOGW(TAG, "Invalid receive response format");
        return 0;
    }

    int headerEnd = resp.data.indexOf('\n', tag);
    String header = headerEnd > tag ? resp.data.substring(tag + 9, headerEnd) : resp.data.substring(tag + 9);
    header.trim();

    int dataLength = 0;
    int comma = header.indexOf(',');
    if (comma >= 0) {
        String lenStr = header.substring(comma + 1); lenStr.trim();
        dataLength = lenStr.toInt();
    } else {
        dataLength = header.toInt();
    }

    if (dataLength <= 0) {
        return 0;
    }

    int dataStart = resp.data.indexOf('\n', tag);
    if (dataStart < 0) return 0;
    dataStart++;

    int maxCopy = (int)maxLength - 1;
    int bytesToCopy = (dataLength < maxCopy) ? dataLength : maxCopy;
    memcpy(buffer, resp.data.c_str() + dataStart, bytesToCopy);
    buffer[bytesToCopy] = '\0';

    // Decrease available counter for subsequent reads
    int remaining = m_availableBytes - bytesToCopy;
    m_availableBytes = (remaining > 0) ? remaining : 0;
    return bytesToCopy;
}

bool CellularSSLClient::disconnect() {
    if (m_state == State::CLOSED) {
        return true;
    }

    m_state = State::CLOSING;

    if (m_sessionId >= 0) {
        // Close HTTPS connection
        // AT+CCHCLOSE=<sessionid>
        String cmd = "+CCHCLOSE=";
        cmd += String(m_sessionId);

        ESP_LOGI(TAG, "Closing HTTPS connection (session %d)", m_sessionId);

        ATCommandHandler::Response resp = m_atHandler->sendCommand(cmd.c_str(), 5000);
        if (!resp.success) {
            ESP_LOGW(TAG, "Failed to close HTTPS connection");
        }

        m_sessionId = -1;
    }

    m_state = State::CLOSED;
    ESP_LOGI(TAG, "HTTPS connection closed");
    return true;
}

ATCommandHandler* CellularSSLClient::getATHandler() const {
    return m_atHandler;
}

void CellularSSLClient::handleURC(const String& urc) {
    if (urc.startsWith("+CCHOPEN:")) {
        // Parse: +CCHOPEN: <sessionid>,<err>
        String value = ATCommandHandler::extractValue(urc, "+CCHOPEN:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            m_cchOpenSessionId = parts[0].toInt();
            m_cchOpenErrorCode = parts[1].toInt();
            m_cchOpenReceived = true;
            
            ESP_LOGI(TAG, "URC: +CCHOPEN: session=%d, err=%d", 
                     m_cchOpenSessionId, m_cchOpenErrorCode);
        }
    } else if (urc.startsWith("+CCHRECV:")) {
        // Two possible formats seen on some firmware versions:
        // 1) +CCHRECV: <session>,<len>
        // 2) +CCHRECV: <len>
        String value = ATCommandHandler::extractValue(urc, "+CCHRECV:");
        value.trim();
        int len = 0;
        auto parts = ATCommandHandler::splitValues(value);
        if (parts.size() >= 2) {
            // parts[0] = session, parts[1] = len
            len = parts[1].toInt();
        } else if (parts.size() == 1) {
            len = parts[0].toInt();
        }
        if (len > 0) {
            // Accumulate available bytes; receive() will drain and decrement
            m_availableBytes += len;
            ESP_LOGI(TAG, "[URC] +CCHRECV: %d bytes available (session data)", len);
        }
    }
}
