#include "at_command_handler.h"
#include <esp_log.h>

const char* ATCommandHandler::TAG = "AT_CMD";

ATCommandHandler::ATCommandHandler(CellularUART* uart)
    : m_uart(uart)
    , m_urcCallback(nullptr)
    , m_commandMutex(nullptr)
{
    // Create mutex for AT command serialization
    m_commandMutex = xSemaphoreCreateMutex();
    if (!m_commandMutex) {
        ESP_LOGE(TAG, "Failed to create command mutex");
    } else {
        ESP_LOGD(TAG, "AT command mutex created");
    }
}

ATCommandHandler::~ATCommandHandler() {
    if (m_commandMutex) {
        vSemaphoreDelete(m_commandMutex);
        m_commandMutex = nullptr;
        ESP_LOGD(TAG, "AT command mutex deleted");
    }
}

ATCommandHandler::Response ATCommandHandler::sendCommand(
    const String& command,
    uint32_t timeoutMs,
    bool expectOK
) {
    Response response;
    uint32_t startTime = millis();

    if (!m_uart) {
        ESP_LOGE(TAG, "UART not initialized");
        response.errorMessage = "UART not initialized";
        return response;
    }

    // 🔒 Acquire mutex to serialize AT commands
    if (!m_commandMutex || xSemaphoreTake(m_commandMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire command mutex (timeout %ums)", MUTEX_TIMEOUT_MS);
        response.errorMessage = "Command serialization timeout";
        return response;
    }

    // Build full AT command
    String fullCommand = "AT";
    if (command.length() > 0) {
        fullCommand += command;
    }
    fullCommand += "\r\n";

    ESP_LOGD(TAG, "TX: %s", fullCommand.c_str());

    // Clear RX buffer before sending
    m_uart->flush();

    // Send command
    m_uart->write(fullCommand);

    // Read response
    response = readResponse(timeoutMs, expectOK);
    response.responseTimeMs = millis() - startTime;

    // 🔓 Release mutex
    xSemaphoreGive(m_commandMutex);

    ESP_LOGD(TAG, "RX: %s [%s, %dms]",
             response.data.c_str(),
             response.success ? "OK" : "ERROR",
             response.responseTimeMs);

    return response;
}

ATCommandHandler::Response ATCommandHandler::sendRawCommand(
    const String& rawCommand,
    uint32_t timeoutMs
) {
    Response response;
    uint32_t startTime = millis();

    if (!m_uart) {
        ESP_LOGE(TAG, "UART not initialized");
        response.errorMessage = "UART not initialized";
        return response;
    }

    // 🔒 Acquire mutex to serialize AT commands
    if (!m_commandMutex || xSemaphoreTake(m_commandMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire command mutex for raw command");
        response.errorMessage = "Command serialization timeout";
        return response;
    }

    ESP_LOGD(TAG, "TX (raw): %s", rawCommand.c_str());

    m_uart->flush();
    m_uart->write(rawCommand);

    response = readResponse(timeoutMs, true);
    response.responseTimeMs = millis() - startTime;

    // 🔓 Release mutex
    xSemaphoreGive(m_commandMutex);

    return response;
}

ATCommandHandler::Response ATCommandHandler::sendDataCommand(
    const String& command,
    const String& data,
    char promptChar,
    uint32_t timeoutMs
) {
    Response response;

    // 🔒 Acquire mutex to serialize AT commands (data commands are multi-step)
    if (!m_commandMutex || xSemaphoreTake(m_commandMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire command mutex for data command");
        response.errorMessage = "Command serialization timeout";
        return response;
    }

    // Step 1: Send initial command WITHOUT consuming the prompt
    // Build full AT command like sendCommand(), but don't call readResponse
    ESP_LOGD(TAG, "Data command step 1: %s", command.c_str());
    if (!m_uart) {
        response.success = false;
        response.errorMessage = "UART not initialized";
        // 🔓 Release mutex before return
        xSemaphoreGive(m_commandMutex);
        return response;
    }

    // Clear RX and send command
    m_uart->flush();
    String fullCommand = String("AT") + command + "\r\n";
    ESP_LOGD(TAG, "TX: %s", fullCommand.c_str());
    m_uart->write(fullCommand);

    // Step 2: Wait for prompt character
    uint32_t startTime = millis();
    bool gotPrompt = false;
    while ((millis() - startTime) < timeoutMs) {
        if (m_uart->available()) {
            uint8_t c;
            if (m_uart->read(&c, 1) == 1) {
                if (c == (uint8_t)promptChar) {
                    gotPrompt = true;
                    break;
                }
            }
        }
        delay(1);
    }

    if (!gotPrompt) {
        ESP_LOGE(TAG, "Timeout waiting for prompt '%c'", promptChar);
        response.success = false;
        response.errorMessage = "Timeout waiting for prompt";
        // 🔓 Release mutex before return
        xSemaphoreGive(m_commandMutex);
        return response;
    }

    ESP_LOGD(TAG, "Got prompt '%c', sending data (%d bytes)", promptChar, data.length());

    // Step 3: Send data as-is (no CRLF)
    m_uart->write(data);

    // Step 4: Wait for final response (OK/SEND OK or errors)
    response = readResponse(timeoutMs, true);
    
    // 🔓 Release mutex after completion
    xSemaphoreGive(m_commandMutex);
    
    return response;
}

bool ATCommandHandler::testAT(uint8_t retries) {
    ESP_LOGI(TAG, "Testing AT communication (retries=%d)...", retries);

    for (uint8_t i = 0; i < retries; i++) {
        // Increased timeout from 1000ms to 2000ms for slower module response
        // Also retry pattern: initial tests get longer timeout
        uint32_t timeout = (i < 3) ? 2000 : 1500;  // First 3 attempts get 2s, rest get 1.5s
        
        Response response = sendCommand("", timeout, true);
        if (response.success) {
            ESP_LOGI(TAG, "AT test OK on attempt %d/%d", i + 1, retries);
            return true;
        }
        
        if (i < retries - 1) {
            // Increased delay between retries from 500ms to 1000ms
            delay(1000);
        }
    }

    ESP_LOGE(TAG, "AT test FAILED after %d retries", retries);
    return false;
}

bool ATCommandHandler::waitForResponse(const String& expectedResponse, uint32_t timeoutMs) {
    uint32_t startTime = millis();

    while ((millis() - startTime) < timeoutMs) {
        if (m_uart->available()) {
            String line = m_uart->readUntilTimeout(100);
            if (line.indexOf(expectedResponse) >= 0) {
                return true;
            }
        }
        delay(10);
    }

    return false;
}

bool ATCommandHandler::waitForURC(const String& prefix, String& outLine, uint32_t timeoutMs) {
    if (!m_uart) {
        ESP_LOGE(TAG, "UART not initialized");
        return false;
    }

    uint32_t startTime = millis();
    while ((millis() - startTime) < timeoutMs) {
        if (m_uart->available()) {
            char buffer[512];
            size_t len = m_uart->readLine(buffer, sizeof(buffer), 300);
            if (len > 0) {
                String line(buffer);
                line.trim();

                if (line.length() == 0) {
                    continue;
                }

                if (line.startsWith(prefix)) {
                    outLine = line;
                    ESP_LOGD(TAG, "URC match: %s", line.c_str());
                    return true;
                }

                if (isURC(line)) {
                    if (m_urcCallback) {
                        m_urcCallback(line);
                    }
                    continue;
                }

                // Ignore other lines (e.g., echoes, final responses)
            }
        }
        delay(1);
    }

    ESP_LOGW(TAG, "Timeout waiting for URC prefix: %s", prefix.c_str());
    return false;
}

void ATCommandHandler::registerURCCallback(URCCallback callback) {
    m_urcCallback = callback;
}

void ATCommandHandler::processURCs() {
    // Non-blocking URC processing
    while (m_uart->available()) {
        char buffer[512];  // Increased for TCP data responses
        size_t len = m_uart->readLine(buffer, sizeof(buffer), 100);
        
        if (len > 0) {
            String line(buffer);
            line.trim();
            
            if (isURC(line)) {
                ESP_LOGD(TAG, "URC: %s", line.c_str());
                
                if (m_urcCallback) {
                    m_urcCallback(line);
                }
            }
        }
    }
}

ATCommandHandler::Response ATCommandHandler::setEcho(bool enable) {
    String command = enable ? "E1" : "E0";
    return sendCommand(command, 1000, true);
}

int ATCommandHandler::parseCMEError(const String& response) {
    // Format: "+CME ERROR: <code>"
    int index = response.indexOf("+CME ERROR:");
    if (index < 0) {
        return -1;
    }

    String codeStr = response.substring(index + 12);  // "+CME ERROR: " = 12 chars
    codeStr.trim();
    return codeStr.toInt();
}

int ATCommandHandler::parseCMSError(const String& response) {
    // Format: "+CMS ERROR: <code>"
    int index = response.indexOf("+CMS ERROR:");
    if (index < 0) {
        return -1;
    }

    String codeStr = response.substring(index + 12);  // "+CMS ERROR: " = 12 chars
    codeStr.trim();
    return codeStr.toInt();
}

String ATCommandHandler::extractValue(const String& response, const String& prefix) {
    int index = response.indexOf(prefix);
    if (index < 0) {
        return "";
    }

    String value = response.substring(index + prefix.length());
    value.trim();
    return value;
}

std::vector<String> ATCommandHandler::splitValues(const String& value) {
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

ATCommandHandler::Response ATCommandHandler::readResponse(uint32_t timeoutMs, bool expectOK) {
    Response response;
    uint32_t startTime = millis();
    String fullResponse = "";
    bool gotFinalResponse = false;

    while ((millis() - startTime) < timeoutMs) {
        if (m_uart->available()) {
            char buffer[1024];  // Increased for TCP data responses
            size_t len = m_uart->readLine(buffer, sizeof(buffer), 300);
            
            if (len > 0) {
                String line(buffer);
                line.trim();

                // Skip empty lines
                if (line.length() == 0) {
                    continue;
                }

                // Skip echo (command itself)
                if (line.startsWith("AT")) {
                    continue;
                }

                // Suppress per-line debug logging here; we'll log the
                // aggregated response once when complete or on timeout.
                // ESP_LOGD(TAG, "  <- %s", line.c_str());

                // Check for final response
                if (isFinalResponse(line)) {
                    gotFinalResponse = true;
                    
                    if (line == "OK") {
                        response.success = true;
                    } else if (line == "ERROR") {
                        response.success = false;
                        response.errorMessage = "ERROR";
                    } else if (line == "SEND OK") {
                        // Data send confirmation from module
                        response.success = true;
                    } else if (line == "SEND FAIL") {
                        response.success = false;
                        response.errorMessage = "SEND FAIL";
                    } else if (line.startsWith("+CME ERROR:")) {
                        response.success = false;
                        response.errorCode = parseCMEError(line);
                        response.errorMessage = line;
                    } else if (line.startsWith("+CMS ERROR:")) {
                        response.success = false;
                        response.errorCode = parseCMSError(line);
                        response.errorMessage = line;
                    }
                    
                    break;
                }

                // Accumulate data
                if (fullResponse.length() > 0) {
                    fullResponse += "\n";
                }
                fullResponse += line;
            }
        }
        delay(1);
    }

    if (!gotFinalResponse && expectOK) {
        ESP_LOGW(TAG, "Timeout waiting for final response");
        response.success = false;
        response.errorMessage = "Timeout";
    } else if (!expectOK) {
        // For commands that don't expect OK
        response.success = true;
    }

    // Assign accumulated data and log once (either full response or timeout)
    response.data = fullResponse;

    if (gotFinalResponse) {
        ESP_LOGD(TAG, "RX (full): %s [%s]", response.data.c_str(), response.success ? "OK" : "ERROR");
    } else {
        ESP_LOGD(TAG, "RX (timeout): %s [TIMEOUT]", response.data.c_str());
    }

    return response;
}

bool ATCommandHandler::isFinalResponse(const String& line) const {
    return (line == "OK" || 
            line == "ERROR" || 
            line.startsWith("+CME ERROR:") ||
            line.startsWith("+CMS ERROR:") ||
            line == "SEND OK" ||
            line == "SEND FAIL");
}

bool ATCommandHandler::isURC(const String& line) const {
    // URCs typically start with '+' and are not final responses
    if (line.length() == 0 || line.charAt(0) != '+') {
        return false;
    }

    // Exclude final responses
    if (line.startsWith("+CME ERROR:") || line.startsWith("+CMS ERROR:")) {
        return false;
    }

    // Common URCs for A7682S
    return (line.startsWith("+CREG:") ||
            line.startsWith("+CGREG:") ||
            line.startsWith("+CEREG:") ||
            line.startsWith("+CSQ:") ||
            line.startsWith("+CGEV:") ||
            line.startsWith("+CIPOPEN:") ||      // TCP connection opened
            line.startsWith("+CIPRCV:") ||       // TCP data received
            line.startsWith("+CIPCLOSE:") ||     // TCP connection closed
            line.startsWith("+CIPERROR:") ||     // TCP error
            line.startsWith("+CDNSGIP:") ||      // DNS resolution result
            line.startsWith("+NETOPEN:") ||
            line.startsWith("+NETCLOSE:") ||
            line.startsWith("+CADATAIND:") ||
            line.startsWith("+CGDCONT:") ||
            line.startsWith("+CPIN:") ||
            line.startsWith("+HTTPACTION:") ||
            line.startsWith("+HTTPSTATUS:"));
}
