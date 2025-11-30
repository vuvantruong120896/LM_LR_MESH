#include "../include/soil_sensor_service.h"
#include "modbus_async.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "../../utils/battery_monitor.h"  // Battery monitoring
#include <cstring>
#include <cstdarg>
#include <sys/time.h>
#include <freertos/task.h>

// ============================================================================
// STATIC MEMBER INITIALIZATION
// ============================================================================

bool SoilSensorService::initialized = false;
uint32_t SoilSensorService::successfulReads = 0;
uint32_t SoilSensorService::failedReads = 0;
uint32_t SoilSensorService::consecutiveFailures = 0;
uint8_t SoilSensorService::lastErrorCode = 0;
char SoilSensorService::lastErrorMsg[256] = {0};
uint32_t SoilSensorService::totalReadTimeMs = 0;

// ============================================================================
// INITIALIZATION & SHUTDOWN
// ============================================================================

bool SoilSensorService::initialize() {
    if (initialized) {
        ESP_LOGW(SOIL_SENSOR_TAG, "Soil sensor service already initialized");
        return true;
    }

    // Initialize Modbus Async driver
    if (!ModbusAsync::getInstance().initialize()) {
        setError(1, "Failed to initialize Modbus Async driver");
        ESP_LOGE(SOIL_SENSOR_TAG, "%s", lastErrorMsg);
        return false;
    }

    // Initialize battery monitor
    if (!BatteryMonitor::init()) {
        ESP_LOGW(SOIL_SENSOR_TAG, "⚠️ Battery monitor init failed - will use default value");
    }

    initialized = true;
    lastErrorCode = 0;
    successfulReads = 0;
    failedReads = 0;
    consecutiveFailures = 0;
    totalReadTimeMs = 0;

    ESP_LOGI(SOIL_SENSOR_TAG, "✅ Soil sensor service initialized (Async Mode)");
    ESP_LOGI(SOIL_SENSOR_TAG, "   Modbus Slave: 0x%04X (baud=%u)",
             MODBUS_SLAVE_ADDRESS, MODBUS_BAUD_RATE);
    ESP_LOGI(SOIL_SENSOR_TAG, "   RS485 Pins: TX=%d, RX=%d, DE=%d",
             RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);

    return true;
}

void SoilSensorService::shutdown() {
    if (!initialized) return;

    // ModbusAsync::shutdown();
    initialized = false;

    ESP_LOGI(SOIL_SENSOR_TAG, "Soil sensor service shut down");
}

bool SoilSensorService::isReady() {
    return initialized;
}

// ============================================================================
// STARTUP INITIALIZATION (Phase 1)
// ============================================================================

bool SoilSensorService::performStartupSequence() {
    if (!isReady()) {
        setError(11, "Service not ready for startup sequence");
        return false;
    }

    ESP_LOGI(SOIL_SENSOR_TAG, "🔄 Starting Phase 1: Startup Initialization Sequence...");

    // Phase 1: Read Device Version (Register 0x07D0)
    ESP_LOGI(SOIL_SENSOR_TAG, "  └─ Reading device version from register 0x07D0...");
    int16_t deviceVersion = readDeviceVersion();
    if (deviceVersion < 0) {
        setError(12, "Failed to read device version");
        ESP_LOGE(SOIL_SENSOR_TAG, "     ❌ Error: %s", lastErrorMsg);
        return false;
    }
    ESP_LOGI(SOIL_SENSOR_TAG, "     ✅ Device version: 0x%04X", (uint16_t)deviceVersion);

    // Small delay between requests
    vTaskDelay(pdMS_TO_TICKS(50));

    // Phase 1: Read Sensor ID (Registers 0x0023 + 0x0024)
    ESP_LOGI(SOIL_SENSOR_TAG, "  └─ Reading sensor ID from 0x0023 + 0x0024...");
    uint32_t sensorID = readSensorID();
    if (sensorID == 0xFFFF) {  // Error code
        setError(13, "Failed to read sensor ID");
        ESP_LOGE(SOIL_SENSOR_TAG, "     ❌ Error: %s", lastErrorMsg);
        return false;
    }
    ESP_LOGI(SOIL_SENSOR_TAG, "     ✅ Sensor ID: 0x%04X", sensorID);

    ESP_LOGI(SOIL_SENSOR_TAG, "✅ Phase 1 Startup Sequence COMPLETE");
    ESP_LOGI(SOIL_SENSOR_TAG, "   Device Ver: 0x%04X | Sensor ID: 0x%04X",
             (uint16_t)deviceVersion, sensorID);

    return true;
}

