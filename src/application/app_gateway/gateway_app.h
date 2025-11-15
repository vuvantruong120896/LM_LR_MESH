#ifndef GATEWAY_APP_H
#define GATEWAY_APP_H

#include <Arduino.h>
#include "gateway_config.h"

// Conditional compilation: WiFi or Cellular mode
#ifdef USE_CELLULAR
    #include "components/cellular/include/cellular_connection_service.h"
    #include "components/cellular/include/cellular_ssl_client.h"
    #include "components/cellular/include/cellular_firebase_https_client.h"
    #include "components/cellular/include/firebase_command_queue.h"
    #include "components/cellular/include/cellular_firebase_queue.h"
    #include "../../services/cellular_firebase_command_poller.h"
#else
    #include "firebase_client.h"
    #include "wifi_connection_service.h"
    #include "../../services/firebase_command_poller.h"
#endif

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

// Gateway state structure
struct GatewayState {
#ifdef USE_CELLULAR
    bool cellularConnected = false;     // Cellular connection status
    int8_t cellularRSSI = 0;            // Cellular signal strength
#else
    bool wifiConnected = false;
#endif
    bool firebaseConnected = false;
    uint32_t packetsUploaded = 0;
    uint32_t lastRoutingTableUpload = 0;
    volatile bool routingTableUploadPending = false;
    uint8_t routingTableUploadPriority = 2;
    uint32_t lastRoutingTableChange = 0;
    uint32_t totalMeshPackets = 0;
    uint32_t uploadErrors = 0;
    uint32_t bootTime = 0;
    uint32_t lastTimeSyncBroadcast = 0;
    bool ntpSynced = false;
    
    // Time sync state (loop-based, periodic with retry)
    uint32_t lastNtpSyncAttempt = 0;
    uint32_t lastSuccessfulNtpSync = 0;
    uint8_t ntpRetryCount = 0;
    bool ntpSyncInProgress = false;
    
    // Provisioning via Firebase command
    bool provisioningActive = false;
    uint32_t provisioningStartTime = 0;
    uint32_t provisioningEndTime = 0;
    String provisioningCommandId = "";
    uint16_t nodesDiscoveredDuringProvisioning = 0;
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
    
#ifdef USE_CELLULAR
    // Cellular components
    CellularConnectionService* cellularService;
    CellularSSLClient* sslClient;
    CellularFirebaseHTTPSClient* firebaseClient;
    CellularFirebaseCommandPoller* cellularCommandPoller;
    FirebaseCommandQueue* commandQueue;
#else
    // WiFi components
    WiFiConnectionService* wifiService;
    FirebaseClient* firebaseClient;
    FirebaseCommandPoller* commandPoller;
#endif
    
    GatewayState gatewayState;
    uint32_t statusCounter;
    uint32_t sensorCounter;
    uint32_t lastStatusUploadTime;
    gatewayStatus* statusPacket;
    ProvisioningService* provisioningService;
    ProvisionManager* provisionManager;
    
    // Counter tracking to prevent duplicate data storage
    std::map<uint16_t, uint32_t> lastProcessedCounter;
    
    // 🔔 Pending buffer upload tracking (callback system)
    struct PendingBufferItem {
        String nodeId;           // Node identifier for NVS buffer removal
        sensorData data;         // Copy of sensor data for re-queueing
        uint32_t queuedAtMs;     // Timestamp when queued
        uint8_t attempts;        // Retry count from queue
    };
    std::map<uint16_t, PendingBufferItem> m_pendingBufferUploads;  // Track items waiting for upload confirmation
    static constexpr uint32_t PENDING_UPLOAD_TIMEOUT_MS = 15000;   // 15 second timeout for pending uploads
    
    // Private methods
    void setupLoRaMesher();
    
#ifdef USE_CELLULAR
    void setupCellular();
#else
    void setupWiFi();
#endif
    
    void setupFirebase();
    void setupTimeSync();
    void broadcastTimeSync();
    
    // 🔔 Callback handlers for queue upload results
#ifdef USE_CELLULAR
    void handleQueueUploadResult(CellularFirebaseQueue::Operation op, bool success, const sensorData* data, uint8_t attempts);
    void processPendingBufferUploads();  // Timeout handler for pending items
#endif
    
#ifdef USE_CELLULAR
    bool syncTimeFromModem();  // Get time from cellular modem via AT+CCLK?
    bool getTimeFromHTTPSAPI(int& year, int& month, int& day, int& hour, int& minute, int& second);  // Get time from HTTPS API
#endif
    
    void initializeServices();
    void initializeNVSStorage();
    void loadNetworkConfiguration();
    void printSystemStatus();
    void uploadToFirebase(AppPacket<sensorData>* packet);
    void uploadGatewaySensorData();
    void uploadRoutingTable();
    void uploadGatewayStatusPeriodic();
    sensorData simulateGatewaySensorData();
    
#ifdef USE_CELLULAR
    void handleCellularEvent(CellularConnectionService::Event event, int8_t rssi);
#else
    void handleWiFiEvent(WiFiConnectionService::WiFiEvent event, int8_t rssi);
#endif
    
    // NEW: Queue-based Firebase upload helpers (non-blocking)
    void queueRoutingTableUpload(uint8_t priority = 2);        // Queue routing table upload
    void queueGatewaySensorDataUpload(uint8_t priority = 2);   // Queue gateway sensor upload
    void queueGatewayStatusUpload(uint8_t priority = 2);       // Queue gateway status upload
    
    // Command handlers for Firebase commands (both WiFi and Cellular)
#ifdef USE_CELLULAR
    typedef CellularFirebaseCommandPoller::Command CommandType;
    void handleStartProvisioning(const CellularFirebaseCommandPoller::Command& cmd);
    void handleStopProvisioning(const CellularFirebaseCommandPoller::Command& cmd);
    void handleAssignNetkey(const CellularFirebaseCommandPoller::Command& cmd);
#else
    typedef FirebaseCommandPoller::Command CommandType;
    void handleStartProvisioning(const FirebaseCommandPoller::Command& cmd);
    void handleStopProvisioning(const FirebaseCommandPoller::Command& cmd);
    void handleAssignNetkey(const FirebaseCommandPoller::Command& cmd);
#endif
    void updateProvisioningProgress();  // Update provisioning progress to Firebase
    
    // Netkey distribution worker (runs on CPU1 to avoid blocking CPU0)
    struct NetkeyDistributionTask {
        CommandType cmd;
        NetworkConfig config;
        bool* completed;
    };
    static void netkeyDistributionWorker(void* parameter);
    TaskHandle_t m_netkeyWorkerHandle = nullptr;
    
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