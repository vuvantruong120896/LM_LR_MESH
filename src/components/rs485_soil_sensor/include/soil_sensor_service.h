#ifndef _SOIL_SENSOR_SERVICE_H
#define _SOIL_SENSOR_SERVICE_H

#include <Arduino.h>
#include "sensor_data.h"
#include "modbus_async.h"
// #include "modbus_rtu_driver.h" // Deprecated

/**
 * @file soil_sensor_service.h
 * @brief Soil Sensor Service - High-level interface for reading soil sensor data
 * 
 * Provides:
 * - Unified interface for reading 7-parameter soil sensor data
 * - Modbus RTU communication via RS485
 * - Error handling with fallback to simulation mode (optional)
 * - Status tracking and error logging
 * 
 * Architecture:
 *   SoilSensorService (this layer)
 *          ↓
 *   ModbusRTUDriver (Modbus protocol)
 *          ↓
 *   UART/GPIO Hardware
 *          ↓
 *   RS485 Bus → Soil Sensor
 * 
 * Shared component for both Node and Gateway applications
 */

class SoilSensorService {
public:
    // ========================================================================
    // INITIALIZATION
    // ========================================================================

    /**
     * @brief Initialize soil sensor service and Modbus driver
     * @return true if initialization successful, false otherwise
     */
    static bool initialize();

    /**
     * @brief Shutdown soil sensor service
     */
    static void shutdown();

    /**
     * @brief Check if service is initialized and ready
     * @return true if ready to read sensor data
     */
    static bool isReady();

    // ========================================================================
    // STARTUP INITIALIZATION (Phase 1)
    // ========================================================================

    /**
     * @brief Perform startup initialization sequence for sensor
     * 
     * Reads device version and sensor ID during startup.
     * This phase prepares the sensor for measurement mode.
     * 
     * Sensor ID is 32-bit, stored in two consecutive registers:
     *   - Register 0x0023: High word (bits 16-31)
     *   - Register 0x0024: Low word (bits 0-15)
     * 
     * Example: Sensor ID 0x00120002
     *   - Reg 0x0023 returns 0x0012
     *   - Reg 0x0024 returns 0x0002
     * 
     * @return true if startup sequence completed successfully
     */
    static bool performStartupSequence();

    /**
     * @brief Read device version/ID from sensor
     * @return Device version value (16-bit) or -1 if failed
     */
    static int16_t readDeviceVersion();

    /**
     * @brief Read Sensor ID (4-digit format from two 16-bit registers)
     * 
     * Reads registers 0x0023 and 0x0024, extracting low byte from each.
     * Each byte represents a 2-digit hex value displayed as decimal.
     * 
     * Example: Sensor marked "001202" or "1202"
     *   - Reg 0x0023 returns 0x0012 → low byte 0x12 → "12"
     *   - Reg 0x0024 returns 0x0002 → low byte 0x02 → "02"
     *   - Combined: 0x1202 → displayed as "1202"
     * 
     * @return Sensor ID as 16-bit value (0x0000-0xFFFF) or 0xFFFF if failed
     */
    static uint32_t readSensorID();

    // ========================================================================
    // MEASUREMENT TRIGGER (Phase 2)
    // ========================================================================

    /**
     * @brief Perform measurement trigger (Phase 2)
     * 
     * Writes trigger command to register 0x0009 to initiate sensor measurement.
     * After calling this, wait 2-3 seconds before reading data in Phase 3.
     * 
     * @param triggerValue Value to write to register 0x0009 (typically 0x0001)
     * @return true if trigger sent successfully
     */
    static bool performMeasurementTrigger(uint16_t triggerValue = 0x0001);

    // ========================================================================
    // SENSOR DATA READING (Phase 3)
    // ========================================================================

    /**
     * @brief Read all 7 soil sensor parameters
     * 
     * Reads real sensor data via Modbus RTU. If real reading fails,
     * returns error-filled structure (does NOT fallback to simulation).
     * 
     * @return sensorData structure with:
     *   - If successful: All 7 soil parameters populated
     *   - If failed: Structure with error flags set
     */
    static sensorData readData();

    /**
     * @brief Check if sensor is currently connected
     * 
     * Attempts a quick single-register read to verify communication.
     * 
     * @return true if sensor responds, false if disconnected/failed
     */
    static bool isConnected();

    /**
     * @brief Get number of consecutive read failures
     * @return Failure count (resets to 0 on successful read)
     */
    static uint32_t getConsecutiveFailures();

    // ========================================================================
    // ERROR INFORMATION
    // ========================================================================

    /**
     * @brief Get human-readable description of last error
     * @return Error description string
     */
    static const char* getLastErrorDescription();

    /**
     * @brief Get error code from last read attempt
     * @return Error code (0 = success)
     */
    static uint8_t getLastErrorCode();

    /**
     * @brief Clear error state
     */
    static void clearErrors();

    // ========================================================================
    // STATUS & STATISTICS
    // ========================================================================

    /**
     * @brief Get total number of successful reads since initialization
     * @return Success count
     */
    static uint32_t getSuccessfulReads();

    /**
     * @brief Get total number of failed reads since initialization
     * @return Failure count
     */
    static uint32_t getFailedReads();

    /**
     * @brief Get average read time (milliseconds)
     * @return Average time in ms
     */
    static uint32_t getAverageReadTimeMs();

    /**
     * @brief Reset statistics
     */
    static void resetStatistics();

    /**
     * @brief Print debug information
     */
    static void printStatus();

    /**
     * @brief Start an asynchronous read of sensor data
     * @return Transaction ID (0 if failed)
     */
    static uint32_t requestDataRead();

    /**
     * @brief Check if the async read is complete
     * @param transactionId Transaction ID returned by requestDataRead
     * @return true if complete
     */
    static bool isReadComplete(uint32_t transactionId);

    /**
     * @brief Get the result of the async read
     * @param transactionId Transaction ID
     * @return sensorData structure
     */
    static sensorData getDataReadResult(uint32_t transactionId);

private:
    // ========================================================================
    // INTERNAL STATE
    // ========================================================================

    static bool initialized;
    static uint32_t successfulReads;
    static uint32_t failedReads;
    static uint32_t consecutiveFailures;
    static uint8_t lastErrorCode;
    static char lastErrorMsg[256];
    static uint32_t totalReadTimeMs;

    // ========================================================================
    // INTERNAL METHODS
    // ========================================================================

    /**
     * @brief Set last error information
     * @param code Error code
     * @param format Printf-style format string
     */
    static void setError(uint8_t code, const char* format, ...);

    /**
     * @brief Read all 7 soil parameters via Modbus
     * @param outSensorData Output structure to populate
     * @return true if all parameters read successfully
     */
    static bool readSoilParameters(sensorData& outSensorData);

    /**
     * @brief Populate sensorData with common fields
     * @param outSensorData Structure to populate
     */
    static void populateCommonFields(sensorData& outSensorData);

    /**
     * @brief Convert raw register values to sensor parameters
     * 
     * This method handles the conversion from raw Modbus register values
     * to actual soil sensor parameters. Scaling/offset may need adjustment
     * based on actual sensor specifications.
     * 
     * @param registerValues Array of 7 raw register values
     * @param outSensorData Output structure
     */
    static void convertRegisterValuesToSensorData(const float* registerValues, sensorData& outSensorData);
};

#endif // _SOIL_SENSOR_SERVICE_H