int16_t SoilSensorService::readDeviceVersion() {
    if (!isReady()) return -1;
    
    float value;
    uint32_t txId = ModbusAsync::getInstance().readHoldingRegistersAsync(MODBUS_SLAVE_ADDRESS, REG_DEVICE_VERSION, 1);
    if (txId == 0) return -1;
    
    uint32_t startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        if (millis() - startWait > 1000) return -1;
    }
    
    if (ModbusAsync::getInstance().getResult(txId, &value, 1) == 1) {
        return (int16_t)value;
    }
    
    return -1;
}

uint32_t SoilSensorService::readSensorID() {
    if (!isReady()) return 0xFFFFFF;  // Error code (24-bit)
    
    float values[2];
    
    // Read first register (0x0023)
    uint32_t txId1 = ModbusAsync::getInstance().readHoldingRegistersAsync(MODBUS_SLAVE_ADDRESS, REG_SENSOR_ID_HIGH, 1);
    if (txId1 == 0) return 0xFFFFFF;
    
    uint32_t startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId1)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        if (millis() - startWait > 1000) return 0xFFFFFF;
    }
    
    if (ModbusAsync::getInstance().getResult(txId1, &values[0], 1) != 1) return 0xFFFFFF;
    
    vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms - minimal delay between requests
    
    // Read second register (0x0024)
    uint32_t txId2 = ModbusAsync::getInstance().readHoldingRegistersAsync(MODBUS_SLAVE_ADDRESS, REG_SENSOR_ID_LOW, 1);
    if (txId2 == 0) return 0xFFFFFF;
    
    startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId2)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        if (millis() - startWait > 1000) return 0xFFFFFF;
    }
    
    if (ModbusAsync::getInstance().getResult(txId2, &values[1], 1) != 1) return 0xFFFFFF;
    
    // Extract Sensor ID from two 16-bit registers
    // Each register's low byte represents a 2-digit decimal value
    // Example: 0x0012 → decimal representation "12"
    //          0x0002 → decimal representation "02"
    //          Combined: "1202"
    
    uint16_t reg0023 = (uint16_t)values[0];
    uint16_t reg0024 = (uint16_t)values[1];
    
    // Extract low byte from each register (contains the actual value)
    uint8_t byte0 = (uint8_t)(reg0023 & 0xFF);  // 0x12
    uint8_t byte1 = (uint8_t)(reg0024 & 0xFF);  // 0x02
    
    // Combine as 4-digit decimal: byte0 * 100 + byte1
    // 0x12 = 18 decimal → displayed as "12" (hex format)
    // 0x02 = 2 decimal  → displayed as "02" (hex format)
    // Result: 0x1202
    uint32_t sensorID = ((uint32_t)byte0 << 8) | byte1;
    
    // Alternative interpretation: treat as BCD or decimal
    // 0x12 BCD = 12 decimal, 0x02 BCD = 2 decimal → "1202"
    
    return sensorID;
}

// ============================================================================
// MEASUREMENT TRIGGER (Phase 2)
// ============================================================================

bool SoilSensorService::performMeasurementTrigger(uint16_t triggerValue) {
    if (!isReady()) {
        setError(15, "Service not ready for measurement trigger");
        return false;
    }

    ESP_LOGI(SOIL_SENSOR_TAG, "🔄 Starting Phase 2: Measurement Trigger...");
    
    // Write trigger to register 0x0009
    ESP_LOGI(SOIL_SENSOR_TAG, "  └─ Writing trigger 0x%04X to register 0x0009...", triggerValue);
    
    uint32_t txId = ModbusAsync::getInstance().writeSingleRegisterAsync(MODBUS_SLAVE_ADDRESS, 0x0009, triggerValue);
    if (txId == 0) {
        setError(16, "Failed to send trigger");
        return false;
    }
    ESP_LOGI(SOIL_SENSOR_TAG, "     Trigger TX ID: %lu", txId);
    
    uint32_t startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        // ESP_LOGD(SOIL_SENSOR_TAG, "     ... waiting for trigger (elapsed: %lu ms)", millis() - startWait);
        if (millis() - startWait > 3000) { // Timeout: 3 seconds
            setError(17, "Trigger timeout");
            return false;
        }
    }
    
    if (ModbusAsync::getInstance().getTransactionState(txId) != ModbusAsync::Transaction::COMPLETED) {
        setError(18, "Trigger failed");
        return false;
    }
    
    ESP_LOGI(SOIL_SENSOR_TAG, "     ✅ Trigger written successfully");
    
    ESP_LOGI(SOIL_SENSOR_TAG, "✅ Phase 2 Measurement Trigger COMPLETE");
    ESP_LOGI(SOIL_SENSOR_TAG, "   Sensor measurement initiated");
    ESP_LOGI(SOIL_SENSOR_TAG, "   Wait 2-3 seconds for measurement to complete...");

    return true;
}

