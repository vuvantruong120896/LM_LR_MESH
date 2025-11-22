#include "../include/modbus_async.h"
#include "esp_log.h"
#include "rom/ets_sys.h" // For ets_delay_us
#include <cstring>

const char* ModbusAsync::TAG = "ModbusAsync";

// ============================================================================
// SINGLETON
// ============================================================================

ModbusAsync& ModbusAsync::getInstance() {
    static ModbusAsync instance;
    return instance;
}

ModbusAsync::ModbusAsync()
    : m_initialized(false),
      m_receiverTaskHandle(nullptr),
      m_mutex(nullptr),
      m_nextTransactionId(1) {
}

ModbusAsync::~ModbusAsync() {
    if (m_receiverTaskHandle) {
        vTaskDelete(m_receiverTaskHandle);
        m_receiverTaskHandle = nullptr;
    }
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
    if (m_initialized) {
        uart_driver_delete(RS485_UART_NUM);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ModbusAsync::initialize() {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
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

    // Install UART driver with proper buffer sizes
    // RX: 512 bytes (can buffer multiple frames)
    // TX: 256 bytes (enables non-blocking writes)
    esp_err_t err = uart_driver_install(RS485_UART_NUM, 512, 256, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_param_config(RS485_UART_NUM, &uartConfig);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    err = uart_set_pin(RS485_UART_NUM, RS485_TX_PIN, RS485_RX_PIN, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    // Configure DE pin for RS485 direction control
    gpio_config_t deConfig = {};
    deConfig.pin_bit_mask = (1ULL << RS485_DE_PIN);
    deConfig.mode = GPIO_MODE_OUTPUT;
    deConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    deConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    deConfig.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&deConfig);
    gpio_set_level((gpio_num_t)RS485_DE_PIN, 0); // Start in receiver mode

    // Flush any garbage in UART buffer
    uart_flush(RS485_UART_NUM);

    // Create mutex for transaction map access
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    // Create receiver task on Core 1 with high priority
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        receiverTaskWrapper,
        "modbus_rx",
        4096,              // Stack size
        this,              // Parameter
        5,                 // Priority (high - above sensor task)
        &m_receiverTaskHandle,
        1                  // Core 1
    );

    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create receiver task");
        vSemaphoreDelete(m_mutex);
        uart_driver_delete(RS485_UART_NUM);
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "✅ Initialized (UART=%d, Baud=%u, RxTask=Core1/Pri5)",
             RS485_UART_NUM, MODBUS_BAUD_RATE);
    
    return true;
}

// ============================================================================
// PUBLIC API - ASYNC REQUESTS
// ============================================================================

uint32_t ModbusAsync::readInputRegistersAsync(uint8_t slaveAddr, uint16_t startAddr, uint16_t count) {
    return sendRequestAsync(slaveAddr, 0x04, startAddr, count);
}

uint32_t ModbusAsync::readHoldingRegistersAsync(uint8_t slaveAddr, uint16_t startAddr, uint16_t count) {
    return sendRequestAsync(slaveAddr, 0x03, startAddr, count);
}

uint32_t ModbusAsync::writeSingleRegisterAsync(uint8_t slaveAddr, uint16_t regAddr, uint16_t value) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return 0;
    }

    // Build Modbus frame: [Addr][FC=0x06][RegAddr_H][RegAddr_L][Value_H][Value_L][CRC_L][CRC_H]
    uint8_t frame[8];
    frame[0] = slaveAddr;
    frame[1] = 0x06;  // ✅ Write Single Register (FC 0x06)
    frame[2] = (regAddr >> 8) & 0xFF;
    frame[3] = regAddr & 0xFF;
    frame[4] = (value >> 8) & 0xFF;
    frame[5] = value & 0xFF;
    
    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    // Create transaction
    uint32_t txId = m_nextTransactionId++;
    if (m_nextTransactionId == 0) m_nextTransactionId = 1; // Avoid 0

    Transaction tx;
    tx.id = txId;
    tx.timestamp = millis();
    tx.slaveAddr = slaveAddr;
    tx.funcCode = 0x06;
    tx.startAddr = regAddr;
    tx.count = 1;
    tx.expectedResponseLen = 8; // Same as request for write
    tx.state = Transaction::PENDING;
    tx.errorCode = 0;

    // Add to map
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        m_transactions[txId] = tx;
        xSemaphoreGive(m_mutex);
    } else {
        ESP_LOGE(TAG, "Mutex timeout when creating write transaction");
        return 0;
    }

    // Send the frame
    sendRawFrame(frame, 8);

    ESP_LOGD(TAG, "📤 Write Reg TX[%u]: Addr=0x%02X Reg=0x%04X Val=0x%04X",
             txId, slaveAddr, regAddr, value);
    
    return txId;
}

