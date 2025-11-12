#include "cellular_uart.h"
#include <esp_log.h>

const char* CellularUART::TAG = "CELLULAR_UART";

CellularUART::CellularUART()
    : m_config()
    , m_serial(nullptr)
    , m_powerState(PowerState::UNKNOWN)
{
}

CellularUART::CellularUART(const Config& config)
    : m_config(config)
    , m_serial(nullptr)
    , m_powerState(PowerState::UNKNOWN)
{
}

CellularUART::~CellularUART() {
    if (m_serial) {
        m_serial->end();
        delete m_serial;
    }
}

bool CellularUART::initialize() {
    ESP_LOGI(TAG, "Initializing UART%d (TX=%d, RX=%d, PWR=%d, Baud=%d)",
             m_config.uartNum, m_config.txPin, m_config.rxPin, 
             m_config.pwrPin, m_config.baudRate);

    // Initialize power pin
    pinMode(m_config.pwrPin, OUTPUT);
    digitalWrite(m_config.pwrPin, LOW);  // Ensure module is OFF initially
    m_powerState = PowerState::OFF;

    // Initialize network status pin (optional)
    if (m_config.netPin != 255) {  // -1 cast to uint8_t = 255
        pinMode(m_config.netPin, INPUT);
        ESP_LOGI(TAG, "Network status pin (NET=%d) configured", m_config.netPin);
    }

    // Create HardwareSerial instance
    m_serial = new HardwareSerial(m_config.uartNum);
    if (!m_serial) {
        ESP_LOGE(TAG, "Failed to create HardwareSerial instance");
        return false;
    }

    // Initialize UART with TX and RX pins
    m_serial->begin(m_config.baudRate, SERIAL_8N1, m_config.rxPin, m_config.txPin);
    
    // Wait for UART to stabilize
    delay(100);

    ESP_LOGI(TAG, "UART initialized successfully");
    return true;
}

bool CellularUART::powerOn(uint32_t delayMs) {
    if (m_powerState == PowerState::ON) {
        ESP_LOGW(TAG, "Module already powered ON");
        return true;
    }

    ESP_LOGI(TAG, "Powering ON module (delay=%dms)...", delayMs);
    
    // A7682S Power-on sequence:
    // 1. Set PWR pin HIGH for at least 500ms
    // 2. Wait for module to boot
    digitalWrite(m_config.pwrPin, HIGH);
    delay(delayMs);

    m_powerState = PowerState::ON;
    
    // Clear any garbage in RX buffer
    flush();

    ESP_LOGI(TAG, "Module powered ON");
    return true;
}

bool CellularUART::powerOff(uint32_t delayMs) {
    if (m_powerState == PowerState::OFF) {
        ESP_LOGW(TAG, "Module already powered OFF");
        return true;
    }

    ESP_LOGI(TAG, "Powering OFF module (delay=%dms)...", delayMs);
    
    // A7682S Power-off sequence:
    // 1. Set PWR pin LOW
    digitalWrite(m_config.pwrPin, LOW);
    delay(delayMs);

    m_powerState = PowerState::OFF;

    ESP_LOGI(TAG, "Module powered OFF");
    return true;
}

CellularUART::PowerState CellularUART::getPowerState() const {
    return m_powerState;
}

size_t CellularUART::write(const uint8_t* data, size_t length) {
    if (!m_serial) {
        ESP_LOGE(TAG, "UART not initialized");
        return 0;
    }

    size_t written = m_serial->write(data, length);
    
    // // Debug log (only first 64 bytes to avoid spam)
    // if (length <= 64) {
    //     char hexStr[length * 3 + 1];
    //     for (size_t i = 0; i < length; i++) {
    //         sprintf(&hexStr[i * 3], "%02X ", data[i]);
    //     }
    //     ESP_LOGD(TAG, "TX[%d]: %s", length, hexStr);
    // } else {
    //     ESP_LOGD(TAG, "TX[%d]: <data too long, truncated>", length);
    // }

    return written;
}

size_t CellularUART::write(const String& str) {
    return write((const uint8_t*)str.c_str(), str.length());
}

size_t CellularUART::read(uint8_t* buffer, size_t maxLength) {
    if (!m_serial) {
        ESP_LOGE(TAG, "UART not initialized");
        return 0;
    }

    size_t available = m_serial->available();
    if (available == 0) {
        return 0;
    }

    size_t toRead = (available > maxLength) ? maxLength : available;
    size_t bytesRead = m_serial->readBytes(buffer, toRead);

    return bytesRead;
}

size_t CellularUART::readLine(char* buffer, size_t maxLength, uint32_t timeoutMs) {
    if (!m_serial) {
        ESP_LOGE(TAG, "UART not initialized");
        return 0;
    }

    uint32_t startTime = millis();
    size_t index = 0;

    while ((millis() - startTime) < timeoutMs) {
        if (m_serial->available()) {
            char c = m_serial->read();
            
            // Check for line endings
            if (c == '\n') {
                buffer[index] = '\0';
                return index;
            } else if (c == '\r') {
                // Skip '\r', wait for '\n'
                continue;
            } else {
                if (index < maxLength - 1) {
                    buffer[index++] = c;
                } else {
                    // Buffer full
                    buffer[maxLength - 1] = '\0';
                    return maxLength - 1;
                }
            }
        }
        delay(1);  // Yield to other tasks
    }

    // Timeout reached
    buffer[index] = '\0';
    return index;
}

String CellularUART::readUntilTimeout(uint32_t timeoutMs) {
    String result = "";
    uint32_t startTime = millis();

    while ((millis() - startTime) < timeoutMs) {
        if (m_serial->available()) {
            result += (char)m_serial->read();
            startTime = millis();  // Reset timeout on each byte
        }
        delay(1);
    }

    return result;
}

int CellularUART::available() const {
    if (!m_serial) {
        return 0;
    }
    return m_serial->available();
}

void CellularUART::flush() {
    if (m_serial) {
        // Read and discard all available data
        while (m_serial->available()) {
            m_serial->read();
        }
    }
}

bool CellularUART::isNetworkConnected() const {
    if (m_config.netPin == 255) {  // -1 cast to uint8_t
        ESP_LOGW(TAG, "Network status pin not configured");
        return false;
    }

    // A7682S NET pin: HIGH when network registered
    return digitalRead(m_config.netPin) == HIGH;
}