// ============================================================================
// SENSOR DATA READING (Phase 3)
// ========================================================================== 

sensorData SoilSensorService::readData() {
    sensorData result;
    memset(&result, 0, sizeof(sensorData));

    if (!isReady()) {
        setError(10, "Service not ready");
        result.deviceType = DeviceType::SOIL_SENSOR;
        failedReads++;
        consecutiveFailures++;
        return result;
    }

    // Record start time
    uint32_t startTime = millis();

    // Phase 1: 
    if (performStartupSequence()) {
        ESP_LOGI(SOIL_SENSOR_TAG, "✅ Phase 1 complete");
    } else {
        failedReads++;
        consecutiveFailures++;
        result.deviceType = DeviceType::SOIL_SENSOR;

        ESP_LOGE(SOIL_SENSOR_TAG, 
                 "❌ Failed to perform startup sequence: %s (failures: %u)", 
                 lastErrorMsg, consecutiveFailures);

    }

    // Phase 2: Trigger Measurement
    // Some sensors require a trigger command before reading
    if (performMeasurementTrigger(0x0001)) {
        ESP_LOGI(SOIL_SENSOR_TAG, "⏳ Waiting 3s for measurement to complete...");
        vTaskDelay(pdMS_TO_TICKS(3000));
    } else {
        ESP_LOGW(SOIL_SENSOR_TAG, "⚠️ Measurement trigger failed or skipped");
    }

    // Perform Phase 3: Read soil parameters
    if (!readSoilParameters(result)) {
        failedReads++;
        consecutiveFailures++;
        result.deviceType = DeviceType::SOIL_SENSOR;

        ESP_LOGE(SOIL_SENSOR_TAG, 
                 "❌ Failed to read sensor: %s (failures: %u)", 
                 lastErrorMsg, consecutiveFailures);

        return result;
    }

    // Record success
    uint32_t readTimeMs = millis() - startTime;
    successfulReads++;
    consecutiveFailures = 0;
    totalReadTimeMs += readTimeMs;
    lastErrorCode = 0;

    ESP_LOGI(SOIL_SENSOR_TAG, "✅ Soil sensor read successful (%.1f ms, success rate: %u%%)",
             (float)readTimeMs,
             (successfulReads * 100) / (successfulReads + failedReads));

    return result;
}

bool SoilSensorService::isConnected() {
    if (!isReady()) return false;

    // Try to read a single register as connectivity check
    uint32_t txId = ModbusAsync::getInstance().readHoldingRegistersAsync(MODBUS_SLAVE_ADDRESS, REG_SOIL_MOISTURE, 1);
    if (txId == 0) return false;
    
    uint32_t startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        if (millis() - startWait > 500) return false;
    }
    
    float val;
    if (ModbusAsync::getInstance().getResult(txId, &val, 1) == 1) {
        ESP_LOGI(SOIL_SENSOR_TAG, "✅ Sensor connectivity OK");
        return true;
    }

    ESP_LOGW(SOIL_SENSOR_TAG, "⚠️ Connectivity check failed");
    return false;
}

uint32_t SoilSensorService::getConsecutiveFailures() {
    return consecutiveFailures;
}

// ============================================================================
// ERROR INFORMATION
// ============================================================================

const char* SoilSensorService::getLastErrorDescription() {
    return lastErrorMsg;
}

uint8_t SoilSensorService::getLastErrorCode() {
    return lastErrorCode;
}

void SoilSensorService::clearErrors() {
    lastErrorCode = 0;
    memset(lastErrorMsg, 0, sizeof(lastErrorMsg));
    consecutiveFailures = 0;
}

// ============================================================================
// STATUS & STATISTICS
// ============================================================================

uint32_t SoilSensorService::getSuccessfulReads() {
    return successfulReads;
}

uint32_t SoilSensorService::getFailedReads() {
    return failedReads;
}

uint32_t SoilSensorService::getAverageReadTimeMs() {
    if (successfulReads == 0) return 0;
    return totalReadTimeMs / successfulReads;
}

void SoilSensorService::resetStatistics() {
    successfulReads = 0;
    failedReads = 0;
    totalReadTimeMs = 0;
    consecutiveFailures = 0;
    ESP_LOGI(SOIL_SENSOR_TAG, "Statistics reset");
}

