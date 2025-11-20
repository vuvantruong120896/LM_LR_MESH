#ifndef _MODBUS_RTU_DRIVER_H
#define _MODBUS_RTU_DRIVER_H

#include <Arduino.h>
#include <driver/uart.h>
#include "rs485_config.h"

/**
 * @file modbus_rtu_driver.h
 * @brief Modbus RTU Driver for RS485 Communication
 * 
 * Implements Modbus RTU protocol for reading input registers from slave devices.
 * Handles:
 * - UART initialization with RS485 transceiver control
 * - Modbus request frame generation (Function Code 0x04)
 * - CRC-16 calculation and verification
 * - Request/response communication with timeout
 * - Error handling and retry logic
 * 
 * Shared component for both Node and Gateway applications
 */

class ModbusRTUDriver {
public:
    /**
     * @brief Initialize RS485 UART and GPIO
     * @return true if initialization successful, false otherwise
     */
    static bool initialize();

    /**
     * @brief Shutdown RS485 communication
     */
    static void shutdown();

    /**
     * @brief Read single 16-bit input register (Function Code 0x04)
     * @param registerAddr Starting register address
     * @return 16-bit register value, or -1 if failed
     */
    static int16_t readInputRegister(uint16_t registerAddr);

    /**
     * @brief Read multiple consecutive input registers (Function Code 0x04)
     * @param startAddr Starting register address
     * @param count Number of registers to read
     * @param outValues Output array to store float values (must have 'count' elements)
     * @return true if all registers read successfully, false if any failed
     * 
     * @note Registers are assumed to be stored as 16-bit integers and will be
     *       converted to floats. Adjust conversion logic based on actual sensor format.
     */
    static bool readInputRegisters(uint16_t startAddr, uint16_t count, float* outValues);

    /**
     * @brief Read single 16-bit holding register (Function Code 0x03)
     * @param registerAddr Starting register address
     * @return 16-bit register value, or -1 if failed
     */
    static int16_t readHoldingRegister(uint16_t registerAddr);

    /**
     * @brief Read multiple consecutive holding registers (Function Code 0x03)
     * @param startAddr Starting register address
     * @param count Number of registers to read
     * @param outValues Output array to store float values (must have 'count' elements)
     * @return true if all registers read successfully, false if any failed
     * 
     * @note Many soil sensors use Function Code 0x03 instead of 0x04
     * @note Registers are assumed to be stored as 16-bit integers and will be
     *       converted to floats. Adjust conversion logic based on actual sensor format.
     */
    static bool readHoldingRegisters(uint16_t startAddr, uint16_t count, float* outValues);

    /**
     * @brief Check if driver is initialized and ready
     * @return true if ready, false otherwise
     */
    static bool isReady();

    /**
     * @brief Get last error message
     * @return String describing last error
     */
    static const char* getLastError();

    /**
     * @brief Get last error code
     * @return Error code (0 = no error)
     */
    static uint8_t getLastErrorCode();

    /**
     * @brief Set Modbus slave address temporarily (for testing/scanning)
     * @param address Slave address (0x0001 - 0xFFFF)
     * @note This overrides MODBUS_SLAVE_ADDRESS from rs485_config.h
     * @note Use setSlaveAddress(0) to restore default address
     */
    static void setSlaveAddress(uint16_t address);

    /**
     * @brief Get current slave address
     * @return Current slave address being used
     */
    static uint16_t getSlaveAddress();

private:
    // ========================================================================
    // INTERNAL STATE
    // ========================================================================
    
    static bool initialized;
    static uint8_t lastErrorCode;
    static char lastErrorMsg[256];
    static uint16_t slaveAddress;  // Override slave address (0 = use default)

    // ========================================================================
    // UART CONTROL
    // ========================================================================

    /**
     * @brief Enable transmitter mode (set DE high)
     * @note Allows data to be sent on RS485 bus
     */
    static inline void enableTransmitter() {
        gpio_set_level((gpio_num_t)RS485_DE_PIN, 1);
        delayMicroseconds(10);  // RS485 turnaround delay
        // ESP_LOGV(MODBUS_TAG, "TX mode: GPIO%d = HIGH", RS485_DE_PIN);
    }

