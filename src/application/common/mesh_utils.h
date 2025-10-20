#ifndef _MESH_UTILS_H
#define _MESH_UTILS_H

#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "device_type.h"

// Common packet structures
struct dataPacket {
    uint32_t counter = 0;
    uint32_t timestamp = 0;
    uint16_t nodeId = 0;
};

/**
 * @brief Unified sensor data structure supporting multiple sensor types
 * 
 * This structure uses a union to efficiently store different sensor types
 * while maintaining a small memory footprint. The deviceType field determines
 * which union member is active.
 * 
 * Memory layout:
 * - Fixed fields: 16 bytes (deviceType, counter, battery, timestamp, nodeId)
 * - Union data: 28 bytes max (soil sensor has most params)
 * - Total: ~44 bytes
 */
struct sensorData {
    // === Common fields (all sensor types) ===
    DeviceType deviceType = DeviceType::SOIL_SENSOR;  ///< Sensor type identifier
    uint32_t counter = 0;                             ///< Sequence counter for sensor samples
    float battery = 0.0;                              ///< Battery voltage (V)
    uint32_t timestamp = 0;                           ///< Unix timestamp (seconds)
    uint16_t nodeId = 0;                              ///< Origin node ID
    
    // === Sensor-specific data (union for memory efficiency) ===
    union {
        // Soil sensor: 7 parameters (28 bytes)
        struct {
            float soilMoisture;      ///< Soil moisture (%) [0-100]
            float soilTemperature;   ///< Soil temperature (°C) [-10 to 60]
            float pH;                ///< Soil pH [0-14], optimal 6-7
            float ec;                ///< Electrical Conductivity (mS/cm) [0-10]
            float nitrogen;          ///< Nitrogen content (mg/kg) [0-300]
            float phosphorus;        ///< Phosphorus content (mg/kg) [0-200]
            float potassium;         ///< Potassium content (mg/kg) [0-300]
        } soil;
        
        // Environment sensor: 4 parameters (16 bytes)
        struct {
            float temperature;       ///< Air temperature (°C)
            float humidity;          ///< Relative humidity (%)
            float pressure;          ///< Atmospheric pressure (hPa)
            float lightIntensity;    ///< Light intensity (lux)
        } environment;
        
        // Water sensor: 4 parameters (16 bytes)
        struct {
            float waterTemp;         ///< Water temperature (°C)
            float pH;                ///< Water pH [0-14]
            float tds;               ///< Total Dissolved Solids (ppm)
            float turbidity;         ///< Water turbidity (NTU)
        } water;
        
        // Generic array for custom sensors (32 bytes)
        float values[8];             ///< Generic float array for custom sensors
    } data;
};

struct gatewayStatus {
    uint16_t connectedNodes = 0;
    uint32_t totalPackets = 0;
    bool wifiConnected = false;
    bool mqttConnected = false;
};

// Common mesh functions
void printPacket(const dataPacket& data);
void printDataPacket(AppPacket<dataPacket>* packet);
void printSensorData(const sensorData& data);
void setupLoRaMesher(LoraMesher::LoraMesherConfig& config);
TaskHandle_t createReceiveTask(const char* taskName);

#endif // _MESH_UTILS_H