#include "at_command_async.h"
#include <esp_log.h>

const char* ATCommandAsync::TAG = "AT_CMD_ASYNC";

ATCommandAsync::ATCommandAsync(CellularUART* uart)
    : m_uart(uart)
    , m_urcCallback(nullptr)
    , m_receiverTaskHandle(nullptr)
    , m_nextCommandId(1)
    , m_debugLogging(false)
{
    m_commandsMutex = xSemaphoreCreateMutex();
    m_cmdCompleted = xSemaphoreCreateBinary();
    
    if (!m_commandsMutex || !m_cmdCompleted) {
        ESP_LOGE(TAG, "Failed to create semaphores");
    }
}

ATCommandAsync::~ATCommandAsync() {
    if (m_receiverTaskHandle) {
        vTaskDelete(m_receiverTaskHandle);
        m_receiverTaskHandle = nullptr;
    }
    
    if (m_commandsMutex) {
        vSemaphoreDelete(m_commandsMutex);
    }
    if (m_cmdCompleted) {
        vSemaphoreDelete(m_cmdCompleted);
    }
}

void ATCommandAsync::initialize() {
    ESP_LOGI(TAG, "🚀 Initializing async AT command handler");
    
    // Create background receiver task (Core 1, high priority)
    xTaskCreatePinnedToCore(
        receiverTaskWrapper,
        "ATReceiver",
        4096,
        this,
        10,  // High priority (same as mesh)
        &m_receiverTaskHandle,
        1    // Core 1 (don't interfere with Core 0 main loop)
    );
    
    ESP_LOGI(TAG, "✅ Async AT handler ready - receiver task created");
}

uint32_t ATCommandAsync::sendCommandAsync(const String& command,
                                          uint32_t timeoutMs,
                                          bool expectOK) {
    if (!m_uart) {
        ESP_LOGE(TAG, "UART not initialized");
        return 0;
    }

    // Generate command ID
    uint32_t commandId = m_nextCommandId++;
    
    // Create pending command
    PendingCommand pending;
    pending.commandId = commandId;
    pending.command = command;
    pending.sentTimeMs = millis();
    pending.timeoutMs = timeoutMs;
    pending.status = PENDING;
    pending.expectOK = expectOK;
    
    // Add to pending map
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        m_pendingCommands[commandId] = pending;
        xSemaphoreGive(m_commandsMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex for sendCommandAsync");
        return 0;
    }
    
    // Build and send command
    String fullCommand = "AT";
    if (command.length() > 0) {
        fullCommand += command;
    }
    fullCommand += "\r\n";
    
    // Flush before send
    m_uart->flush();
    size_t bytesSent = m_uart->write(fullCommand);
    
    ESP_LOGI(TAG, "[CMD %u] TX %u bytes: %s | Timeout: %ums", commandId, bytesSent, fullCommand.c_str(), timeoutMs);
    
    // Signal receiver task to wake up
    xSemaphoreGive(m_cmdCompleted);
    
    return commandId;
}

bool ATCommandAsync::waitForResponse(uint32_t commandId,
                                    Response& outResponse,
                                    uint32_t maxWaitMs,
                                    uint32_t checkIntervalMs) {
    uint32_t startTime = millis();
    
    while ((millis() - startTime) < maxWaitMs) {
        // Check if command completed
        if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            auto it = m_pendingCommands.find(commandId);
            
            if (it != m_pendingCommands.end()) {
                PendingCommand& cmd = it->second;
                
                if (cmd.status != PENDING) {
                    // Command completed (or timed out)
                    outResponse = cmd.response;
                    m_pendingCommands.erase(it);
                    xSemaphoreGive(m_commandsMutex);
                    
                    if (m_debugLogging) {
                        ESP_LOGD(TAG, "[CMD %u] ✅ Response received in %ums",
                                commandId, millis() - startTime);
                    }
                    return (cmd.status == COMPLETED);
                }
            }
            
            xSemaphoreGive(m_commandsMutex);
        }
        
        // Non-blocking: wait and check again
        if (checkIntervalMs > 0) {
            delay(checkIntervalMs);
        } else {
            // Blocking: small delay to prevent busy loop
            delay(10);
        }
    }
    
    // Timeout - clean up
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        auto it = m_pendingCommands.find(commandId);
        if (it != m_pendingCommands.end()) {
            outResponse = it->second.response;
            outResponse.success = false;
            outResponse.errorMessage = "Response timeout";
            m_pendingCommands.erase(it);
        }
        xSemaphoreGive(m_commandsMutex);
    }
    
    ESP_LOGW(TAG, "[CMD %u] ⏱️  Timeout waiting for response after %ums",
            commandId, maxWaitMs);
    return false;
}