// ============================================================================
// PUBLIC API - RESULT POLLING
// ============================================================================

bool ModbusAsync::isTransactionComplete(uint32_t transactionId) {
    if (transactionId == 0) return true;

    bool complete = false;
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto it = m_transactions.find(transactionId);
        if (it != m_transactions.end()) {
            complete = (it->second.state != Transaction::PENDING);
        } else {
            complete = true; // Transaction not found = completed/cleaned up
        }
        xSemaphoreGive(m_mutex);
    }
    return complete;
}

ModbusAsync::Transaction::State ModbusAsync::getTransactionState(uint32_t transactionId) {
    if (transactionId == 0) return ModbusAsync::Transaction::ERROR;

    ModbusAsync::Transaction::State state = ModbusAsync::Transaction::ERROR;
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto it = m_transactions.find(transactionId);
        if (it != m_transactions.end()) {
            state = it->second.state;
        }
        xSemaphoreGive(m_mutex);
    }
    return state;
}

int ModbusAsync::getResult(uint32_t transactionId, float* outValues, uint16_t maxCount) {
    if (transactionId == 0 || !outValues || maxCount == 0) {
        return -1;
    }

    int result = -1;
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto it = m_transactions.find(transactionId);
        if (it != m_transactions.end()) {
            const Transaction& tx = it->second;
            
            if (tx.state == Transaction::COMPLETED && !tx.responseData.empty()) {
                // Extract register values from response
                // Format: [Addr][FC][ByteCount][Data...][CRC]
                if (tx.responseData.size() >= 3) {
                    uint8_t byteCount = tx.responseData[2];
                    uint16_t numRegs = byteCount / 2;
                    
                    for (uint16_t i = 0; i < numRegs && i < maxCount; i++) {
                        uint16_t regValue = (tx.responseData[3 + i*2] << 8) | tx.responseData[4 + i*2];
                        outValues[i] = (float)regValue;
                    }
                    result = (numRegs <= maxCount) ? numRegs : maxCount;
                }
            } else if (tx.state == Transaction::ERROR || tx.state == Transaction::TIMEOUT) {
                result = -1;
            }
        }
        xSemaphoreGive(m_mutex);
    }
    return result;
}

// ============================================================================
// INTERNAL - REQUEST BUILDING
// ============================================================================

uint32_t ModbusAsync::sendRequestAsync(uint8_t slaveAddr, uint8_t funcCode, uint16_t startAddr, uint16_t count) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return 0;
    }

    // Build Modbus frame: [Addr][FC][StartAddr_H][StartAddr_L][Count_H][Count_L][CRC_L][CRC_H]
    uint8_t frame[8];
    frame[0] = slaveAddr;
    frame[1] = funcCode;
    frame[2] = (startAddr >> 8) & 0xFF;
    frame[3] = startAddr & 0xFF;
    frame[4] = (count >> 8) & 0xFF;
    frame[5] = count & 0xFF;
    
    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    // Create transaction
    uint32_t txId = m_nextTransactionId++;
    if (m_nextTransactionId == 0) m_nextTransactionId = 1; // Avoid 0

    Transaction tx;
    tx.id = txId;
    tx.timestamp = millis();
    tx.slaveAddr = slaveAddr;
    tx.funcCode = funcCode;
    tx.startAddr = startAddr;
    tx.count = count;
    tx.expectedResponseLen = calculateExpectedResponseLength(funcCode, count);
    tx.state = Transaction::PENDING;
    tx.errorCode = 0;

    // Add to map
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        m_transactions[txId] = tx;
        xSemaphoreGive(m_mutex);
    } else {
        ESP_LOGE(TAG, "Mutex timeout when creating transaction");
        return 0;
    }

    // Send the frame
    sendRawFrame(frame, 8);

    ESP_LOGD(TAG, "📤 Read TX[%u]: Addr=0x%02X FC=0x%02X Start=0x%04X Count=%u",
             txId, slaveAddr, funcCode, startAddr, count);
    
    return txId;
}

