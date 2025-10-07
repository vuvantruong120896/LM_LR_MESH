#ifndef BRIDGE_APP_H
#define BRIDGE_APP_H

#include <Arduino.h>
#include "bridge_config.h"
#include "uart_protocol.h"
#include "led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"
#include "components/lora_mesh_manager/src/services/NVSStorageService.h"
#include "components/lora_mesh_manager/src/services/AddressManagementService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningProtocol.h"

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
    
    // Status and management methods
    bool isNetworkConfigured() const;
    bool getNetworkConfig(NetworkConfig& config) const;
    AddressManagementService::AllocationResult allocateNodeAddress(const uint8_t* deviceUUID, const char* deviceName);
    bool isNodeProvisioned(uint16_t address) const;
    uint8_t getActiveProvisioningSessions() const;
    bool isProvisioningReady() const;
    void getProvisioningStatus(UartProvisioningStatus& status) const;

private:
    LoraMesher& radio;
    UartProtocol* uartProtocol;
    BridgeState bridgeState;
    uint32_t statusCounter;
    bridgeStatus* statusPacket;
    ProvisioningService* provisioningService;
    
    // Private methods
    void setupLoRaMesher();
    void setupUART();
    void initializeServices();
    void initializeNVSStorage();
    void loadNetworkConfiguration();
    void printSystemStatus();
    void forwardToUART(AppPacket<sensorData>* packet);
    void sendBridgeStatus();
    void updateUARTConnection();
    
    // REMOVED: Routing table persistence functions
    // Routing table no longer saved to NVS - rebuilds naturally via HELLO protocol
    // static void saveRoutingTableToNVS();
    // static void loadRoutingTableFromNVS();
    
    // Static callbacks
    static void onNetkeyReceived(const UartNetworkKey& netkey);
    static void onNetkeyUpdated(const uint8_t* newKey, uint8_t version);
    static void onProvisioningControl(const UartProvisioningControl& control);
    
    // Static callback methods
    static void processBridgePackets(void* parameter);
    static void processProvisioningPackets(void* parameter);
    TaskHandle_t createBridgeReceiveTask();
    
    // Provisioning packet handlers
    void handleProvisioningPacket(AppPacket<DataPacket>* packet);
    static void handleProvisionRequest(const uint8_t* packetData, size_t packetSize, uint16_t senderAddress);
    static void handleProvisionComplete(const uint8_t* packetData, size_t packetSize, uint16_t senderAddress);
    
    // Pointer to instance for static callbacks
    static BridgeApp* instance;
};

#endif // BRIDGE_APP_H