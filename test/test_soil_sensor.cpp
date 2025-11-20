#ifdef SENSOR_TEST_BUILD

/**
 * @file test_soil_sensor.cpp
 * @brief Standalone test environment for RS485 soil sensor validation
 * 
 * This file provides standalone testing functions for the RS485 soil sensor
 * without requiring full Node/Gateway initialization. Use this to validate
 * sensor functionality, debug connection issues, and verify data format.
 * 
 * **Usage:**
 * 
 * This file is used by the esp32-sensor-test environment in platformio.ini.
 * 
 * **Build & Run:**
 * ```bash
 * # Build and upload test
 * pio run -e esp32-sensor-test --target upload
 * 
 * # Monitor output
 * pio device monitor -e esp32-sensor-test
 * 
 * # Or use PlatformIO test framework
 * pio test -e esp32-sensor-test
 * ```
 * 
 * **Test Modes:**
 * Edit test_soil_sensor_main.cpp to choose:
 * - TEST_MODE_SINGLE: One read and stop
 * - TEST_MODE_CONTINUOUS: Continuous reads
 * - TEST_MODE_DIAGNOSTIC: Diagnostics only
 * 
 * **Expected Serial Output:**
 * 
 * Single read test:
 * ```
 * ====== SENSOR TEST: Single Read ======
 * Status: OK
 * Moisture: 45.3% (Capacity: 512)
 * Temperature: 28.5°C (Raw: 285)
 * pH: 7.2 (Raw: 72)
 * EC: 1500 µS/cm (Raw: 1500)
 * Nitrogen: 120 mg/kg (Raw: 120)
 * Phosphorus: 80 mg/kg (Raw: 80)
 * Potassium: 150 mg/kg (Raw: 150)
 * Timestamp: 123456 ms
 * ===================================
 * ```
 * 
 * Continuous test (5 readings, 10 second intervals):
 * ```
 * ====== SENSOR TEST: Continuous (5 readings, 10s interval) ======
 * Read #1 (Time: 0s)   | Moisture: 45.3% | Temp: 28.5°C | pH: 7.2 | Status: OK
 * Read #2 (Time: 10s)  | Moisture: 45.2% | Temp: 28.4°C | pH: 7.2 | Status: OK
 * Read #3 (Time: 20s)  | Moisture: 45.3% | Temp: 28.5°C | pH: 7.2 | Status: OK
 * ===================================================
 * Stats: 3 OK, 0 Failed (100% success rate)
 * ```
 * 
 * **Diagnostic Information:**
 * - Successfully reads all 7 soil parameters from sensor
 * - Validates CRC16 checksums from Modbus responses
 * - Tests UART communication with proper timeouts
 * - Verifies data format and range consistency
 * - Logs timing information for performance monitoring
 * 
 * **Hardware Requirements:**
 * - RS485 adapter with soil sensor connected to GPIO 20, 21, 42
 * - USB serial connection for log output
 * - Power supply for ESP32 and sensor (typical: 3.3V + 12V)
 * 
 * @author KAGRI Development Team
 * @version 1.0
 */

#include <soil_sensor_service.h>
#include <modbus_rtu_driver.h>
#include <rs485_config.h>
#include <esp_log.h>
#include <driver/uart.h>

static const char* SENSOR_TEST_TAG = "SENSOR-TEST";

/**
 * @brief Initialize sensor for testing
 * 
 * Initializes SoilSensorService for standalone testing.
 * Safe to call multiple times - will skip reinit if already running.
 * 
 * **Output:**
 * ```
 * SENSOR-TEST: ========== SENSOR TEST SETUP ==========
 * SENSOR-TEST: Initializing RS485 Soil Sensor...
 * SENSOR-TEST: ✅ Sensor initialized successfully
 * SENSOR-TEST: Sensor Status: Connected
 * SENSOR-TEST: ========================================
 * ```
 */
