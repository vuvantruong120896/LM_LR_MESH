#ifndef GATEWAY_APP_H
#define GATEWAY_APP_H

#include <Arduino.h>
#include "gateway_config.h"
#include "firebase_client.h"
#include "wifi_connection_service.h"
#include "led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"
#include "components/lora_mesh_manager/src/services/NVSStorageService.h"
#include "components/lora_mesh_manager/src/services/AddressManagementService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningProtocol.h"
#include "components/lora_mesh_manager/src/services/TimeSyncService.h"

// Gateway state structure
struct GatewayState {
    bool wifiConnected = false;
    bool firebaseConnected = false;
    uint32_t packetsUploaded = 0;
    uint32_t lastRoutingTableUpload = 0;
    uint32_t totalMeshPackets = 0;
    uint32_t uploadErrors = 0;
    uint32_t bootTime = 0;
    uint32_t lastTimeSyncBroadcast = 0;  // Time of last time sync broadcast
    bool ntpSynced = false;              // True if NTP time sync successful
};

class GatewayApp {
public:
    GatewayApp();
    ~GatewayApp();
    
    void setup();
    void loop();
    
    // Status and management methods
    bool isNetworkConfigured() const;
    bool getNetworkConfig(NetworkConfig& config) const;
    AddressManagementService::AllocationResult allocateNodeAddress(const uint8_t* deviceUUID, const char* deviceName);
    bool isNodeProvisioned(uint16_t address) const;
    uint8_t getActiveProvisioningSessions() const;
    bool isProvisioningReady() const;

private:
    LoraMesher& radio;
    WiFiConnectionService* wifiService;
    FirebaseClient* firebaseClient;
    GatewayState gatewayState;
    uint32_t statusCounter;
    gatewayStatus* statusPacket;
    ProvisioningService* provisioningService;
    
    // Private methods
    void setupLoRaMesher();
    void setupWiFi();
    void setupFirebase();
    void setupTimeSync();
    void broadcastTimeSync();
    void initializeServices();
    void initializeNVSStorage();
    void loadNetworkConfiguration();
    void printSystemStatus();
    void uploadToFirebase(AppPacket<sensorData>* packet);
    void uploadRoutingTable();
    void handleWiFiEvent(WiFiConnectionService::WiFiEvent event, int8_t rssi);
    
    // Static callbacks
    static void onNetkeyUpdated(const uint8_t* newKey, uint8_t version);
    static void onRoutingTableChanged();  // Called when routing table changes (add/remove nodes)
    
    // Static callback methods
    static void processGatewayPackets(void* parameter);
    static void processProvisioningPackets(void* parameter);
    TaskHandle_t createGatewayReceiveTask();
    
    // Provisioning packet handlers
    void handleProvisioningPacket(AppPacket<DataPacket>* packet);
    static void handleProvisionRequest(const uint8_t* packetData, size_t packetSize, uint16_t senderAddress);
    static void handleProvisionComplete(const uint8_t* packetData, size_t packetSize, uint16_t senderAddress);
    
    // Pointer to instance for static callbacks
    static GatewayApp* instance;
};

#endif // GATEWAY_APP_H