bool ATCommandAsync::isCommandComplete(uint32_t commandId, Response* outResponse) {
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        auto it = m_pendingCommands.find(commandId);
        
        if (it != m_pendingCommands.end()) {
            PendingCommand& cmd = it->second;
            
            if (cmd.status != PENDING) {
                if (outResponse) {
                    *outResponse = cmd.response;
                }
                m_pendingCommands.erase(it);
                xSemaphoreGive(m_commandsMutex);
                return true;
            }
        }
        
        xSemaphoreGive(m_commandsMutex);
    }
    
    return false;
}

ATCommandAsync::Response ATCommandAsync::sendCommand(const String& command,
                                                      uint32_t timeoutMs,
                                                      bool expectOK) {
    // Synchronous wrapper using async API
    uint32_t cmdId = sendCommandAsync(command, timeoutMs, expectOK);
    Response response;
    
    if (cmdId == 0) {
        response.success = false;
        response.errorMessage = "Failed to queue command";
        return response;
    }
    
    waitForResponse(cmdId, response, timeoutMs + 500);  // +500ms buffer
    return response;
}

void ATCommandAsync::registerURCCallback(URCCallback callback) {
    m_urcCallback = callback;
    ESP_LOGI(TAG, "✅ URC callback registered");
}

size_t ATCommandAsync::getPendingCommandCount() const {
    size_t count = 0;
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = m_pendingCommands.size();
        xSemaphoreGive(m_commandsMutex);
    }
    return count;
}

void ATCommandAsync::receiverTaskWrapper(void* param) {
    ATCommandAsync* pThis = static_cast<ATCommandAsync*>(param);
    pThis->receiverTask();
}

