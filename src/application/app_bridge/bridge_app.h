#ifndef BRIDGE_APP_H
#define BRIDGE_APP_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "bridge_config.h"
#include "led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"

// Bridge state structure
struct BridgeState {
    bool wifiConnected = false;
    bool mqttConnected = false;
    uint32_t packetsForwarded = 0;
    uint32_t lastMqttReconnect = 0;
    uint32_t totalMeshPackets = 0;
};

class BridgeApp {
public:
    BridgeApp();
    ~BridgeApp();
    
    void setup();
    void loop();

private:
    LoraMesher& radio;
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    BridgeState bridgeState;
    uint32_t statusCounter;
    bridgeStatus* statusPacket;
    
    // Private methods
    void setupLoRaMesher();
    void connectWiFi();
    void connectMQTT();
    void forwardToMQTT(AppPacket<dataPacket>* packet);
    void publishBridgeStatus();
    
    // Static callback methods
    static void processBridgePackets(void* parameter);
    TaskHandle_t createBridgeReceiveTask();
    
    // Pointer to instance for static callbacks
    static BridgeApp* instance;
};

#endif // BRIDGE_APP_H