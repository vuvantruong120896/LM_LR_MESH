#ifndef NODE_APP_H
#define NODE_APP_H

#include <Arduino.h>
#include "node_config.h"
#include "led_control.h"
#include "provision_manager_node.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"
#include "components/lora_mesh_manager/src/services/NVSStorageService.h"
#include "components/lora_mesh_manager/src/services/AddressManagementService.h"
#include "components/lora_mesh_manager/src/services/ProvisioningProtocol.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/include/mesh_security.h"
#include "components/lora_mesh_manager/src/services/TimeSyncService.h"

// Node provisioning states (kept for backward compatibility, but now using BLE provisioning)
enum NodeProvisioningState {
    NODE_STATE_UNPROVISIONED = 0,   // No network key, need to provision via BLE
    NODE_STATE_PROVISIONING = 1,    // BLE provisioning in progress (deprecated)
    NODE_STATE_PROVISIONED = 2,     // Has valid network key, fully joined
    NODE_STATE_PROVISION_FAILED = 3 // Provisioning failed (deprecated)
};

class NodeApp {
public:
    NodeApp();
    ~NodeApp();
    
    void setup();
    void loop();
    
    // Public method for time sync packet handling (called from mesh_utils)
    void handleTimeSyncPacket(AppPacket<TimeSyncService::TimeSyncPacket>* packet);
    
    // Public getter for provision manager (for gateway address lookup)
    ProvisionManagerNode* getProvisionManager() const { return provisionManager; }

private:
    LoraMesher& radio;
    uint32_t dataCounter;
    dataPacket* nodePacket;
    
    // BLE Provisioning Manager (NEW)
    ProvisionManagerNode* provisionManager;
    
    // Provisioning state management (deprecated - now using BLE provisioning)
    NodeProvisioningState provisioningState;
    uint32_t lastProvisionAttempt;
    uint8_t provisionRetryCount;
    uint8_t deviceUUID[16];
    uint16_t assignedAddress;
    bool hasValidNetworkKey;
    
    // Network credentials (in-memory only for now)
    uint8_t networkKey[16];
    uint8_t authToken[8];
    uint16_t networkId;
    uint8_t keyVersion;
    
    // Private methods
    sensorData simulateSensorData();
    void setupLoRaMesher();
    void generateDeviceUUID();
    bool isNetworkConfigured();
    void setupTimeSync();
    
    // Provisioning methods
    void checkProvisioningState();
    bool sendProvisionRequest();
    void handleProvisionResponse(const ProvisionResponsePacket* response);
    void handleProvisionReject(const ProvisionRejectPacket* reject);
    bool sendProvisionComplete();
    void applyNetworkCredentials(const ProvisionResponsePacket* response);
    
    // Network key handling
    static void onNetkeyUpdated(const uint8_t* newKey, uint8_t version);

    // Provisioning packet callback from ProvisioningService
    static void onProvisioningPacketReceived(uint8_t packetType, const uint8_t* packet, 
                                           size_t packetSize, uint16_t senderAddress);
    
    // Time sync packet receiver
    static void processTimeSyncPackets(void* parameter);
    TaskHandle_t createTimeSyncReceiveTask();
    
public:
    // Instance pointer for static callbacks (public for mesh_utils access)
    static NodeApp* instance;
    
    // Time sync task handle (for notifications from main receive task)
    static TaskHandle_t timeSyncTaskHandle;
};

#endif // NODE_APP_H