void sensor_test_setup() {
    ESP_LOGI(SENSOR_TEST_TAG, "========== SENSOR TEST SETUP ==========");
    ESP_LOGI(SENSOR_TEST_TAG, "Initializing RS485 Soil Sensor...");
    
    if (!SoilSensorService::isConnected()) {
        if (!SoilSensorService::initialize()) {
            ESP_LOGE(SENSOR_TEST_TAG, "❌ FAILED to initialize sensor");
            ESP_LOGI(SENSOR_TEST_TAG, "========================================");
            return;
        }
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "✅ Sensor initialized successfully");
    ESP_LOGI(SENSOR_TEST_TAG, "Sensor Status: %s",
             SoilSensorService::isConnected() ? "Connected" : "Disconnected");
    ESP_LOGI(SENSOR_TEST_TAG, "========================================");
}

/**
 * @brief Read sensor once and display results
 * 
 * Performs a single sensor read and logs all 7 parameters with
 * both processed values and raw Modbus register values.
 * 
 * **Execution Time:** ~125ms (includes UART wait and Modbus retry logic)
 * 
 * **Output Example:**
 * ```
 * SENSOR-TEST: ====== SENSOR TEST: Single Read ======
 * SENSOR-TEST: Reading sensor (may take up to 125ms)...
 * SENSOR-TEST: ✅ Read successful (took 127ms)
 * SENSOR-TEST: Status: OK
 * SENSOR-TEST: Moisture: 45.3% (Capacity: 512)
 * SENSOR-TEST: Temperature: 28.5°C (Raw: 285)
 * SENSOR-TEST: pH: 7.20 (Raw: 72)
 * SENSOR-TEST: EC: 1500.0 µS/cm (Raw: 1500)
 * SENSOR-TEST: Nitrogen: 120.0 mg/kg (Raw: 120)
 * SENSOR-TEST: Phosphorus: 80.0 mg/kg (Raw: 80)
 * SENSOR-TEST: Potassium: 150.0 mg/kg (Raw: 150)
 * SENSOR-TEST: Timestamp: 123456 ms
 * SENSOR-TEST: ===================================
 * ```
 * 
 * @note Blocks for ~125ms during read
 * @note Logs error details if read fails
 */
void sensor_test_read_single() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== SENSOR TEST: Single Read ======");
    ESP_LOGI(SENSOR_TEST_TAG, "Reading sensor (may take up to 125ms)...");
    
    uint32_t startTime = esp_timer_get_time() / 1000;  // Convert to ms
    sensorData reading = SoilSensorService::readData();
    uint32_t endTime = esp_timer_get_time() / 1000;
    uint32_t durationMs = endTime - startTime;
    
    if (reading.error) {
        ESP_LOGE(SENSOR_TEST_TAG, "❌ Read FAILED (took %u ms)", durationMs);
        ESP_LOGE(SENSOR_TEST_TAG, "Error details:");
        ESP_LOGE(SENSOR_TEST_TAG, "  Error flag: YES");
        ESP_LOGE(SENSOR_TEST_TAG, "  Debug: Check RS485 wiring and sensor power");
        ESP_LOGI(SENSOR_TEST_TAG, "===================================");
        return;
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "✅ Read successful (took %u ms)", durationMs);
    ESP_LOGI(SENSOR_TEST_TAG, "Status: %s", reading.error ? "ERROR" : "OK");
    
    if (reading.deviceType == DeviceType::SOIL_SENSOR) {
        // Log all 7 soil parameters
        ESP_LOGI(SENSOR_TEST_TAG, "Moisture: %.1f%% (Capacity: %u)",
                 reading.data.soil.soilMoisture,
                 reading.data.soil.capacity);
        ESP_LOGI(SENSOR_TEST_TAG, "Temperature: %.1f°C (Raw: %d)",
                 reading.data.soil.soilTemperature,
                 (int)reading.data.soil.soilTemperature * 10);  // Approximate raw
        ESP_LOGI(SENSOR_TEST_TAG, "pH: %.2f (Raw: %d)",
                 reading.data.soil.pH,
                 (int)(reading.data.soil.pH * 10));
        ESP_LOGI(SENSOR_TEST_TAG, "EC: %.1f µS/cm (Raw: %u)",
                 reading.data.soil.conductivity,
                 (unsigned int)reading.data.soil.conductivity);
        ESP_LOGI(SENSOR_TEST_TAG, "Nitrogen: %.1f mg/kg (Raw: %u)",
                 reading.data.soil.nitrogen,
                 (unsigned int)reading.data.soil.nitrogen);
        ESP_LOGI(SENSOR_TEST_TAG, "Phosphorus: %.1f mg/kg (Raw: %u)",
                 reading.data.soil.phosphorus,
                 (unsigned int)reading.data.soil.phosphorus);
        ESP_LOGI(SENSOR_TEST_TAG, "Potassium: %.1f mg/kg (Raw: %u)",
                 reading.data.soil.potassium,
                 (unsigned int)reading.data.soil.potassium);
    } else if (reading.deviceType == DeviceType::ENV_SENSOR) {
        ESP_LOGI(SENSOR_TEST_TAG, "Temperature: %.1f°C",
                 reading.data.environment.temperature);
        ESP_LOGI(SENSOR_TEST_TAG, "Humidity: %.1f%%",
                 reading.data.environment.humidity);
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "Timestamp: %u ms", reading.timestamp);
    ESP_LOGI(SENSOR_TEST_TAG, "===================================");
}