void ATCommandAsync::receiverTask() {
    ESP_LOGI(TAG, "🔄 Receiver task started on Core %d", xPortGetCoreID());
    
    String lineBuffer = "";
    uint32_t lastActivityMs = millis();
    uint8_t rxBuffer[256];
    
    while (true) {
        try {
            // Non-blocking check for available data
            if (m_uart && m_uart->available() > 0) {
                // Read available bytes (non-blocking)
                size_t len = m_uart->read(rxBuffer, sizeof(rxBuffer));
                lastActivityMs = millis();
                
                // Process each received byte
                for (size_t i = 0; i < len; i++) {
                    char c = (char)rxBuffer[i];
                    
                    if (c == '\n') {
                        // Complete line received
                        lineBuffer.trim();
                        if (lineBuffer.length() > 0) {
                            processReceivedLine(lineBuffer);
                        }
                        lineBuffer = "";
                    } else if (c == '>') {
                        // Prompt detected - treat as line
                        lineBuffer += c;
                        processReceivedLine(lineBuffer);
                        lineBuffer = "";
                    } else if (c == '\r') {
                        // Skip carriage return
                        continue;
                    } else if (c >= 32 && c < 127) {
                        // Valid ASCII character
                        lineBuffer += c;
                    } else if (c == 8 || c == 127) {
                        // Backspace - remove last character
                        if (lineBuffer.length() > 0) {
                            lineBuffer.remove(lineBuffer.length() - 1);
                        }
                    }
                    // Else: skip control characters
                }
            } else {
                // No data available - clean up periodically
                uint32_t now = millis();
                if ((now - lastActivityMs) > 500) {
                    cleanupTimedOutCommands();
                    lastActivityMs = now;
                }
                
                // Small delay to prevent busy-waiting
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception in receiver task: %s", e.what());
        }
        
        // Yield to other tasks occasionally
        taskYIELD();
    }
}

void ATCommandAsync::processReceivedLine(const String& line) {
    if (m_debugLogging) {
        ESP_LOGD(TAG, "📥 RX: %s", line.c_str());
    }
    
    // Try to match to pending command
    if (matchResponseToCommand(line)) {
        return;  // Matched to command
    }
    
    // Check if URC
    if (isURC(line)) {
        ESP_LOGD(TAG, "📬 URC: %s", line.c_str());
        if (m_urcCallback) {
            m_urcCallback(line);
        }
        return;
    }
    
    // Unknown line - log it
    if (m_debugLogging && line.length() > 0) {
        ESP_LOGV(TAG, "⓵ Unmatched: %s", line.c_str());
    }
}

bool ATCommandAsync::matchResponseToCommand(const String& line) {
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    // Look for pending commands
    for (auto it = m_pendingCommands.begin(); it != m_pendingCommands.end(); ++it) {
        uint32_t cmdId = it->first;
        PendingCommand& cmd = it->second;
        if (cmd.status != PENDING) {
            continue;  // Already completed
        }
        
        // Skip echo
        if (line.startsWith("AT")) {
            xSemaphoreGive(m_commandsMutex);
            return true;
        }
        
        // Check for final response
        if (isFinalResponse(line)) {
            // Accumulate final response line
            if (cmd.response.data.length() > 0) {
                cmd.response.data += "\n";
            }
            cmd.response.data += line;
            
            cmd.status = COMPLETED;
            cmd.response.responseTimeMs = millis() - cmd.sentTimeMs;
            
            if (line == "OK") {
                cmd.response.success = true;
            } else if (line == "ERROR") {
                cmd.response.success = false;
                cmd.response.errorMessage = "ERROR";
            } else if (line.startsWith("+CME ERROR:")) {
                cmd.response.success = false;
                cmd.response.errorMessage = line;
            } else {
                cmd.response.success = true;  // Accept other responses
            }
            
            ESP_LOGI(TAG, "[CMD %u] Complete in %ums: %s", cmdId, cmd.response.responseTimeMs, cmd.response.data.c_str());
            
            if (m_debugLogging) {
                ESP_LOGD(TAG, "[CMD %u] ✅ Final response: %s (%ums)",
                        cmdId, line.c_str(), cmd.response.responseTimeMs);
            }
            
            xSemaphoreGive(m_commandsMutex);
            return true;
        }
        
        // Accumulate data
        if (cmd.response.data.length() > 0) {
            cmd.response.data += "\n";
        }
        cmd.response.data += line;
        
        xSemaphoreGive(m_commandsMutex);
        return true;
    }
    
    xSemaphoreGive(m_commandsMutex);
    return false;
}

void ATCommandAsync::cleanupTimedOutCommands() {
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    uint32_t now = millis();
    std::vector<uint32_t> timedOut;
    
    for (auto it = m_pendingCommands.begin(); it != m_pendingCommands.end(); ++it) {
        uint32_t cmdId = it->first;
        PendingCommand& cmd = it->second;
        if (cmd.status == PENDING) {
            uint32_t elapsed = now - cmd.sentTimeMs;
            
            if (elapsed > cmd.timeoutMs) {
                // Timeout occurred
                cmd.status = TIMEOUT;
                cmd.response.success = false;
                cmd.response.errorMessage = "Command timeout";
                cmd.response.responseTimeMs = elapsed;
                
                ESP_LOGW(TAG, "[CMD %u] ⏱️  Timeout after %ums: %s",
                        cmdId, elapsed, cmd.command.c_str());
                
                timedOut.push_back(cmdId);
            }
        }
    }
    
    // Remove timed-out commands (after timeout + 1 second)
    for (uint32_t cmdId : timedOut) {
        auto it = m_pendingCommands.find(cmdId);
        if (it != m_pendingCommands.end()) {
            uint32_t elapsed = now - it->second.sentTimeMs;
            if (elapsed > (it->second.timeoutMs + 1000)) {
                m_pendingCommands.erase(it);
            }
        }
    }
    
    xSemaphoreGive(m_commandsMutex);
}

bool ATCommandAsync::isFinalResponse(const String& line) const {
    return (line == "OK" || 
            line == "ERROR" || 
            line.startsWith("+CME ERROR:") ||
            line.startsWith("+CMS ERROR:") ||
            line == "SEND OK" ||
            line == "SEND FAIL" ||
            line == ">" || 
            line.endsWith(">"));
}

bool ATCommandAsync::isURC(const String& line) const {
    if (line.length() == 0 || line.charAt(0) != '+') {
        return false;
    }
    
    return (line.startsWith("+CREG:") ||
            line.startsWith("+CGREG:") ||
            line.startsWith("+CEREG:") ||
            line.startsWith("+CSQ:") ||
            line.startsWith("+CGEV:") ||
            line.startsWith("+CIPOPEN:") ||
            line.startsWith("+CIPRCV:") ||
            line.startsWith("+CIPCLOSE:") ||
            line.startsWith("+CIPERROR:") ||
            line.startsWith("+CDNSGIP:") ||
            line.startsWith("+NETOPEN:") ||
            line.startsWith("+NETCLOSE:") ||
            line.startsWith("+CADATAIND:") ||
            line.startsWith("+CGDCONT:") ||
            line.startsWith("+CPIN:") ||
            line.startsWith("+HTTPACTION:") ||
            line.startsWith("+HTTPSTATUS:"));
}

// Static helper methods
String ATCommandAsync::extractValue(const String& response, const String& prefix) {
    int index = response.indexOf(prefix);
    if (index < 0) {
        return "";
    }

    String value = response.substring(index + prefix.length());
    value.trim();
    return value;
}

std::vector<String> ATCommandAsync::splitValues(const String& value) {
    std::vector<String> result;
    int startIndex = 0;
    int commaIndex = 0;

    while ((commaIndex = value.indexOf(',', startIndex)) >= 0) {
        String part = value.substring(startIndex, commaIndex);
        part.trim();
        result.push_back(part);
        startIndex = commaIndex + 1;
    }

    // Add last part
    String lastPart = value.substring(startIndex);
    lastPart.trim();
    if (lastPart.length() > 0) {
        result.push_back(lastPart);
    }

    return result;
}

void ATCommandAsync::sendRawData(const uint8_t* data, size_t len) {
    if (m_uart) {
        m_uart->write(data, len);
        // Log summary of data sent
        ESP_LOGI(TAG, "📤 TX RAW: %u bytes", len);
    }
}

void ATCommandAsync::sendRawData(const String& data) {
    sendRawData((const uint8_t*)data.c_str(), data.length());
}

uint32_t ATCommandAsync::expectResponse(uint32_t timeoutMs) {
    // Generate command ID
    uint32_t commandId = m_nextCommandId++;
    
    // Create pending command (but don't send anything)
    PendingCommand pending;
    pending.commandId = commandId;
    pending.command = "[EXPECT_RESPONSE]";
    pending.sentTimeMs = millis();
    pending.timeoutMs = timeoutMs;
    pending.status = PENDING;
    pending.expectOK = true;
    
    // Add to pending map
    if (xSemaphoreTake(m_commandsMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        m_pendingCommands[commandId] = pending;
        xSemaphoreGive(m_commandsMutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire mutex for expectResponse");
        return 0;
    }
    
    ESP_LOGI(TAG, "[CMD %u] Waiting for response (timeout: %ums)", commandId, timeoutMs);
    return commandId;
}