void SoilSensorService::printStatus() {
    uint32_t totalReads = successfulReads + failedReads;
    float successRate = (totalReads > 0) ? (successfulReads * 100.0f / totalReads) : 0.0f;
    uint32_t avgTime = getAverageReadTimeMs();

    ESP_LOGI(SOIL_SENSOR_TAG, "╔════════════════════════════════════════════╗");
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Soil Sensor Service Status                 ║");
    ESP_LOGI(SOIL_SENSOR_TAG, "╠════════════════════════════════════════════╣");
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Status: %s", isReady() ? "✅ READY" : "❌ NOT READY");
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Connected: %s", isConnected() ? "✅ YES" : "❌ NO");
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Successful reads: %u", successfulReads);
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Failed reads: %u", failedReads);
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Success rate: %.1f%%", successRate);
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Consecutive failures: %u", consecutiveFailures);
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Average read time: %u ms", avgTime);
    ESP_LOGI(SOIL_SENSOR_TAG, "║ Last error: [%u] %s", lastErrorCode, lastErrorMsg);
    ESP_LOGI(SOIL_SENSOR_TAG, "╚════════════════════════════════════════════╝");
}

// ============================================================================
// INTERNAL: ERROR HANDLING
// ============================================================================

void SoilSensorService::setError(uint8_t code, const char* format, ...) {
    lastErrorCode = code;

    va_list args;
    va_start(args, format);
    vsnprintf(lastErrorMsg, sizeof(lastErrorMsg), format, args);
    va_end(args);

    ESP_LOGD(SOIL_SENSOR_TAG, "Error [%u]: %s", code, lastErrorMsg);
}

// ============================================================================
// INTERNAL: SENSOR PARAMETER READING
// ============================================================================

bool SoilSensorService::readSoilParameters(sensorData& outSensorData) {
    // Initialize structure
    memset(&outSensorData, 0, sizeof(sensorData));
    outSensorData.deviceType = DeviceType::SOIL_SENSOR;

    // Populate common fields
    populateCommonFields(outSensorData);

    // Read all 7 soil sensor registers at once
    float registerValues[SOIL_SENSOR_REGISTER_COUNT];

    ESP_LOGD(SOIL_SENSOR_TAG, "Reading %u registers from address 0x%04X...",
             SOIL_SENSOR_REGISTER_COUNT, REG_SOIL_MOISTURE);

    // Async Read (Blocking wait)
    uint32_t txId = ModbusAsync::getInstance().readHoldingRegistersAsync(
        MODBUS_SLAVE_ADDRESS, REG_SOIL_MOISTURE, SOIL_SENSOR_REGISTER_COUNT);
    
    if (txId == 0) {
        setError(20, "Failed to start async read");
        return false;
    }
    
    // Wait for result
    uint32_t startWait = millis();
    while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
        vTaskDelay(pdMS_TO_TICKS(1));  // 🔧 Reduced from 10ms to 1ms for faster receiver task scheduling
        if (millis() - startWait > 2000) {
            setError(21, "Async read timeout");
            return false;
        }
    }
    
    int count = ModbusAsync::getInstance().getResult(txId, registerValues, SOIL_SENSOR_REGISTER_COUNT);
    if (count != SOIL_SENSOR_REGISTER_COUNT) {
        setError(22, "Async read failed (code=%d)", count);
        return false;
    }

    // Convert register values to sensor data
    convertRegisterValuesToSensorData(registerValues, outSensorData);

    return true;
}

void SoilSensorService::populateCommonFields(sensorData& outSensorData) {
    outSensorData.deviceType = DeviceType::SOIL_SENSOR;
    outSensorData.counter = successfulReads + 1;
    outSensorData.timestamp = time(nullptr);
    
    // Read real battery voltage from ADC
    outSensorData.battery = BatteryMonitor::readVoltage();
    if (outSensorData.battery < 0.1f) {
        // Fallback if battery read fails
        ESP_LOGW(SOIL_SENSOR_TAG, "Battery read failed, using nominal 3.7V");
        outSensorData.battery = 3.7f;
    }

    // Set Node ID from provisioning or MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    outSensorData.nodeId = ((uint16_t)mac[4] << 8) | mac[5];
}