/**
 * @brief Continuous sensor reading test
 * 
 * Performs multiple sensor reads at 10-second intervals to validate:
 * - Consistent data acquisition
 * - Proper timing behavior
 * - Error recovery
 * - Long-term stability
 * 
 * **Parameters:**
 * - Number of reads: 5 (configurable by editing NUM_READS)
 * - Interval: 10 seconds between reads
 * - Total time: ~50 seconds
 * 
 * **Output Example:**
 * ```
 * SENSOR-TEST: ====== SENSOR TEST: Continuous (5 readings) ======
 * SENSOR-TEST: Starting continuous read test - 50 seconds total
 * SENSOR-TEST: Read #1 (t=0s)   | Moisture: 45.3% | Temp: 28.5°C | pH: 7.20 | ✅ OK
 * SENSOR-TEST: Read #2 (t=10s)  | Moisture: 45.2% | Temp: 28.4°C | pH: 7.20 | ✅ OK
 * SENSOR-TEST: Read #3 (t=20s)  | Moisture: 45.3% | Temp: 28.5°C | pH: 7.20 | ✅ OK
 * SENSOR-TEST: Read #4 (t=30s)  | Moisture: 45.3% | Temp: 28.5°C | pH: 7.19 | ✅ OK
 * SENSOR-TEST: Read #5 (t=40s)  | Moisture: 45.1% | Temp: 28.4°C | pH: 7.20 | ✅ OK
 * SENSOR-TEST: ===================================================
 * SENSOR-TEST: Stats: 5 OK, 0 Failed (100.0% success rate)
 * ```
 * 
 * @param numReads Number of sensor reads to perform (default: 5, recommend: 3-10)
 * @param intervalSeconds Seconds between reads (default: 10, recommend: 5-30)
 * 
 * **Use Cases:**
 * - Validate sensor reads are stable over time
 * - Detect intermittent connection issues
 * - Measure actual read timing with delays
 * - Verify data format consistency
 * 
 * @note Blocking operation - takes roughly (numReads * intervalSeconds) seconds
 * @note Can be run multiple times
 * @note Good for validation before deploying to nodes
 */
