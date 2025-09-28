#include "node_app.h"
#include "mesh_security_config.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"

#define LM_TAG "NodeApp"

// Static instance pointer for callbacks
static NodeApp* nodeAppInstance = nullptr;

NodeApp::NodeApp() 
    : radio(LoraMesher::getInstance()), 
      dataCounter(0), 
      nodePacket(new dataPacket),
      provisioningState(NODE_STATE_UNPROVISIONED),
      lastProvisionAttempt(0),
      provisionRetryCount(0),
      assignedAddress(0),
      hasValidNetworkKey(false),
      networkId(0),
      keyVersion(0) {
    
    nodeAppInstance = this;
    memset(deviceUUID, 0, sizeof(deviceUUID));
    memset(networkKey, 0, sizeof(networkKey));
    memset(authToken, 0, sizeof(authToken));
}

NodeApp::~NodeApp() {
    delete nodePacket;
}

void NodeApp::setup() {
    ESP_LOGI(LM_TAG, "=== LoRaMesh Node Application ===");
    ESP_LOGI(LM_TAG, "Node ID: 0x%X", NODE_ID);
    
    led_init();
    led_pattern_startup();
    
    // Initialize mesh security first
    if (!initializeMeshSecurity()) {
        ESP_LOGE(LM_TAG, "Failed to initialize mesh security");
        led_pattern_error();
        return;
    }
    
    // Log security status
    logSecurityStatus();
    
    // Initialize NVS storage service (required by ProvisioningService and AddressManagementService)
    if (!NVSStorageService::initialize()) {
        ESP_LOGW(LM_TAG, "NVS storage initialization failed or already initialized");
    } else {
        ESP_LOGI(LM_TAG, "NVS Storage Service initialized for node");
    }
    
    // **LOAD NETWORK CONFIG**: Check for persistent network credentials
    NetworkConfig cfg;
    if (NVSStorageService::loadNetworkConfig(cfg)) {
        ESP_LOGI(LM_TAG, "Found persistent network config in NVS:");
        ESP_LOGI(LM_TAG, "  Network ID: 0x%04X, Key Version: %d", cfg.networkId, cfg.keyVersion);
        ESP_LOGI(LM_TAG, "  Timestamp: %u, Initialized: %s", cfg.timestamp, cfg.initialized ? "YES" : "NO");
        
        if (cfg.initialized) {
            // Apply loaded network credentials to local state
            networkId = cfg.networkId;
            keyVersion = cfg.keyVersion;
            memcpy(networkKey, cfg.networkKey, sizeof(networkKey));
            memcpy(authToken, cfg.authToken, sizeof(authToken));
            hasValidNetworkKey = true;
            
            // Update mesh security with loaded network key
            if (MeshSecurityService::updateNetworkKey(cfg.networkKey)) {
                ESP_LOGI(LM_TAG, "Applied persistent network key to mesh security");
                
                // Update NetkeyDistributionService with loaded credentials
                NetkeyDistributionService::updateLocalNetworkKey(cfg.networkKey, cfg.authToken, cfg.networkId, cfg.keyVersion);
                
                // Mark as provisioned if we have valid credentials
                provisioningState = NODE_STATE_PROVISIONED;
                ESP_LOGI(LM_TAG, "Node restored to PROVISIONED state from persistent storage");
            } else {
                ESP_LOGW(LM_TAG, "Failed to apply persistent network key to mesh security");
                hasValidNetworkKey = false;
            }
        } else {
            ESP_LOGW(LM_TAG, "Network config found but not initialized - will need provisioning");
        }
    } else {
        ESP_LOGI(LM_TAG, "No persistent network config found - Node needs provisioning");
    }
    
    // Initialize AddressManagementService (required by ProvisioningService)
    if (!AddressManagementService::initialize()) {
        ESP_LOGW(LM_TAG, "AddressManagement initialization failed or already initialized");
    } else {
        ESP_LOGI(LM_TAG, "Address Management Service initialized for node");
    }
    
    // Initialize NetkeyDistributionService
    NetkeyDistributionService::initialize();
    NetkeyDistributionService::setNetkeyUpdateCallback(onNetkeyUpdated);
    ESP_LOGI(LM_TAG, "Netkey Distribution Service initialized for node");
    
    // Initialize ProvisioningService for Node
    if (!ProvisioningService::initialize()) {
        ESP_LOGE(LM_TAG, "Failed to initialize Provisioning Service");
        led_pattern_error();
        return;
    }
    // Register callback to receive provisioning packets
    ProvisioningService::setNodePacketCallback(onProvisioningPacketReceived);
    ESP_LOGI(LM_TAG, "Provisioning Service initialized for node");
    
    // Initialize provisioning
    ESP_LOGI(LM_TAG, "Initializing node provisioning...");
    generateDeviceUUID();
    
    setupLoRaMesher();

    ESP_LOGI(LM_TAG, "Node setup complete. Send interval: %d ms", SEND_INTERVAL_MS);
    ESP_LOGI(LM_TAG, "Node provisioning state: %s", 
             (provisioningState == NODE_STATE_UNPROVISIONED) ? "UNPROVISIONED" :
             (provisioningState == NODE_STATE_PROVISIONING) ? "PROVISIONING" :
             (provisioningState == NODE_STATE_PROVISIONED) ? "PROVISIONED" : "FAILED");
}

