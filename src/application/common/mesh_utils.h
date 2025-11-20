#ifndef _MESH_UTILS_H
#define _MESH_UTILS_H

#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "components/rs485_soil_sensor/include/sensor_data.h"

// Common packet structures
struct dataPacket {
    uint32_t counter = 0;
    uint32_t timestamp = 0;
    uint16_t nodeId = 0;
};

// Note: sensorData struct is now defined in components/rs485_soil_sensor/include/sensor_data.h
// This allows sensor component to be used independently without mesh dependencies

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