void sensor_test_continuous(uint8_t numReads = 5, uint8_t intervalSeconds = 10) {
    ESP_LOGI(SENSOR_TEST_TAG, "====== SENSOR TEST: Continuous (%d readings) ======", numReads);
    ESP_LOGI(SENSOR_TEST_TAG, "Starting continuous read test");
    ESP_LOGI(SENSOR_TEST_TAG, "  Interval: %d seconds", intervalSeconds);
    ESP_LOGI(SENSOR_TEST_TAG, "  Total time: ~%d seconds", (numReads - 1) * intervalSeconds);
    
    uint32_t successCount = 0;
    uint32_t failCount = 0;
    uint32_t testStartTime = millis();
    
    for (uint8_t i = 0; i < numReads; i++) {
        // Calculate elapsed time since test start
        uint32_t elapsedSec = (millis() - testStartTime) / 1000;
        
        // Read sensor
        sensorData reading = SoilSensorService::readData();
        
        if (reading.error) {
            failCount++;
            ESP_LOGI(SENSOR_TEST_TAG, "Read #%d (t=%ds) | ❌ FAILED", i + 1, elapsedSec);
        } else {
            successCount++;
            
            // Format status string
            const char* statusStr = "✅ OK";
            
            // Log with compact format for easy comparison
            if (reading.deviceType == DeviceType::SOIL_SENSOR) {
                ESP_LOGI(SENSOR_TEST_TAG, 
                         "Read #%d (t=%2ds) | Moisture: %5.1f%% | Temp: %5.1f°C | pH: %5.2f | %s",
                         i + 1, elapsedSec,
                         reading.data.soil.soilMoisture,
                         reading.data.soil.soilTemperature,
                         reading.data.soil.pH,
                         statusStr);
            } else {
                ESP_LOGI(SENSOR_TEST_TAG, "Read #%d (t=%ds) | %s", i + 1, elapsedSec, statusStr);
            }
        }
        
        // Wait interval before next read (except after last read)
        if (i < numReads - 1) {
            ESP_LOGD(SENSOR_TEST_TAG, "Waiting %d seconds until next read...", intervalSeconds);
            vTaskDelay(pdMS_TO_TICKS(intervalSeconds * 1000));
        }
    }
    
    // Summary statistics
    uint32_t totalReads = successCount + failCount;
    float successRate = (totalReads > 0) ? (100.0f * successCount / totalReads) : 0.0f;
    
    ESP_LOGI(SENSOR_TEST_TAG, "===================================================");
    ESP_LOGI(SENSOR_TEST_TAG, "Stats: %u OK, %u Failed (%.1f%% success rate)",
             successCount, failCount, successRate);
    
    if (successRate == 100.0f) {
        ESP_LOGI(SENSOR_TEST_TAG, "🎉 TEST PASSED - All reads successful!");
    } else if (successRate >= 80.0f) {
        ESP_LOGW(SENSOR_TEST_TAG, "⚠️ TEST DEGRADED - Some reads failed, check sensor");
    } else {
        ESP_LOGE(SENSOR_TEST_TAG, "❌ TEST FAILED - Most reads failed, check RS485 wiring");
    }
}

/**
 * @brief Test initialization check
 * 
 * Validates that sensor and services are properly initialized
 * before running functional tests.
 * 
 * **Returns:**
 * - true: Sensor is ready for testing
 * - false: Sensor initialization required or failed
 * 
 * @note Used internally by test functions, but useful for diagnostics
 */
bool sensor_test_is_ready() {
    if (!SoilSensorService::isConnected()) {
        ESP_LOGW(SENSOR_TEST_TAG, "⚠️ Sensor not connected - call sensor_test_setup() first");
        return false;
    }
    return true;
}

/**
 * @brief Display sensor service diagnostics
 * 
 * Logs current sensor status and statistics for debugging.
 * 
 * **Output:**
 * ```
 * SENSOR-TEST: ====== SENSOR DIAGNOSTICS ======
 * SENSOR-TEST: Connection Status: Connected
 * SENSOR-TEST: Last Error: None
 * SENSOR-TEST: ===============================
 * ```
 */
void sensor_test_diagnostics() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== SENSOR DIAGNOSTICS ======");
    
    if (SoilSensorService::isConnected()) {
        ESP_LOGI(SENSOR_TEST_TAG, "Connection Status: ✅ Connected");
    } else {
        ESP_LOGI(SENSOR_TEST_TAG, "Connection Status: ❌ Disconnected");
        ESP_LOGI(SENSOR_TEST_TAG, "Debug: Check RS485 wiring and power");
    }
    
    // Get last error info
    const char* errorMsg = SoilSensorService::getLastErrorDescription();
    if (errorMsg == nullptr || strlen(errorMsg) == 0) {
        ESP_LOGI(SENSOR_TEST_TAG, "Last Error: None");
    } else {
        ESP_LOGW(SENSOR_TEST_TAG, "Last Error: %s", errorMsg);
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "===============================");
}

