#ifndef GATEWAY_APP_H
#define GATEWAY_APP_H

#include <Arduino.h>
#include "gateway_config.h"
#include "firebase_client.h"
#include "wifi_connection_service.h"
#include "led_control.h"
#include "offline_data_buffer.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"
#include "components/lora_mesh_manager/src/services/NVSStorageService.h"
#include "components/lora_mesh_manager/src/services/AddressManagementService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningProtocol.h"
#include "components/lora_mesh_manager/src/services/TimeSyncService.h"
#include "provision_manager.h"
#include "../../services/firebase_command_poller.h"

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
    
    // NEW: Provisioning via Firebase command
    bool provisioningActive = false;     // True if provisioning mode active
    uint32_t provisioningStartTime = 0;  // When provisioning started
    uint32_t provisioningEndTime = 0;    // When provisioning should end
    String provisioningCommandId = "";   // Current provisioning command ID
    uint16_t nodesDiscoveredDuringProvisioning = 0; // Count of nodes discovered
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
    uint32_t sensorCounter;              // Counter for gateway sensor data
    uint32_t lastStatusUploadTime;       // Timestamp of last gateway status upload
    gatewayStatus* statusPacket;
    ProvisioningService* provisioningService;
    ProvisionManager* provisionManager;
    FirebaseCommandPoller* commandPoller;  // NEW: Command poller for Firebase commands
    
    // Counter tracking to prevent duplicate data storage
    std::map<uint16_t, uint32_t> lastProcessedCounter;  // nodeId -> last processed counter
    
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
    void uploadGatewaySensorData();     // Upload gateway's own sensor data
    void uploadRoutingTable();
    void uploadGatewayStatusPeriodic(); // Upload gateway status periodically
    sensorData simulateGatewaySensorData(); // Generate gateway sensor data
    void handleWiFiEvent(WiFiConnectionService::WiFiEvent event, int8_t rssi);
    
    // NEW: Queue-based Firebase upload helpers (non-blocking)
    void queueRoutingTableUpload(uint8_t priority = 2);        // Queue routing table upload
    void queueGatewaySensorDataUpload(uint8_t priority = 2);   // Queue gateway sensor upload
    void queueGatewayStatusUpload(uint8_t priority = 2);       // Queue gateway status upload
    
    // NEW: Command handlers for Firebase commands
    void handleStartProvisioning(const FirebaseCommandPoller::Command& cmd);
    void handleStopProvisioning(const FirebaseCommandPoller::Command& cmd);
    void handleAssignNetkey(const FirebaseCommandPoller::Command& cmd);
    void updateProvisioningProgress();  // Update provisioning progress to Firebase
    
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