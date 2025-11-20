#include "../include/modbus_rtu_driver.h"
#include "esp_log.h"
#include <cstring>
#include <cstdarg>

// ============================================================================
// STATIC MEMBER INITIALIZATION
// ============================================================================

bool ModbusRTUDriver::initialized = false;
uint8_t ModbusRTUDriver::lastErrorCode = 0;
char ModbusRTUDriver::lastErrorMsg[256] = {0};
uint16_t ModbusRTUDriver::slaveAddress = 0;  // 0 = use default from config

// ============================================================================
// INITIALIZATION & SHUTDOWN
// ============================================================================

bool ModbusRTUDriver::initialize() {
    if (initialized) {
        ESP_LOGW(MODBUS_TAG, "Modbus driver already initialized");
        return true;
    }

    // Configure UART
    uart_config_t uartConfig = {
        .baud_rate = MODBUS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };

    // Install UART driver
    esp_err_t err = uart_driver_install(RS485_UART_NUM, 256, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        setError(1, "uart_driver_install failed: %s", esp_err_to_name(err));
        ESP_LOGE(MODBUS_TAG, "%s", lastErrorMsg);
        return false;
    }

    // Configure UART parameters
    err = uart_param_config(RS485_UART_NUM, &uartConfig);
    if (err != ESP_OK) {
        setError(2, "uart_param_config failed: %s", esp_err_to_name(err));
        ESP_LOGE(MODBUS_TAG, "%s", lastErrorMsg);
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    // Set UART pins
    err = uart_set_pin(RS485_UART_NUM, RS485_TX_PIN, RS485_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        setError(3, "uart_set_pin failed: %s", esp_err_to_name(err));
        ESP_LOGE(MODBUS_TAG, "%s", lastErrorMsg);
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    // Configure DE pin as output (RS485 transmit enable)
    gpio_config_t ioConf = {
        .pin_bit_mask = (1ULL << RS485_DE_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    err = gpio_config(&ioConf);
    if (err != ESP_OK) {
        setError(4, "gpio_config failed: %s", esp_err_to_name(err));
        ESP_LOGE(MODBUS_TAG, "%s", lastErrorMsg);
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    // Start in receiver mode
    enableReceiver();

    initialized = true;
    lastErrorCode = 0;
    ESP_LOGI(MODBUS_TAG, "✅ Modbus RTU driver initialized (UART%d, %d bps, DE=%d)", 
             RS485_UART_NUM + 1, MODBUS_BAUD_RATE, RS485_DE_PIN);

    return true;
}

void ModbusRTUDriver::shutdown() {
    if (!initialized) return;

    enableReceiver();  // Safe state
    uart_driver_delete(RS485_UART_NUM);
    initialized = false;

    ESP_LOGI(MODBUS_TAG, "Modbus RTU driver shut down");
}

bool ModbusRTUDriver::isReady() {
    return initialized;
}

// ============================================================================
// REGISTER READ OPERATIONS
// ============================================================================

int16_t ModbusRTUDriver::readInputRegister(uint16_t registerAddr) {
    float values[1];
    if (readInputRegisters(registerAddr, 1, values)) {
        return (int16_t)values[0];
    }
    return -1;
}

int16_t ModbusRTUDriver::readHoldingRegister(uint16_t registerAddr) {
    float values[1];
    if (readHoldingRegisters(registerAddr, 1, values)) {
        return (int16_t)values[0];
    }
    return -1;
}

bool ModbusRTUDriver::readInputRegisters(uint16_t startAddr, uint16_t count, float* outValues) {
    if (!initialized) {
        setError(10, "Driver not initialized");
        return false;
    }

    if (count == 0 || count > 125) {  // Modbus limit: max 125 registers per request
        setError(11, "Invalid register count: %u (must be 1-125)", count);
        return false;
    }

    if (!outValues) {
        setError(12, "Output buffer is NULL");
        return false;
    }

    // Retry loop
    for (uint8_t attempt = 0; attempt < MODBUS_RETRY_COUNT; attempt++) {
        if (attempt > 0) {
            // Exponential backoff delay
            uint32_t delayMs = MODBUS_RETRY_DELAY_MS * (1 << (attempt - 1));
            ESP_LOGD(MODBUS_TAG, "Retry %u/%u after %u ms", attempt, MODBUS_RETRY_COUNT, delayMs);
            delay(delayMs);
        }

        // Build request frame
        uint8_t requestFrame[16];
        uint16_t requestLen = 0;

        if (!buildReadInputRegistersRequest(getSlaveAddress(), startAddr, count, requestFrame, &requestLen)) {
            ESP_LOGW(MODBUS_TAG, "Failed to build request frame");
            continue;
        }

        // Send request
        if (!sendModbusRequest(requestFrame, requestLen)) {
            ESP_LOGW(MODBUS_TAG, "Failed to send request");
            continue;
        }

        // Receive response
        uint8_t responseFrame[256];
        uint16_t responseLen = 0;

        if (!receiveModbusResponse(responseFrame, sizeof(responseFrame), &responseLen)) {
            // Only log warning on last attempt to reduce spam during scanning
            if (attempt == MODBUS_RETRY_COUNT - 1) {
                ESP_LOGW(MODBUS_TAG, "Failed to receive response (attempt %u/%u)", attempt + 1, MODBUS_RETRY_COUNT);
            }
            continue;
        }

        // Parse response
        uint16_t parsedCount = 0;
        if (!parseReadInputRegistersResponse(responseFrame, responseLen, outValues, &parsedCount)) {
            ESP_LOGW(MODBUS_TAG, "Failed to parse response");
            continue;
        }

        // Success
        lastErrorCode = 0;
        ESP_LOGD(MODBUS_TAG, "✅ Successfully read %u registers from address 0x%04X", count, startAddr);
        return true;
    }

    // All retries exhausted
    ESP_LOGE(MODBUS_TAG, "❌ Failed to read registers after %u attempts: %s", MODBUS_RETRY_COUNT, lastErrorMsg);
    return false;
}

bool ModbusRTUDriver::readHoldingRegisters(uint16_t startAddr, uint16_t count, float* outValues) {
    if (!initialized) {
        setError(10, "Driver not initialized");
        return false;
    }

    if (count == 0 || count > 125) {  // Modbus limit: max 125 registers per request
        setError(11, "Invalid register count: %u (must be 1-125)", count);
        return false;
    }

    if (!outValues) {
        setError(12, "Output buffer is NULL");
        return false;
    }

    // Retry loop
    for (uint8_t attempt = 0; attempt < MODBUS_RETRY_COUNT; attempt++) {
        if (attempt > 0) {
            // Exponential backoff delay
            uint32_t delayMs = MODBUS_RETRY_DELAY_MS * (1 << (attempt - 1));
            ESP_LOGD(MODBUS_TAG, "Retry %u/%u after %u ms", attempt, MODBUS_RETRY_COUNT, delayMs);
            delay(delayMs);
        }

        // Build request frame (Function 0x03)
        uint8_t requestFrame[16];
        uint16_t requestLen = 0;

        if (!buildReadHoldingRegistersRequest(getSlaveAddress(), startAddr, count, requestFrame, &requestLen)) {
            ESP_LOGW(MODBUS_TAG, "Failed to build request frame");
            continue;
        }

        // Send request
        if (!sendModbusRequest(requestFrame, requestLen)) {
            ESP_LOGW(MODBUS_TAG, "Failed to send request");
            continue;
        }

        // Receive response
        uint8_t responseFrame[256];
        uint16_t responseLen = 0;

        if (!receiveModbusResponse(responseFrame, sizeof(responseFrame), &responseLen)) {
            // Only log warning on last attempt to reduce spam during scanning
            if (attempt == MODBUS_RETRY_COUNT - 1) {
                ESP_LOGW(MODBUS_TAG, "Failed to receive response (attempt %u/%u)", attempt + 1, MODBUS_RETRY_COUNT);
            }
            continue;
        }

        // Parse response
        uint16_t parsedCount = 0;
        if (!parseReadHoldingRegistersResponse(responseFrame, responseLen, outValues, &parsedCount)) {
            ESP_LOGW(MODBUS_TAG, "Failed to parse response");
            continue;
        }

        // Success
        lastErrorCode = 0;
        ESP_LOGD(MODBUS_TAG, "✅ Successfully read %u holding registers from address 0x%04X", count, startAddr);
        return true;
    }

    // All retries exhausted
    ESP_LOGE(MODBUS_TAG, "❌ Failed to read holding registers after %u attempts: %s", MODBUS_RETRY_COUNT, lastErrorMsg);
    return false;
}

// ============================================================================
// UART COMMUNICATION
// ============================================================================

bool ModbusRTUDriver::sendModbusRequest(const uint8_t* request, uint16_t length) {
    if (!request || length == 0 || length > 256) {
        setError(20, "Invalid request buffer");
        return false;
    }

    // Clear RX buffer to remove any stale data
    uart_flush_input(RS485_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Switch to transmitter mode
    enableTransmitter();
    vTaskDelay(pdMS_TO_TICKS(1));  // Wait 1ms for RS485 driver to stabilize

    // Send data
    int bytesWritten = uart_write_bytes(RS485_UART_NUM, (const char*)request, length);

    // Wait for transmission to complete
    uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));

    // Switch back to receiver mode (allow time for driver to disable)
    vTaskDelay(pdMS_TO_TICKS(1));
    enableReceiver();
    vTaskDelay(pdMS_TO_TICKS(2));  // Wait before reading response

    if (bytesWritten != length) {
        setError(21, "uart_write_bytes failed: wrote %d/%u bytes", bytesWritten, length);
        return false;
    }

    ESP_LOGV(MODBUS_TAG, "📤 Sent request (%u bytes): %02X %02X %02X %02X %02X %02X %02X %02X",
             length, request[0], request[1], request[2], request[3], 
             request[4], request[5], request[6], request[7]);
    
    ESP_LOGD(MODBUS_TAG, "📤 Frame sent: Slave=0x%02X, Func=0x%02X, Reg=0x%04X",
             request[0], request[1], (request[2] << 8) | request[3]);

    return true;
}

bool ModbusRTUDriver::receiveModbusResponse(uint8_t* response, uint16_t maxLength, uint16_t* outLength) {
    if (!response || !outLength || maxLength == 0) {
        setError(30, "Invalid response buffer");
        return false;
    }

    *outLength = 0;
    uint32_t startTime = millis();

    // ========================================================================
    // STEP 1: Wait for first 3 bytes (Slave, Func, ByteCount)
    // These allow us to calculate exact frame size
    // ========================================================================
    
    while (*outLength < 3 && millis() - startTime < MODBUS_RESPONSE_TIMEOUT_MS) {
        size_t available = uart_read_bytes(RS485_UART_NUM, response + *outLength, 
                                          3 - *outLength, pdMS_TO_TICKS(100));
        if (available > 0) {
            *outLength += available;
            startTime = millis();  // Reset timeout on each byte received
        }
    }

    if (*outLength < 3) {
        setError(31, "No response received (timeout > %u ms)", MODBUS_RESPONSE_TIMEOUT_MS);
        return false;
    }

    // ESP_LOGV(MODBUS_TAG, "📥 First 3 bytes: 0x%02X 0x%02X 0x%02X (ByteCount=%u)", 
    //          response[0], response[1], response[2], response[2]);

    // ========================================================================
    // STEP 2: Calculate exact frame size from ByteCount
    // Frame: [Slave:1] [Func:1] [ByteCount:1] [Data:ByteCount] [CRC:2]
    // Total = 5 + ByteCount
    // ========================================================================
    
    uint8_t byteCount = response[2];
    uint16_t expectedLength = 5 + byteCount;

    if (expectedLength > maxLength) {
        setError(32, "Response too long: expected %u bytes, buffer only %u", expectedLength, maxLength);
        return false;
    }

    if (expectedLength < 5) {
        setError(33, "Invalid byte count: %u (response too short)", byteCount);
        return false;
    }

    // ========================================================================
    // STEP 3: Read remaining data with inter-byte timeout
    // ========================================================================
    
    uint32_t lastByteTime = millis();
    while (*outLength < expectedLength) {
        if (millis() - lastByteTime > 100) {
            // Timeout waiting for more bytes
            break;
        }

        size_t available = uart_read_bytes(RS485_UART_NUM, response + *outLength, 
                                          expectedLength - *outLength, pdMS_TO_TICKS(50));
        if (available > 0) {
            *outLength += available;
            lastByteTime = millis();
        }
    }

    // ========================================================================
    // STEP 4: Verify we got complete frame
    // ========================================================================
    
    if (*outLength != expectedLength) {
        setError(34, "Incomplete response: got %u bytes, expected %u", *outLength, expectedLength);
        return false;
    }

    ESP_LOGV(MODBUS_TAG, "📥 Received response (%u bytes): %02X %02X %02X %02X %02X %02X %02X %02X ...",
             *outLength, response[0], response[1], response[2], response[3],
             response[4], response[5], response[6], response[7]);

    return true;
}

// ============================================================================
// MODBUS FRAME BUILDING
// ============================================================================

bool ModbusRTUDriver::buildReadInputRegistersRequest(
    uint16_t slaveAddr,
    uint16_t startAddr,
    uint16_t regCount,
    uint8_t* outRequest,
    uint16_t* outLength) {
    
    if (!outRequest || !outLength) {
        return false;
    }

    // Frame structure:
    // [0]      Slave Address
    // [1]      Function Code (0x04)
    // [2-3]    Starting Address (big-endian)
    // [4-5]    Quantity of Registers (big-endian)
    // [6-7]    CRC-16 (little-endian)

    uint8_t frame[12];
    uint16_t frameLen = 0;

    frame[frameLen++] = (uint8_t)(slaveAddr & 0xFF);
    frame[frameLen++] = MODBUS_FUNC_READ_INPUT;
    frame[frameLen++] = (uint8_t)((startAddr >> 8) & 0xFF);
    frame[frameLen++] = (uint8_t)(startAddr & 0xFF);
    frame[frameLen++] = (uint8_t)((regCount >> 8) & 0xFF);
    frame[frameLen++] = (uint8_t)(regCount & 0xFF);

    // Calculate CRC
    uint16_t crc = calculateCRC16(frame, frameLen);
    frame[frameLen++] = (uint8_t)(crc & 0xFF);
    frame[frameLen++] = (uint8_t)((crc >> 8) & 0xFF);

    // Copy to output
    memcpy(outRequest, frame, frameLen);
    *outLength = frameLen;

    return true;
}

bool ModbusRTUDriver::buildReadHoldingRegistersRequest(
    uint16_t slaveAddr,
    uint16_t startAddr,
    uint16_t regCount,
    uint8_t* outRequest,
    uint16_t* outLength) {
    
    if (!outRequest || !outLength) {
        return false;
    }

    // Frame structure (Function Code 0x03):
    // [0]      Slave Address
    // [1]      Function Code (0x03)
    // [2-3]    Starting Address (big-endian)
    // [4-5]    Quantity of Registers (big-endian)
    // [6-7]    CRC-16 (little-endian)

    uint8_t frame[12];
    uint16_t frameLen = 0;

    frame[frameLen++] = (uint8_t)(slaveAddr & 0xFF);
    frame[frameLen++] = 0x03;  // Function Code: Read Holding Registers
    frame[frameLen++] = (uint8_t)((startAddr >> 8) & 0xFF);
    frame[frameLen++] = (uint8_t)(startAddr & 0xFF);
    frame[frameLen++] = (uint8_t)((regCount >> 8) & 0xFF);
    frame[frameLen++] = (uint8_t)(regCount & 0xFF);

    // Calculate CRC
    uint16_t crc = calculateCRC16(frame, frameLen);
    frame[frameLen++] = (uint8_t)(crc & 0xFF);
    frame[frameLen++] = (uint8_t)((crc >> 8) & 0xFF);

    // Copy to output
    memcpy(outRequest, frame, frameLen);
    *outLength = frameLen;

    return true;
}

bool ModbusRTUDriver::parseReadInputRegistersResponse(
    const uint8_t* response,
    uint16_t length,
    float* outValues,
    uint16_t* outCount) {
    
    if (!response || !outValues || !outCount || length < 7) {
        setError(40, "Invalid response parameters");
        return false;
    }

    *outCount = 0;

    // Check slave address
    if (response[0] != (getSlaveAddress() & 0xFF)) {
        setError(41, "Slave address mismatch: got 0x%02X, expected 0x%02X", 
                 response[0], getSlaveAddress() & 0xFF);
        return false;
    }

    // Check for exception
    if (isModbusException(response, length)) {
        uint8_t exceptionCode = response[2];
        setError(42, "Modbus exception: 0x%02X", exceptionCode);
        return false;
    }

    // Check function code
    if (response[1] != MODBUS_FUNC_READ_INPUT) {
        setError(43, "Function code mismatch: got 0x%02X, expected 0x%02X",
                 response[1], MODBUS_FUNC_READ_INPUT);
        return false;
    }

    // Check byte count
    uint8_t byteCount = response[2];
    if (length != byteCount + 5) {  // 1(addr) + 1(func) + 1(count) + count + 2(CRC)
        setError(44, "Response length mismatch: got %u, expected %u", 
                 length, byteCount + 5);
        return false;
    }

    // Verify CRC
    if (!verifyCRC16(response, length)) {
        setError(45, "CRC verification failed");
        return false;
    }

    // Extract register values (big-endian 16-bit integers)
    uint16_t regCount = byteCount / 2;
    for (uint16_t i = 0; i < regCount; i++) {
        uint16_t value = ((uint16_t)response[3 + i * 2] << 8) | response[3 + i * 2 + 1];
        outValues[i] = (float)value;  // Convert to float
    }

    *outCount = regCount;
    return true;
}

bool ModbusRTUDriver::parseReadHoldingRegistersResponse(
    const uint8_t* response,
    uint16_t length,
    float* outValues,
    uint16_t* outCount) {
    
    if (!response || !outValues || !outCount || length < 7) {
        setError(40, "Invalid response parameters");
        return false;
    }

    *outCount = 0;

    // Check slave address
    if (response[0] != (getSlaveAddress() & 0xFF)) {
        setError(41, "Slave address mismatch: got 0x%02X, expected 0x%02X", 
                 response[0], getSlaveAddress() & 0xFF);
        return false;
    }

    // Check for exception
    if (isModbusException(response, length)) {
        uint8_t exceptionCode = response[2];
        setError(42, "Modbus exception: 0x%02X", exceptionCode);
        return false;
    }

    // Check function code (should be 0x03)
    if (response[1] != 0x03) {
        setError(43, "Function code mismatch: got 0x%02X, expected 0x03",
                 response[1]);
        return false;
    }

    // Check byte count
    uint8_t byteCount = response[2];
    if (length != byteCount + 5) {  // 1(addr) + 1(func) + 1(count) + count + 2(CRC)
        setError(44, "Response length mismatch: got %u, expected %u", 
                 length, byteCount + 5);
        return false;
    }

    // Verify CRC
    if (!verifyCRC16(response, length)) {
        setError(45, "CRC verification failed");
        return false;
    }

    // Extract register values (big-endian 16-bit integers)
    uint16_t regCount = byteCount / 2;
    for (uint16_t i = 0; i < regCount; i++) {
        uint16_t value = ((uint16_t)response[3 + i * 2] << 8) | response[3 + i * 2 + 1];
        outValues[i] = (float)value;  // Convert to float
    }

    *outCount = regCount;
    return true;
}

// ============================================================================
// CRC-16 CALCULATION
// ============================================================================

uint16_t ModbusRTUDriver::calculateCRC16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];

        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ MODBUS_CRC_POLY;
            } else {
                crc = crc >> 1;
            }
        }
    }

    return crc;
}