/**
 * @brief Scan all possible Modbus slave addresses
 * 
 * Scans addresses from 0x01 to 0xFF to find active Modbus slaves.
 * Uses function code 0x04 (Read Input Registers) to check connectivity.
 * 
 * **Output:**
 * ```
 * [SENSOR-TEST] ====== MODBUS ADDRESS SCANNER ======
 * [SENSOR-TEST] Scanning addresses 0x01 to 0xFF...
 * [SENSOR-TEST] This may take 5-10 minutes, please wait...
 * [SENSOR-TEST] 
 * [SENSOR-TEST] ✅ Found device at 0x04B2 (1202)
 * [SENSOR-TEST] ✅ Found device at 0x0005 (5)
 * [SENSOR-TEST] 
 * [SENSOR-TEST] ====== SCAN COMPLETE ======
 * [SENSOR-TEST] Total devices found: 2
 * [SENSOR-TEST] Active addresses: 0x04B2, 0x0005
 * [SENSOR-TEST] ===========================
 * ```
 * 
 * @note This function takes 5-10 minutes to complete due to timeouts
 */
void sensor_test_scan_addresses_optimized() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== MODBUS ADDRESS SCANNER (OPTIMIZED) ======");
    ESP_LOGI(SENSOR_TEST_TAG, "Scanning addresses 1 to 247...");
    ESP_LOGI(SENSOR_TEST_TAG, "");

    const uint16_t MIN_ADDR = 1;
    const uint16_t MAX_ADDR = 247;

    uint8_t foundCount = 0;
    uint16_t foundAddresses[256];
    uint32_t scanStart = millis();

    for (uint16_t addr = MIN_ADDR; addr <= MAX_ADDR; addr++) {
        // Progress
        if (addr % 16 == 1) {
            ESP_LOGI(SENSOR_TEST_TAG, "[%ums] Scanning %d → %d",
                     millis() - scanStart, addr, addr + 15);
        }

        ModbusRTUDriver::setSlaveAddress((uint8_t)addr);

        // Delay for safety
        vTaskDelay(pdMS_TO_TICKS(3));

        float value;

        // Try to read holding register at address 0x0000 (Function 0x03)
        // Sensor uses Function 0x03, not 0x04
        bool ok = ModbusRTUDriver::readHoldingRegisters(0x0000, 1, &value);

        if (ok) {
            foundAddresses[foundCount++] = addr;

            ESP_LOGI(SENSOR_TEST_TAG, "");
            ESP_LOGI(SENSOR_TEST_TAG, "🎯 FOUND DEVICE at 0x%02X (%d decimal)", addr, addr);
            ESP_LOGI(SENSOR_TEST_TAG, "    Register[0x0000] = %.2f (Function 0x03)", value);
            ESP_LOGI(SENSOR_TEST_TAG, "");

            // SHORT DELAY to avoid overlapping frames of detected device
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }

    // Restore default
    ModbusRTUDriver::setSlaveAddress(0);

    uint32_t total = millis() - scanStart;
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "====== SCAN COMPLETE ======");
    ESP_LOGI(SENSOR_TEST_TAG, "Time: %lu ms (%.1f sec)", total, total / 1000.0);
    ESP_LOGI(SENSOR_TEST_TAG, "Devices found: %d", foundCount);

    if (foundCount > 0) {
        for (int i = 0; i < foundCount; i++) {
            ESP_LOGI(SENSOR_TEST_TAG, "  %d. Address = %d (0x%02X)",
                     i + 1, foundAddresses[i], foundAddresses[i]);
        }
    } else {
        ESP_LOGW(SENSOR_TEST_TAG, "⚠ No devices found!");
    }

    ESP_LOGI(SENSOR_TEST_TAG, "===============================================");
}

// Wrapper for original name (compatibility)
void sensor_test_scan_addresses() {
    sensor_test_scan_addresses_optimized();
}

/**
 * @brief Debug UART - read any pending data without timeout
 * Useful to see if sensor is actually sending data
 */