    /**
     * @brief Enable receiver mode (set DE low)
     * @note Allows data to be received from RS485 bus
     */
    static inline void enableReceiver() {
        gpio_set_level((gpio_num_t)RS485_DE_PIN, 0);
        delayMicroseconds(10);  // RS485 turnaround delay
        // ESP_LOGV(MODBUS_TAG, "RX mode: GPIO%d = LOW", RS485_DE_PIN);
    }

    // ========================================================================
    // MODBUS FRAME HANDLING
    // ========================================================================

    /**
     * @brief Send Modbus request frame over UART
     * @param request Pointer to request buffer
     * @param length Length of request data
     * @return true if sent successfully, false otherwise
     */
    static bool sendModbusRequest(const uint8_t* request, uint16_t length);

    /**
     * @brief Receive Modbus response frame from UART
     * @param response Pointer to response buffer
     * @param maxLength Maximum buffer size
     * @param outLength Output: actual length of response received
     * @return true if response received successfully, false on timeout/error
     */
    static bool receiveModbusResponse(uint8_t* response, uint16_t maxLength, uint16_t* outLength);

    /**
     * @brief Build Modbus RTU read input registers request frame
     * @param slaveAddr Modbus slave address
     * @param startAddr Starting register address
     * @param regCount Number of registers to read
     * @param outRequest Output buffer for request frame
     * @param outLength Output: length of generated request
     * @return true if frame generated successfully, false on error
     */
    static bool buildReadInputRegistersRequest(
        uint16_t slaveAddr,
        uint16_t startAddr,
        uint16_t regCount,
        uint8_t* outRequest,
        uint16_t* outLength
    );

    /**
     * @brief Build Modbus RTU read holding registers request frame (Function 0x03)
     * @param slaveAddr Modbus slave address
     * @param startAddr Starting register address
     * @param regCount Number of registers to read
     * @param outRequest Output buffer for request frame
     * @param outLength Output: length of generated request
     * @return true if frame generated successfully, false on error
     */
    static bool buildReadHoldingRegistersRequest(
        uint16_t slaveAddr,
        uint16_t startAddr,
        uint16_t regCount,
        uint8_t* outRequest,
        uint16_t* outLength
    );

    /**
     * @brief Parse Modbus RTU response frame and extract register values
     * @param response Response buffer
     * @param length Length of response
     * @param outValues Output array for parsed values
     * @param outCount Output: number of values parsed
     * @return true if parsed successfully, false on error (CRC, format, etc)
     */
    static bool parseReadInputRegistersResponse(
        const uint8_t* response,
        uint16_t length,
        float* outValues,
        uint16_t* outCount
    );

    /**
     * @brief Parse Modbus RTU holding registers response frame and extract values
     * @param response Response buffer
     * @param length Length of response
     * @param outValues Output array for parsed values
     * @param outCount Output: number of values parsed
     * @return true if parsed successfully, false on error (CRC, format, etc)
     */
    static bool parseReadHoldingRegistersResponse(
        const uint8_t* response,
        uint16_t length,
        float* outValues,
        uint16_t* outCount
    );

    // ========================================================================
    // CRC-16 CALCULATION
    // ========================================================================

    /**
     * @brief Calculate CRC-16 for Modbus RTU
     * @param data Pointer to data buffer
     * @param length Length of data in bytes
     * @return 16-bit CRC value
     */
    static uint16_t calculateCRC16(const uint8_t* data, uint16_t length);

    /**
     * @brief Verify CRC-16 in received message
     * @param data Pointer to message data (including CRC bytes at end)
     * @param length Total length including CRC bytes
     * @return true if CRC is valid, false otherwise
     */
    static bool verifyCRC16(const uint8_t* data, uint16_t length);

    // ========================================================================
    // ERROR HANDLING
    // ========================================================================

    /**
     * @brief Set last error message and code
     * @param code Error code
     * @param format Printf-style format string
     * @param ... Printf-style arguments
     */
    static void setError(uint8_t code, const char* format, ...);

    /**
     * @brief Check if response indicates Modbus exception
     * @param response Response buffer
     * @param length Response length
     * @return true if exception detected, false otherwise
     */
    static bool isModbusException(const uint8_t* response, uint16_t length);
};

#endif // _MODBUS_RTU_DRIVER_H