bool ModbusRTUDriver::verifyCRC16(const uint8_t* data, uint16_t length) {
    if (length < 3) return false;

    // Extract CRC from message (little-endian at end)
    uint16_t receivedCRC = ((uint16_t)data[length - 1] << 8) | data[length - 2];

    // Calculate CRC on data portion (excluding CRC bytes)
    uint16_t calculatedCRC = calculateCRC16(data, length - 2);

    return receivedCRC == calculatedCRC;
}

// ============================================================================
// MODBUS EXCEPTION HANDLING
// ============================================================================

bool ModbusRTUDriver::isModbusException(const uint8_t* response, uint16_t length) {
    if (length < 3) return false;
    // Exception response: function code | 0x80
    return (response[1] & 0x80) != 0;
}

// ============================================================================
// ERROR HANDLING
// ============================================================================

const char* ModbusRTUDriver::getLastError() {
    return lastErrorMsg;
}

uint8_t ModbusRTUDriver::getLastErrorCode() {
    return lastErrorCode;
}

void ModbusRTUDriver::setSlaveAddress(uint16_t address) {
    slaveAddress = address;
    if (address == 0) {
        ESP_LOGV(MODBUS_TAG, "Slave address restored to default: 0x%04X", MODBUS_SLAVE_ADDRESS);
    } else {
        ESP_LOGV(MODBUS_TAG, "Slave address set to: 0x%04X", address);
    }
}

uint16_t ModbusRTUDriver::getSlaveAddress() {
    return (slaveAddress == 0) ? MODBUS_SLAVE_ADDRESS : slaveAddress;
}

void ModbusRTUDriver::setError(uint8_t code, const char* format, ...) {
    lastErrorCode = code;

    va_list args;
    va_start(args, format);
    vsnprintf(lastErrorMsg, sizeof(lastErrorMsg), format, args);
    va_end(args);

    ESP_LOGD(MODBUS_TAG, "Error [%u]: %s", code, lastErrorMsg);
}