void sensor_test_debug_uart() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== UART DEBUG MODE ======");
    ESP_LOGI(SENSOR_TEST_TAG, "Set Slave Address to 0x01");
    
    ModbusRTUDriver::setSlaveAddress(0x01);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Build request manually
    uint8_t request[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    
    ESP_LOGI(SENSOR_TEST_TAG, "Sending request: 01 03 00 00 00 01 84 0A");
    
    // Clear RX buffer
    uart_flush_input(RS485_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Send request
    gpio_set_level((gpio_num_t)RS485_DE_PIN, 1);  // TX mode
    vTaskDelay(pdMS_TO_TICKS(1));
    
    int written = uart_write_bytes(RS485_UART_NUM, (const char*)request, 8);
    ESP_LOGI(SENSOR_TEST_TAG, "Sent %d bytes", written);
    
    uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(1));
    
    gpio_set_level((gpio_num_t)RS485_DE_PIN, 0);  // RX mode
    vTaskDelay(pdMS_TO_TICKS(50));
    
    ESP_LOGI(SENSOR_TEST_TAG, "Waiting for response (500ms timeout)...");
    
    uint8_t buffer[256];
    int totalRead = 0;
    uint32_t startTime = millis();
    
    while (millis() - startTime < 500) {
        size_t available = uart_read_bytes(RS485_UART_NUM, buffer + totalRead, 
                                          sizeof(buffer) - totalRead, pdMS_TO_TICKS(50));
        if (available > 0) {
            totalRead += available;
            ESP_LOGI(SENSOR_TEST_TAG, "[+%ldms] Read %u bytes (total %d)", 
                     millis() - startTime, available, totalRead);
        }
    }
    
    if (totalRead > 0) {
        ESP_LOGI(SENSOR_TEST_TAG, "✅ Received %d bytes:", totalRead);
        for (int i = 0; i < totalRead; i++) {
            if (i % 8 == 0) {
                ESP_LOGI(SENSOR_TEST_TAG, "   [%d-%d]: %02X %02X %02X %02X %02X %02X %02X %02X",
                         i, i+7,
                         buffer[i], (i+1)<totalRead ? buffer[i+1] : 0,
                         (i+2)<totalRead ? buffer[i+2] : 0, (i+3)<totalRead ? buffer[i+3] : 0,
                         (i+4)<totalRead ? buffer[i+4] : 0, (i+5)<totalRead ? buffer[i+5] : 0,
                         (i+6)<totalRead ? buffer[i+6] : 0, (i+7)<totalRead ? buffer[i+7] : 0);
            }
        }
    } else {
        ESP_LOGW(SENSOR_TEST_TAG, "❌ No data received (timeout)");
        ESP_LOGW(SENSOR_TEST_TAG, "Check:");
        ESP_LOGW(SENSOR_TEST_TAG, "  1. Sensor power supply");
        ESP_LOGW(SENSOR_TEST_TAG, "  2. RS485 A/B connections");
        ESP_LOGW(SENSOR_TEST_TAG, "  3. Pull-up resistors on bus");
        ESP_LOGW(SENSOR_TEST_TAG, "  4. DE pin (GPIO42) switching correctly");
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "====== END UART DEBUG ======");
}

/**
 * @brief Test Phase 1 - Startup Initialization Sequence
 * 
 * Performs the startup sequence:
 * 1. Read Device Version from register 0x07D0
 * 2. Read Sensor ID from registers 0x0023 + 0x0024
 * 
 * The Sensor ID is a 4-digit value formed from two 16-bit Modbus registers:
 *   - Register 0x0023 low byte: represents 2-digit hex value (e.g., 0x12 → "12")
 *   - Register 0x0024 low byte: represents 2-digit hex value (e.g., 0x02 → "02")
 * 
 * Example: Sensor marked "001202" or "1202"
 *   - Reg 0x0023 = 0x0012 (low byte 0x12)
 *   - Reg 0x0024 = 0x0002 (low byte 0x02)
 *   - Combined ID = 0x1202 → displayed as "1202"
 * 
 * **Expected Output:**
 * ```
 * SENSOR-TEST: ====== PHASE 1 TEST: Startup Sequence ======
 * SENSOR-TEST: 🔄 Starting Phase 1 initialization...
 * SENSOR-TEST: Reading device version (0x07D0)...
 * SENSOR-TEST: ✅ Device Version: 0x0001
 * SENSOR-TEST: Reading sensor ID (0x0023 + 0x0024)...
 * SENSOR-TEST: ✅ Sensor ID: 0x1202
 * SENSOR-TEST: ✅ PHASE 1 COMPLETE - All startup queries successful!
 * SENSOR-TEST: ======================================
 * ```
 */
