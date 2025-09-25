#ifndef _MESH_UTILS_H
#define _MESH_UTILS_H

#include "components/lora_mesh_manager/include/LoraMesher.h"

// Common packet structures
struct dataPacket {
    uint32_t counter = 0;
    uint32_t timestamp = 0;
    uint16_t nodeId = 0;
};

struct sensorData {
    float temperature = 0.0;
    float humidity = 0.0; 
    float battery = 0.0;
    uint32_t timestamp = 0;
};

struct bridgeStatus {
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