void NodeApp::loop() {
    // Send sensor data periodically if provisioned
    if (provisioningState == NODE_STATE_PROVISIONED && hasValidNetworkKey) {
        static uint32_t lastDataSend = 0;
        uint32_t currentTime = millis();
        
        if (currentTime - lastDataSend >= SEND_INTERVAL_MS) {
            // Create and send sensor data
            sensorData s = simulateSensorData();

            // Populate sensor metadata
            s.counter = ++dataCounter;
            s.timestamp = currentTime;
            s.nodeId = assignedAddress ? assignedAddress : NODE_ID;

            ESP_LOGI(LM_TAG, "Sending sensor data #%d - Temp: %.1f°C, Hum: %.1f%%, Batt: %.2fV", 
                     s.counter, s.temperature, s.humidity, s.battery);

            // Send sensorData struct to Bridge (use createPacketAndSend so secure wrapping is applied when enabled)
            radio.createPacketAndSend<sensorData>(BROADCAST_ADDR, &s, 1);
            led_pattern_message(); // Flash LED to indicate data sent

            lastDataSend = currentTime;
        }
    } else {
        // Node is not provisioned - wait for netkey from Bridge
        static uint32_t lastStatusLog = 0;
        uint32_t currentTime = millis();
        
        if (currentTime - lastStatusLog >= 30000) { // Log every 30 seconds
            ESP_LOGI(LM_TAG, "Waiting for network key from Bridge... State: %s", 
                     (provisioningState == NODE_STATE_PROVISIONED) ? "PROVISIONED" : "UNPROVISIONED");
            lastStatusLog = currentTime;
        }
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Check every 1 second instead of SEND_INTERVAL_MS
}

sensorData NodeApp::simulateSensorData() {
    sensorData data;
    data.temperature = 20.0 + (random(0, 200) / 10.0); // 20-40°C
    data.humidity = 40.0 + (random(0, 600) / 10.0);    // 40-100%
    data.battery = 3.2 + (random(0, 80) / 100.0);      // 3.2-4.0V
    data.timestamp = millis();
    return data;
}

void NodeApp::setupLoRaMesher() {
    LoraMesher::LoraMesherConfig config;
    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;
    config.module = LORA_MODULE;
    
    radio.begin(config);
    
    // Use regular receive task - provisioning now handled by ProvisioningService
    TaskHandle_t receiveHandle = createReceiveTask("Node Receive Task");
    if (receiveHandle) {
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(LM_TAG, "LoRaMesher initialized with ProvisioningService integration");
        led_pattern_connected();
    } else {
        ESP_LOGE(LM_TAG, "Failed to initialize LoRaMesher");
        led_pattern_error();
    }
}

// Static callback for netkey updates from Bridge
void NodeApp::onNetkeyUpdated(const uint8_t* newKey, uint8_t version) {
    ESP_LOGI(LM_TAG, "*** NETWORK KEY UPDATED ***");
    ESP_LOGI(LM_TAG, "New key version: %d", version);
    ESP_LOGI(LM_TAG, "Key (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X...",
             newKey[0], newKey[1], newKey[2], newKey[3],
             newKey[4], newKey[5], newKey[6], newKey[7]);
    
    // Visual indication of key update
    led_pattern_message(); // Flash LED to indicate key update
    
    // **PERSIST TO NVS**: Create NetworkConfig and save for reboot survival
    NetworkConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    // Populate with received data
    memcpy(cfg.networkKey, newKey, sizeof(cfg.networkKey));
    cfg.keyVersion = version;
    cfg.timestamp = millis();
    cfg.initialized = true;
    
    // Get current security config to extract auth token (if available)
    const MeshSecurityConfig& secConfig = MeshSecurityService::getConfig();
    memcpy(cfg.authToken, secConfig.authToken, sizeof(cfg.authToken));
    
    // Get Network ID from NetkeyDistributionService 
    cfg.networkId = NetkeyDistributionService::getLocalNetworkId();
    
    // Save network config to NVS
    if (NVSStorageService::saveNetworkConfig(cfg)) {
        ESP_LOGI(LM_TAG, "Network credentials saved to NVS successfully");
        ESP_LOGI(LM_TAG, "  Key Version: %d, Timestamp: %u", cfg.keyVersion, cfg.timestamp);
        
        // Update instance state if we have access to it
        if (nodeAppInstance) {
            nodeAppInstance->hasValidNetworkKey = true;
            nodeAppInstance->keyVersion = version;
            memcpy(nodeAppInstance->networkKey, newKey, sizeof(nodeAppInstance->networkKey));
            memcpy(nodeAppInstance->authToken, secConfig.authToken, sizeof(nodeAppInstance->authToken));
            
            // Mark as provisioned
            if (nodeAppInstance->provisioningState != NODE_STATE_PROVISIONED) {
                nodeAppInstance->provisioningState = NODE_STATE_PROVISIONED;
                ESP_LOGI(LM_TAG, "Node state updated to PROVISIONED");
            }
        }
    } else {
        ESP_LOGE(LM_TAG, "Failed to save network credentials to NVS");
    }
    
    ESP_LOGI(LM_TAG, "Node network credentials updated - will survive reboot");
}

// Static callback for provisioning packets from ProvisioningService
void NodeApp::onProvisioningPacketReceived(uint8_t packetType, const uint8_t* packet, 
                                         size_t packetSize, uint16_t senderAddress) {
    ESP_LOGI(LM_TAG, "Received provisioning packet type 0x%02X from 0x%04X", packetType, senderAddress);
    
    if (!nodeAppInstance) {
        ESP_LOGE(LM_TAG, "NodeApp instance not available for callback");
        return;
    }
    
    // Handle different packet types
    if (packetType == PROVISION_RESPONSE_PACKET) {
        if (packetSize >= sizeof(ProvisionResponsePacket)) {
            const ProvisionResponsePacket* response = 
                reinterpret_cast<const ProvisionResponsePacket*>(packet);
            nodeAppInstance->handleProvisionResponse(response);
        } else {
            ESP_LOGW(LM_TAG, "Provision response packet too small: %d bytes", packetSize);
        }
    } else if (packetType == PROVISION_REJECT_PACKET) {
        if (packetSize >= sizeof(ProvisionRejectPacket)) {
            const ProvisionRejectPacket* reject = 
                reinterpret_cast<const ProvisionRejectPacket*>(packet);
            nodeAppInstance->handleProvisionReject(reject);
        } else {
            ESP_LOGW(LM_TAG, "Provision reject packet too small: %d bytes", packetSize);
        }
    } else {
        ESP_LOGW(LM_TAG, "Unknown provisioning packet type: 0x%02X", packetType);
    }
}

void NodeApp::generateDeviceUUID() {
    // Generate UUID based on ESP32 MAC address for uniqueness
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // Simple UUID format: MAC + random bytes
    memcpy(deviceUUID, mac, 6);
    
    // Add 10 more random bytes
    for (int i = 6; i < 16; i++) {
        deviceUUID[i] = esp_random() & 0xFF;
    }
    
    ESP_LOGI(LM_TAG, "Generated device UUID");
}

bool NodeApp::isNetworkConfigured() {
    return hasValidNetworkKey && (assignedAddress != 0) && (provisioningState == NODE_STATE_PROVISIONED);
}

void NodeApp::checkProvisioningState() {
    uint32_t currentTime = millis();
    
    switch (provisioningState) {
        case NODE_STATE_UNPROVISIONED:
            // Generate UUID if not already done
            if (deviceUUID[0] == 0 && deviceUUID[1] == 0) {
                generateDeviceUUID();
            }
            
            // Try to send provision request
            if (currentTime - lastProvisionAttempt >= PROVISION_RETRY_INTERVAL_MS) {
                ESP_LOGI(LM_TAG, "Attempting to provision (attempt %d)", provisionRetryCount + 1);
                if (sendProvisionRequest()) {
                    provisioningState = NODE_STATE_PROVISIONING;
                    lastProvisionAttempt = currentTime;
                    provisionRetryCount++;
                } else {
                    ESP_LOGE(LM_TAG, "Failed to send provision request");
                    lastProvisionAttempt = currentTime;
                }
            }
            break;
            
        case NODE_STATE_PROVISIONING:
            // Check for timeout
            if (currentTime - lastProvisionAttempt >= PROVISION_TIMEOUT_MS) {
                ESP_LOGW(LM_TAG, "Provision request timed out");
                if (provisionRetryCount >= MAX_PROVISION_RETRIES) {
                    provisioningState = NODE_STATE_PROVISION_FAILED;
                    ESP_LOGE(LM_TAG, "Max provision retries exceeded");
                } else {
                    provisioningState = NODE_STATE_UNPROVISIONED; // Retry
                }
            }
            break;
            
        case NODE_STATE_PROVISION_FAILED:
            // Wait longer before retrying after failure
            if (currentTime - lastProvisionAttempt >= PROVISION_FAILURE_RETRY_INTERVAL_MS) {
                ESP_LOGI(LM_TAG, "Retrying provisioning after failure");
                provisioningState = NODE_STATE_UNPROVISIONED;
                provisionRetryCount = 0;
            }
            break;
            
        case NODE_STATE_PROVISIONED:
            // Already provisioned, nothing to do
            break;
    }
}

bool NodeApp::sendProvisionRequest() {
    // Create provision request packet
    ProvisionRequestPacket request;
    bool success = ProvisioningProtocol::createProvisionRequest(
        deviceUUID, 
        CAPABILITY_BASIC_NODE,      // device type
        "ESP32-Node",              // device name
        AUTH_METHOD_PSK,            // auth method
        request);
    
    if (!success) {
        ESP_LOGE(LM_TAG, "Failed to create provision request");
        return false;
    }
    
    ESP_LOGI(LM_TAG, "Sending provision request to network");
    radio.sendReliablePacket(BROADCAST_ADDR, (uint8_t*)&request, sizeof(request));
    return true; // sendReliablePacket is void, assume success
}

void NodeApp::handleProvisionResponse(const ProvisionResponsePacket* response) {
    if (provisioningState != NODE_STATE_PROVISIONING) {
        ESP_LOGW(LM_TAG, "Received provision response but not in provisioning state");
        return;
    }
    
    ESP_LOGI(LM_TAG, "Received provision response");
    ESP_LOGI(LM_TAG, "Assigned address: 0x%04X", response->assignedAddress);
    ESP_LOGI(LM_TAG, "Network ID: %d", response->networkId);
    ESP_LOGI(LM_TAG, "Key version: %d", response->keyVersion);
    
    // Apply network credentials
    applyNetworkCredentials(response);
    
    // Send provision complete
    if (sendProvisionComplete()) {
        provisioningState = NODE_STATE_PROVISIONED;
        ESP_LOGI(LM_TAG, "*** PROVISIONING COMPLETED SUCCESSFULLY ***");
        led_pattern_connected();
    } else {
        ESP_LOGE(LM_TAG, "Failed to send provision complete");
        provisioningState = NODE_STATE_PROVISION_FAILED;
        led_pattern_error();
    }
}

void NodeApp::handleProvisionReject(const ProvisionRejectPacket* reject) {
    ESP_LOGW(LM_TAG, "Provision request rejected. Reason: %d", reject->rejectReason);
    provisioningState = NODE_STATE_PROVISION_FAILED;
    led_pattern_error();
}

bool NodeApp::sendProvisionComplete() {
    ProvisionCompletePacket complete;
    bool success = ProvisioningProtocol::createProvisionComplete(
        assignedAddress, 
        PROVISION_SUCCESS, 
        complete);
    
    if (!success) {
        ESP_LOGE(LM_TAG, "Failed to create provision complete packet");
        return false;
    }
    
    ESP_LOGI(LM_TAG, "Sending provision complete confirmation");
    radio.sendReliablePacket(BROADCAST_ADDR, (uint8_t*)&complete, sizeof(complete));
    return true; // sendReliablePacket is void, assume success
}

void NodeApp::applyNetworkCredentials(const ProvisionResponsePacket* response) {
    // Store network credentials
    assignedAddress = response->assignedAddress;
    networkId = response->networkId;
    keyVersion = response->keyVersion;
    memcpy(networkKey, response->networkKey, sizeof(networkKey));
    memcpy(authToken, response->authToken, sizeof(authToken));
    
    // Apply the network key to mesh security
    MeshSecurityService::updateNetworkKey(networkKey);
    hasValidNetworkKey = true;
    
    ESP_LOGI(LM_TAG, "Applied network credentials:");
    ESP_LOGI(LM_TAG, "  Address: 0x%04X", assignedAddress);
    ESP_LOGI(LM_TAG, "  Network ID: %d", networkId);
    ESP_LOGI(LM_TAG, "  Key version: %d", keyVersion);
}