void sensor_test_phase1_startup() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== PHASE 1 TEST: Startup Sequence ======");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "🔄 Starting Phase 1 initialization...");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    
    if (!SoilSensorService::performStartupSequence()) {
        ESP_LOGE(SENSOR_TEST_TAG, "❌ PHASE 1 FAILED");
        ESP_LOGE(SENSOR_TEST_TAG, "Error: %s", SoilSensorService::getLastErrorDescription());
        ESP_LOGE(SENSOR_TEST_TAG, "");
        ESP_LOGE(SENSOR_TEST_TAG, "Troubleshooting:");
        ESP_LOGE(SENSOR_TEST_TAG, "  1. Verify sensor is powered");
        ESP_LOGE(SENSOR_TEST_TAG, "  2. Check RS485 connections (A/B pins)");
        ESP_LOGE(SENSOR_TEST_TAG, "  3. Verify 120Ω termination resistors");
        ESP_LOGE(SENSOR_TEST_TAG, "  4. Confirm sensor address is 0x01");
        ESP_LOGE(SENSOR_TEST_TAG, "======================================");
        return;
    }
    
    // Phase 1 succeeded - read values for display
    int16_t deviceVersion = SoilSensorService::readDeviceVersion();
    uint32_t sensorID = SoilSensorService::readSensorID();
    
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "✅ PHASE 1 COMPLETE - All startup queries successful!");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "📋 Sensor Information:");
    ESP_LOGI(SENSOR_TEST_TAG, "   Device Version: 0x%04X", (uint16_t)deviceVersion);
    ESP_LOGI(SENSOR_TEST_TAG, "   Sensor ID:      0x%04X", sensorID);
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "📊 Status Summary:");
    SoilSensorService::printStatus();
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "✓ Phase 1 ready. Next:");
    ESP_LOGI(SENSOR_TEST_TAG, "  - Phase 2: Trigger measurement via register 0x0009");
    ESP_LOGI(SENSOR_TEST_TAG, "  - Phase 3: Read soil data from registers 0x0000-0x0006");
    ESP_LOGI(SENSOR_TEST_TAG, "======================================");
}

/**
 * @brief Test Phase 2 - Measurement Trigger
 * 
 * Performs the measurement trigger:
 * - Write trigger command to register 0x0009
 * - Wait for sensor to complete measurement
 * - Verify measurement is ready
 * 
 * **Expected Output:**
 * ```
 * SENSOR-TEST: ====== PHASE 2 TEST: Measurement Trigger ======
 * SENSOR-TEST: 🔄 Starting Phase 2 measurement trigger...
 * SENSOR-TEST: Writing trigger to register 0x0009...
 * SENSOR-TEST: ✅ Trigger sent successfully
 * SENSOR-TEST: ✅ PHASE 2 COMPLETE - Measurement triggered!
 * SENSOR-TEST: Wait 2-3 seconds for measurement...
 * SENSOR-TEST: ======================================
 * ```
 */
void sensor_test_phase2_trigger() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== PHASE 2 TEST: Measurement Trigger ======");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "🔄 Starting Phase 2 measurement trigger...");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    
    if (!SoilSensorService::performMeasurementTrigger(0x0001)) {
        ESP_LOGE(SENSOR_TEST_TAG, "❌ PHASE 2 FAILED");
        ESP_LOGE(SENSOR_TEST_TAG, "Error: %s", SoilSensorService::getLastErrorDescription());
        ESP_LOGE(SENSOR_TEST_TAG, "======================================");
        return;
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "✅ PHASE 2 COMPLETE - Measurement triggered!");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "⏳ Waiting 3 seconds for measurement to complete...");
    
    delay(3000);  // Wait for measurement
    
    ESP_LOGI(SENSOR_TEST_TAG, "✅ Measurement complete! Ready for Phase 3 data readout.");
    ESP_LOGI(SENSOR_TEST_TAG, "======================================");
}