uint16_t ModbusAsync::calculateExpectedResponseLength(uint8_t funcCode, uint16_t count) {
    switch (funcCode) {
        case 0x03: // Read Holding Registers
        case 0x04: // Read Input Registers
            return 5 + (count * 2); // [Addr][FC][ByteCount][Data...][CRC(2)]
        case 0x06: // Write Single Register
            return 8; // Echo request
        default:
            return 0;
    }
}

// ============================================================================
// INTERNAL - UART TRANSMISSION
// ============================================================================

void ModbusAsync::sendRawFrame(const uint8_t* frame, uint16_t len) {
    if (!frame || len == 0) return;

    // Print frame content in hex
    ESP_LOGI(TAG, "📤 Sending frame (%u bytes):", len);
    // for (uint16_t i = 0; i < len; i += 8) {
    //     char hexBuf[64];
    //     int offset = 0;
    //     offset += snprintf(hexBuf + offset, sizeof(hexBuf) - offset, "   [%02u] ", i);
    //     for (uint16_t j = i; j < i + 8 && j < len; j++) {
    //         offset += snprintf(hexBuf + offset, sizeof(hexBuf) - offset, "%02X ", frame[j]);
    //     }
    //     ESP_LOGI(TAG, "%s", hexBuf);
    // }

    // Switch to transmit mode
    gpio_set_level((gpio_num_t)RS485_DE_PIN, 1);

    // Flush RX buffer to remove any stale data or echo
    uart_flush_input(RS485_UART_NUM);

    // Write data (non-blocking with TX buffer)
    uart_write_bytes(RS485_UART_NUM, frame, len);

    // Wait for transmission to complete (increased timeout)
    uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(50));

    // Switch back to receive mode
    gpio_set_level((gpio_num_t)RS485_DE_PIN, 0);

    ESP_LOGV(TAG, "📤 Sent %u bytes", len);
}

// ============================================================================
// RECEIVER TASK
// ============================================================================

void ModbusAsync::receiverTaskWrapper(void* param) {
    ModbusAsync* self = static_cast<ModbusAsync*>(param);
    self->receiverTask();
}

void ModbusAsync::receiverTask() {
    ESP_LOGI(TAG, "🎧 Receiver task started on Core %d", xPortGetCoreID());

    std::vector<uint8_t> frameBuffer;
    frameBuffer.reserve(64); // Pre-allocate for typical frame size
    
    uint32_t lastByteTime = 0;
    const uint32_t INTER_FRAME_SILENCE_MS = 5; // 5ms silence = frame complete (faster detection)

    while (true) {
        uint8_t byte;
        int len = uart_read_bytes(RS485_UART_NUM, &byte, 1, pdMS_TO_TICKS(50));

        if (len > 0) {
            // Got a byte
            uint32_t now = millis();

            // Check if this is start of new frame (silence detected)
            if (!frameBuffer.empty() && (now - lastByteTime) > INTER_FRAME_SILENCE_MS) {
                // Process previous frame
                processReceivedFrame(frameBuffer);
                frameBuffer.clear();
            }

            frameBuffer.push_back(byte);
            lastByteTime = now;

        } else {
            // No data received
            // Check if we have a pending frame that's complete (silence timeout)
            if (!frameBuffer.empty()) {
                uint32_t now = millis();
                if ((now - lastByteTime) > INTER_FRAME_SILENCE_MS) {
                    processReceivedFrame(frameBuffer);
                    frameBuffer.clear();
                    cleanupOldTransactions();
                }
            }

            // Yield to other tasks when idle to prevent CPU starvation
            vTaskDelay(pdMS_TO_TICKS(10));
        }

    }
}

