#ifndef BRIDGE_APP_H
#define BRIDGE_APP_H

#include <Arduino.h>
#include "bridge_config.h"
#include "uart_protocol.h"
#include "led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"

// Bridge state structure
struct BridgeState {
    bool uartConnected = false;
    uint32_t packetsForwarded = 0;
    uint32_t lastHeartbeat = 0;
    uint32_t lastStatusSent = 0;
    uint32_t totalMeshPackets = 0;
    uint32_t uartErrors = 0;
};

class BridgeApp {
public:
    BridgeApp();
    ~BridgeApp();
    
    void setup();
    void loop();

private:
    LoraMesher& radio;
    UartProtocol* uartProtocol;
    BridgeState bridgeState;
    uint32_t statusCounter;
    bridgeStatus* statusPacket;
    
    // Private methods
    void setupLoRaMesher();
    void setupUART();
    void forwardToUART(AppPacket<dataPacket>* packet);
    void sendBridgeStatus();
    void updateUARTConnection();
    
    // Static callback methods
    static void processBridgePackets(void* parameter);
    TaskHandle_t createBridgeReceiveTask();
    
    // Pointer to instance for static callbacks
    static BridgeApp* instance;
};

#endif // BRIDGE_APP_H