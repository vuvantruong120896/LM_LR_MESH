#ifndef _SENSOR_DATA_H
#define _SENSOR_DATA_H

#include <cstdint>

/**
 * @file sensor_data.h
 * @brief Sensor data structures - standalone, no dependencies
 * 
 * This file contains only data structures for sensor readings.
 * No dependencies on LoRa, mesh, or application code.
 * Can be used in test environments or minimal builds.
 */

/**
 * @brief Device type enumeration
 */
enum class DeviceType : uint8_t {
    SOIL_SENSOR = 1,      ///< 8-parameter soil sensor (NPK + pH + EC + moisture + temp + salt)
    ENV_SENSOR = 2,       ///< Environment sensor (temp + humidity + pressure + light)
    UNKNOWN = 255         ///< Unknown or uninitialized
};

/**
 * @brief Unified sensor data structure supporting multiple sensor types
 * 
 * This structure uses a union to efficiently store different sensor types
 * while maintaining a small memory footprint. The deviceType field determines
 * which union member is active.
 * 
 * Memory layout:
 * - Fixed fields: 16 bytes (deviceType, counter, battery, timestamp, nodeId, error)
 * - Union data: 32 bytes max (soil sensor has most params)
 * - Total: ~48 bytes
 * 
 * Usage:
 * ```cpp
 * sensorData reading;
 * reading.deviceType = DeviceType::SOIL_SENSOR;
 * reading.data.soil.soilMoisture = 45.3;
 * reading.data.soil.pH = 7.2;
 * // ... set other fields
 * ```
 */
struct sensorData {
    // === Common fields (all sensor types) ===
    DeviceType deviceType = DeviceType::SOIL_SENSOR;  ///< Sensor type identifier
    uint32_t counter = 0;                             ///< Sequence counter for sensor samples
    float battery = 0.0;                              ///< Battery voltage (V)
    uint32_t timestamp = 0;                           ///< Unix timestamp (seconds)
    uint16_t nodeId = 0;                              ///< Origin node ID
    bool error = false;                               ///< Error flag (true = read failed)
    
    // === Sensor-specific data (union for memory efficiency) ===
    union {
        // Soil sensor: 8 parameters + capacity (36 bytes)
        struct {
            float soilMoisture;      ///< Soil moisture (%) [0-100]
            float soilTemperature;   ///< Soil temperature (°C) [-10 to 60]
            float pH;                ///< Soil pH [0-14], optimal 6-7
            float conductivity;      ///< Electrical Conductivity (µS/cm) [0-10000]
            float nitrogen;          ///< Nitrogen content (mg/kg) [0-300]
            float phosphorus;        ///< Phosphorus content (mg/kg) [0-200]
            float potassium;         ///< Potassium content (mg/kg) [0-300]
            float saltContent;       ///< Salt content (mg/kg) [0-5000]
            uint16_t capacity;       ///< Soil capacity (raw ADC or calculated)
        } soil;
        
        // Environment sensor: 4 parameters (16 bytes)
        struct {
            float temperature;       ///< Air temperature (°C)
            float humidity;          ///< Relative humidity (%)
            float pressure;          ///< Atmospheric pressure (hPa)
            float light;             ///< Light intensity (lux)
        } environment;
        
        // Future sensor types can be added here
        // struct { ... } waterLevel;
        // struct { ... } weatherStation;
    } data;
    
    /**
     * @brief Initialize all fields to safe defaults
     */
    void clear() {
        deviceType = DeviceType::UNKNOWN;
        counter = 0;
        battery = 0.0;
        timestamp = 0;
        nodeId = 0;
        error = true;  // Default to error state
        
        // Clear union (soil has largest size)
        data.soil.soilMoisture = 0.0;
        data.soil.soilTemperature = 0.0;
        data.soil.pH = 0.0;
        data.soil.conductivity = 0.0;
        data.soil.nitrogen = 0.0;
        data.soil.phosphorus = 0.0;
        data.soil.potassium = 0.0;
        data.soil.saltContent = 0.0;
        data.soil.capacity = 0;
    }
};

#endif // _SENSOR_DATA_H