void ModbusAsync::processReceivedFrame(const std::vector<uint8_t>& frame) {
    if (frame.size() < 5) {
        ESP_LOGV(TAG, "📥 Frame too short: %u bytes", frame.size());
        return;
    }

    // Debug: Dump frame
    ESP_LOGD(TAG, "📥 Frame dump (%u bytes):", frame.size());
    for (size_t i = 0; i < frame.size(); i += 8) {
        char hexBuf[128];
        int offset = 0;
        offset += snprintf(hexBuf + offset, sizeof(hexBuf) - offset, "   [%02zu] ", i);
        for (size_t j = i; j < i + 8 && j < frame.size(); j++) {
            offset += snprintf(hexBuf + offset, sizeof(hexBuf) - offset, "%02X ", frame[j]);
        }
        ESP_LOGD(TAG, "%s", hexBuf);
    }

    // Validate CRC
    uint16_t receivedCRC = frame[frame.size() - 2] | (frame[frame.size() - 1] << 8);
    uint16_t calculatedCRC = calculateCRC(frame.data(), frame.size() - 2);

    if (receivedCRC != calculatedCRC) {
        ESP_LOGW(TAG, "❌ CRC mismatch: received=0x%04X calculated=0x%04X", 
                 receivedCRC, calculatedCRC);
        ESP_LOGW(TAG, "   CRC bytes: [%zu]=0x%02X [%zu]=0x%02X",
                 frame.size() - 2, frame[frame.size() - 2],
                 frame.size() - 1, frame[frame.size() - 1]);
        return;
    }

    uint8_t slaveAddr = frame[0];
    uint8_t funcCode = frame[1];

    ESP_LOGD(TAG, "📥 Valid frame: Addr=0x%02X FC=0x%02X Len=%u", 
             slaveAddr, funcCode, frame.size());

    // Find matching pending transaction
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (auto& entry : m_transactions) {
            Transaction& tx = entry.second;
            
            if (tx.state == Transaction::PENDING &&
                tx.slaveAddr == slaveAddr &&
                tx.funcCode == funcCode) {
                
                // Match found!
                tx.responseData.assign(frame.begin(), frame.end());
                tx.state = Transaction::COMPLETED;
                
                ESP_LOGI(TAG, "✅ TX[%u] completed: %u bytes received", 
                         tx.id, frame.size());
                break;
            }
        }
        xSemaphoreGive(m_mutex);
    }
}

void ModbusAsync::cleanupOldTransactions() {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint32_t now = millis();
        
        auto it = m_transactions.begin();
        while (it != m_transactions.end()) {
            Transaction& tx = it->second;
            
            // Timeout pending transactions after 2 seconds
            if (tx.state == Transaction::PENDING && 
                (now - tx.timestamp) > TRANSACTION_TIMEOUT_MS) {
                
                ESP_LOGW(TAG, "⏱️ TX[%u] timeout after %ums", 
                         tx.id, now - tx.timestamp);
                tx.state = Transaction::TIMEOUT;
            }
            
            // Remove completed/error/timeout transactions after 5 seconds
            if (tx.state != Transaction::PENDING && 
                (now - tx.timestamp) > 5000) {
                it = m_transactions.erase(it);
            } else {
                ++it;
            }
        }
        
        xSemaphoreGive(m_mutex);
    }
}

// ============================================================================
// CRC CALCULATION
// ============================================================================

uint16_t ModbusAsync::calculateCRC(const uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}
