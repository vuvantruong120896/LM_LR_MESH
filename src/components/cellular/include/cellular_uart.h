#ifndef CELLULAR_UART_H
#define CELLULAR_UART_H

#include <Arduino.h>
#include <HardwareSerial.h>

/**
 * @brief UART Driver for A7682S Cellular Module
 * 
 * Hardware Configuration:
 * - TX Pin: IO40 (ESP32 TX -> A7682S RX)
 * - RX Pin: IO41 (ESP32 RX <- A7682S TX)
 * - PWR Pin: IO39 (Power Enable - active HIGH)
 * - NET Pin: IO38 (Network Status - optional)
 * - Baud Rate: 115200 (default for A7682S)
 * 
 * @note This driver handles low-level UART communication
 */
class CellularUART {
public:
    /**
     * @brief UART configuration
     */
    struct Config {
        uint8_t txPin;          ///< TX pin (ESP32 -> Module)
        uint8_t rxPin;          ///< RX pin (ESP32 <- Module)
        uint8_t pwrPin;         ///< Power enable pin
        uint8_t netPin;         ///< Network status pin (optional, -1 to disable)
        uint32_t baudRate;      ///< Baud rate (default: 115200)
        uint8_t uartNum;        ///< UART number (0, 1, 2)
        
        Config()
            : txPin(40)
            , rxPin(41)
            , pwrPin(39)
            , netPin(38)
            , baudRate(115200)
            , uartNum(1)  // UART1
        {}
    };

    /**
     * @brief Power state
     */
    enum class PowerState {
        OFF,
        ON,
        UNKNOWN
    };

    /**
     * @brief Constructor with default config
     */
    CellularUART();

    /**
     * @brief Constructor with custom config
     * @param config UART configuration
     */
    explicit CellularUART(const Config& config);

    /**
     * @brief Destructor
     */
    ~CellularUART();

    /**
     * @brief Initialize UART interface
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Power on the module
     * @param delayMs Delay after power on (default: 3000ms)
     * @return true if power on successful
     */
    bool powerOn(uint32_t delayMs = 3000);

    /**
     * @brief Power off the module
     * @param delayMs Delay after power off (default: 1000ms)
     * @return true if power off successful
     */
    bool powerOff(uint32_t delayMs = 1000);

    /**
     * @brief Get current power state
     * @return PowerState
     */
    PowerState getPowerState() const;

    /**
     * @brief Write data to UART
     * @param data Data to write
     * @param length Data length
     * @return Number of bytes written
     */
    size_t write(const uint8_t* data, size_t length);

    /**
     * @brief Write string to UART
     * @param str String to write
     * @return Number of bytes written
     */
    size_t write(const String& str);

    /**
     * @brief Read data from UART (non-blocking)
     * @param buffer Buffer to store data
     * @param maxLength Maximum bytes to read
     * @return Number of bytes read
     */
    size_t read(uint8_t* buffer, size_t maxLength);

    /**
     * @brief Read line from UART (blocking with timeout)
     * @param buffer Buffer to store line
     * @param maxLength Maximum bytes to read
     * @param timeoutMs Timeout in milliseconds
     * @return Number of bytes read (excluding newline)
     */
    size_t readLine(char* buffer, size_t maxLength, uint32_t timeoutMs = 1000);

    /**
     * @brief Read all available data until timeout
     * @param timeoutMs Timeout in milliseconds
     * @return String with all data
     */
    String readUntilTimeout(uint32_t timeoutMs = 1000);

    /**
     * @brief Check if data is available
     * @return Number of bytes available
     */
    int available() const;

    /**
     * @brief Clear RX buffer
     */
    void flush();

    /**
     * @brief Check network status pin
     * @return true if network connected (NET pin HIGH)
     */
    bool isNetworkConnected() const;

private:
    Config m_config;
    HardwareSerial* m_serial;
    PowerState m_powerState;
    
    static const char* TAG;
};

#endif // CELLULAR_UART_H