void SoilSensorService::convertRegisterValuesToSensorData(
    const float* registerValues,
    sensorData& outSensorData) {

    if (!registerValues) return;

    // Direct assignment - adjust scaling based on actual sensor output format
    // Registers are read as 16-bit integers, converted to float by Modbus driver
    
    // // DEBUG: Log all raw register values
    // ESP_LOGI(SOIL_SENSOR_TAG, "🔍 RAW REGISTER VALUES (BEFORE CONVERSION):");
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[0] = %.0f (Moisture)", registerValues[0]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[1] = %.0f (Temperature)", registerValues[1]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[2] = %.0f (pH)", registerValues[2]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[3] = %.0f (EC)", registerValues[3]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[4] = %.0f (N)", registerValues[4]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[5] = %.0f (P)", registerValues[5]);
    // ESP_LOGI(SOIL_SENSOR_TAG, "   Reg[6] = %.0f (K)", registerValues[6]);
    
    // NOTE: These conversions assume the sensor outputs:
    // - Moisture as percentage (0-100)
    // - Temperature in °C
    // - pH as pH value (0-14)
    // - EC in mS/cm
    // - N, P, K in mg/kg
    //
    // If the sensor outputs values in different formats (scaled by 10, 100, etc),
    // apply the appropriate scaling here.
    //
    // Common patterns for soil sensors:
    // - Temperature: often scaled by 10 (e.g., 231 = 23.1°C)
    // - pH: often as-is or scaled by 10
    // - EC: often as-is (µS/cm) or scaled by 10
    // - Moisture: often as-is (%)
    // - NPK: often as-is (mg/kg) or scaled by 10

    // SCALING FACTORS (based on sensor calibration)
    // All values are scaled by 10 from sensor
    outSensorData.data.soil.soilMoisture = registerValues[0] / 10.0f;            // Reg 0: Moisture % (÷10)
    outSensorData.data.soil.soilTemperature = registerValues[1] / 10.0f;         // Reg 1: Temp °C (÷10)
    outSensorData.data.soil.pH = registerValues[2] / 10.0f;                      // Reg 2: pH (÷10)
    outSensorData.data.soil.conductivity = registerValues[3] / 100.0f;            // Reg 3: EC µS/cm     (÷100)
    outSensorData.data.soil.nitrogen = registerValues[4];                        // Reg 4: N mg/kg (as-is)
    outSensorData.data.soil.phosphorus = registerValues[5];                      // Reg 5: P mg/kg (as-is)
    outSensorData.data.soil.potassium = registerValues[6];                       // Reg 6: K mg/kg (as-is)
    outSensorData.data.soil.saltContent = registerValues[7];                     // Reg 7: Salt mg/kg (as-is)
    outSensorData.data.soil.capacity = 0;                                        // Reserved for future use

    // Log read values
    ESP_LOGD(SOIL_SENSOR_TAG,
             "📊 Soil parameters: M=%.1f%% T=%.1f°C pH=%.2f EC=%.2f N=%.0f P=%.0f K=%.0f Salt=%.0f",
             outSensorData.data.soil.soilMoisture,
             outSensorData.data.soil.soilTemperature,
             outSensorData.data.soil.pH,
             outSensorData.data.soil.conductivity,
             outSensorData.data.soil.nitrogen,
             outSensorData.data.soil.phosphorus,
             outSensorData.data.soil.potassium,
             outSensorData.data.soil.saltContent);
}

// ============================================================================
// ASYNC OPERATIONS
// ============================================================================

uint32_t SoilSensorService::requestDataRead() {
    if (!isReady()) return 0;
    return ModbusAsync::getInstance().readHoldingRegistersAsync(
        MODBUS_SLAVE_ADDRESS, REG_SOIL_MOISTURE, SOIL_SENSOR_REGISTER_COUNT);
}

bool SoilSensorService::isReadComplete(uint32_t transactionId) {
    return ModbusAsync::getInstance().isTransactionComplete(transactionId);
}

sensorData SoilSensorService::getDataReadResult(uint32_t transactionId) {
    sensorData result;
    memset(&result, 0, sizeof(sensorData));
    result.deviceType = DeviceType::SOIL_SENSOR;
    populateCommonFields(result);
    
    float registerValues[SOIL_SENSOR_REGISTER_COUNT];
    int count = ModbusAsync::getInstance().getResult(transactionId, registerValues, SOIL_SENSOR_REGISTER_COUNT);
    
    if (count == SOIL_SENSOR_REGISTER_COUNT) {
        convertRegisterValuesToSensorData(registerValues, result);
        successfulReads++;
        consecutiveFailures = 0;
        lastErrorCode = 0;
    } else {
        failedReads++;
        consecutiveFailures++;
        setError(30, "Async read result failed: %d", count);
    }
    
    return result;
}