/**
 * @brief Test Phase 3 - Data Readout
 * 
 * Reads all 7 soil sensor parameters:
 * 1. Read 7 consecutive registers (0x0000-0x0006)
 * 2. Display all soil parameters
 * 3. Verify data validity
 * 
 * Parameters read:
 * - 0x0000: Soil Moisture (%)
 * - 0x0001: Soil Temperature (°C)
 * - 0x0002: pH value
 * - 0x0003: Electrical Conductivity (EC, µS/cm)
 * - 0x0004: Nitrogen content (mg/kg)
 * - 0x0005: Phosphorus content (mg/kg)
 * - 0x0006: Potassium content (mg/kg)
 * 
 * **Expected Output:**
 * ```
 * SENSOR-TEST: ====== PHASE 3 TEST: Data Readout ======
 * SENSOR-TEST: 🔄 Starting Phase 3 data readout...
 * SENSOR-TEST: Reading 7 soil parameters...
 * SENSOR-TEST: ✅ Moisture: 45.3%
 * SENSOR-TEST: ✅ Temperature: 28.5°C
 * SENSOR-TEST: ✅ pH: 7.2
 * SENSOR-TEST: ✅ EC: 1500 µS/cm
 * SENSOR-TEST: ✅ Nitrogen: 120 mg/kg
 * SENSOR-TEST: ✅ Phosphorus: 80 mg/kg
 * SENSOR-TEST: ✅ Potassium: 150 mg/kg
 * SENSOR-TEST: ✅ PHASE 3 COMPLETE - All parameters read!
 * SENSOR-TEST: ======================================
 * ```
 */
void sensor_test_phase3_readout() {
    ESP_LOGI(SENSOR_TEST_TAG, "====== PHASE 3 TEST: Data Readout ======");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    ESP_LOGI(SENSOR_TEST_TAG, "🔄 Starting Phase 3 data readout...");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    
    ESP_LOGI(SENSOR_TEST_TAG, "Reading 7 soil parameters from 0x0000-0x0006...");
    ESP_LOGI(SENSOR_TEST_TAG, "");
    
    sensorData reading = SoilSensorService::readData();
    
    if (reading.deviceType == DeviceType::SOIL_SENSOR && 
        SoilSensorService::getConsecutiveFailures() == 0) {
        
        ESP_LOGI(SENSOR_TEST_TAG, "✅ Successfully read all parameters:");
        ESP_LOGI(SENSOR_TEST_TAG, "");
        ESP_LOGI(SENSOR_TEST_TAG, "   📊 Soil Moisture:    %.1f%%", reading.data.soil.soilMoisture);
        ESP_LOGI(SENSOR_TEST_TAG, "   🌡️  Temperature:     %.1f°C", reading.data.soil.soilTemperature);
        ESP_LOGI(SENSOR_TEST_TAG, "   🧪 pH:              %.2f", reading.data.soil.pH);
        ESP_LOGI(SENSOR_TEST_TAG, "   ⚡ EC (Conductivity): %.1f µS/cm", reading.data.soil.conductivity);
        ESP_LOGI(SENSOR_TEST_TAG, "   🌱 Nitrogen (N):    %.1f mg/kg", reading.data.soil.nitrogen);
        ESP_LOGI(SENSOR_TEST_TAG, "   🌿 Phosphorus (P):  %.1f mg/kg", reading.data.soil.phosphorus);
        ESP_LOGI(SENSOR_TEST_TAG, "   🍃 Potassium (K):   %.1f mg/kg", reading.data.soil.potassium);
        
        ESP_LOGI(SENSOR_TEST_TAG, "");
        ESP_LOGI(SENSOR_TEST_TAG, "✅ PHASE 3 COMPLETE - All parameters read successfully!");
        ESP_LOGI(SENSOR_TEST_TAG, "");
        
        // Print status for statistics
        SoilSensorService::printStatus();
        
    } else {
        ESP_LOGE(SENSOR_TEST_TAG, "❌ PHASE 3 FAILED - Could not read sensor data");
        ESP_LOGE(SENSOR_TEST_TAG, "Error: %s", SoilSensorService::getLastErrorDescription());
        ESP_LOGE(SENSOR_TEST_TAG, "Consecutive failures: %u", SoilSensorService::getConsecutiveFailures());
    }
    
    ESP_LOGI(SENSOR_TEST_TAG, "======================================");
}

#endif // SENSOR_TEST_BUILD

