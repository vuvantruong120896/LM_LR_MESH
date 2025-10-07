#include "bridge_app.h"
#include "components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "mesh_security_config.h"
#include <esp_log.h>

static const char* TAG = "BRIDGE";

// Static member initialization
BridgeApp* BridgeApp::instance = nullptr;

BridgeApp::BridgeApp()
    : radio(LoraMesher::getInstance()),
      uartProtocol(nullptr),
      statusCounter(0),
      statusPacket(new bridgeStatus) {
    instance = this;
}

BridgeApp::~BridgeApp() {
    delete statusPacket;
    delete uartProtocol;
}

void BridgeApp::setup() {
    ESP_LOGI(TAG, "=== LoRaMesh Bridge/Gateway Application ===");
    ESP_LOGI(TAG, "Bridge ID: 0x%X", BRIDGE_ID);

    led_init();
    led_pattern_startup();

    // Initialize mesh security first
    if (!initializeMeshSecurity()) {
        ESP_LOGE(TAG, "Failed to initialize mesh security");
        led_pattern_error();
        return;
    }

    // Initialize NVS storage (network config persistence)
    if (!NVSStorageService::initialize()) {
        ESP_LOGW(TAG, "NVS storage initialization failed or already initialized");
    } else {
        // Try to load existing network configuration and apply
        NetworkConfig cfg;
        if (NVSStorageService::loadNetworkConfig(cfg)) {
            ESP_LOGI(TAG, "Loaded network config from NVS: version=%d, netId=0x%04X, initialized=%d, timestamp=%u",
                     cfg.keyVersion, cfg.networkId, cfg.initialized ? 1 : 0, cfg.timestamp);

            // Print networkKey (hex) and authToken (hex) for verification (only first/last bytes to avoid too verbose logs)
            char netkey_hex[16 * 2 + 1];
            for (int i = 0; i < 16; ++i) {
                sprintf(&netkey_hex[i * 2], "%02X", cfg.networkKey[i]);
            }
            netkey_hex[32] = '\0';

            char auth_hex[8 * 2 + 1];
            for (int i = 0; i < 8; ++i) {
                sprintf(&auth_hex[i * 2], "%02X", cfg.authToken[i]);
            }
            auth_hex[16] = '\0';

            ESP_LOGI(TAG, "NetworkKey: %s", netkey_hex);
            ESP_LOGI(TAG, "AuthToken: %s", auth_hex);

            // Apply local network key to mesh stack
            NetkeyDistributionService::updateLocalNetworkKey(cfg.networkKey, cfg.authToken, cfg.networkId, cfg.keyVersion);
            // Initialize dependent services that require NVS (moved to initializeServices())
            initializeServices();
        } else {
            ESP_LOGI(TAG, "No valid network config found in NVS");
        }
    }

    // Log security status
    logSecurityStatus();

    // REMOVED: Routing table NVS persistence callback
    // Network will rebuild routing table naturally via HELLO protocol after reboot
    // Benefits: Zero flash wear, no stale routes, simpler code
    
    setupLoRaMesher();
    setupUART();
    
    // REMOVED: loadRoutingTableFromNVS() - routing table no longer persisted
    // Network starts fresh and builds routes via HELLO packets (120s normal, 30s fast discovery)
    ESP_LOGI(TAG, "Routing table will be built from scratch via HELLO protocol");

    ESP_LOGI(TAG, "Bridge setup complete");
    led_pattern_connected();
}

void BridgeApp::initializeServices() {
    const int maxRetries = 3;
    const uint32_t baseDelayMs = 200; // exponential backoff base

    // Ensure AddressManagementService initialized
    if (!AddressManagementService::isInitialized()) {
        ESP_LOGI(TAG, "Initializing AddressManagementService...");
        bool ok = false;
        for (int attempt = 1; attempt <= maxRetries; ++attempt) {
            if (AddressManagementService::initialize()) {
                ESP_LOGI(TAG, "AddressManagementService initialized on attempt %d", attempt);
                ok = true; break;
            }
            uint32_t waitMs = baseDelayMs * (1 << (attempt - 1));
            ESP_LOGW(TAG, "AddressManagementService init attempt %d failed, retrying in %d ms", attempt, waitMs);
            delay(waitMs);
        }
        if (!ok) {
            ESP_LOGE(TAG, "AddressManagementService failed to initialize after %d attempts", maxRetries);
        }
    } else {
        ESP_LOGI(TAG, "AddressManagementService already initialized");
    }

    // SIMPLIFIED: No longer using complex ProvisioningService
    // All provisioning handled via direct netkey distribution
    ESP_LOGI(TAG, "Simplified provisioning via netkey distribution - no ProvisioningService needed");
}

void BridgeApp::loop() {
    uint32_t currentTime = millis();

    // Handle UART communication
    if (uartProtocol) {
        uartProtocol->update();
        updateUARTConnection();
    }
    
    // NOTE: Routing table is now saved to NVS ONLY when changes occur (node added/removed)
    // via callback mechanism. Periodic save removed to reduce flash wear.
    // See: RoutingTableService::setRoutingTableChangedCallback() in setup()

    // Simple status LED indication
    if (statusCounter++ % 100 == 0) {
        if (bridgeState.uartConnected) {
            led_pattern_message(); // Quick flash for active
        } else {
            led_pattern_error();   // Error pattern for disconnected
        }
    }

    delay(100); // Main loop delay
}

void BridgeApp::setupLoRaMesher() {
    ESP_LOGI(TAG, "Setting up LoRaMesher...");

    LoraMesher::LoraMesherConfig config;
    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;
    config.module = LORA_MODULE;

    radio.begin(config);
    
    // Set Bridge as Gateway so nodes can discover it via getClosestGateway()
    radio.addGatewayRole();
    ESP_LOGI(TAG, "Bridge configured as Gateway role");

    TaskHandle_t receiveHandle = createBridgeReceiveTask();
    if (receiveHandle) {
        ESP_LOGI(TAG, "Setting task handle %p for bridge data", receiveHandle);
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(TAG, "LoRaMesher initialized for Bridge");
    } else {
        ESP_LOGE(TAG, "Failed to create bridge receive task");
        led_pattern_error();
    }
}

void BridgeApp::setupUART() {
    ESP_LOGI(TAG, "Setting up UART communication...");

    // Create UART protocol instance
    uartProtocol = new UartProtocol(&Serial1);
    uartProtocol->begin(UART_BAUD_RATE);

    // Set callbacks for UART protocol
    uartProtocol->setNetkeyCallback(onNetkeyReceived);
    uartProtocol->setProvisioningCallback(onProvisioningControl);

    bridgeState.uartConnected = true;

    ESP_LOGI(TAG, "UART initialized on Serial1, baud: %d", UART_BAUD_RATE);
    ESP_LOGI(TAG, "RX pin: %d, TX pin: %d", UART_RX_PIN, UART_TX_PIN);
    ESP_LOGI(TAG, "Netkey and provisioning callbacks registered");
}

void BridgeApp::forwardToUART(AppPacket<sensorData>* packet) {
    if (!uartProtocol || !bridgeState.uartConnected) {
        ESP_LOGW(TAG, "UART not available for forwarding");
        return;
    }

    // Check if we're in provisioning mode (Fast Discovery)
    // Don't process/forward sensor data during provisioning - only Hello packets
    uint8_t currentMode = radio.getCurrentHelloMode();
    if (currentMode == HELLO_MODE_FAST_DISCOVERY) {
        ESP_LOGD(TAG, "Dropping sensor data packet - in Fast Discovery Mode (provisioning)");
        ESP_LOGD(TAG, "Only Hello packets are processed during provisioning for routing table building");
        return;
    }

    bridgeState.totalMeshPackets++;

    // Extract data from packet - payload contains the actual dataPacket
    if (packet->payloadSize >= sizeof(sensorData)) {
        sensorData* s = reinterpret_cast<sensorData*>(packet->payload);
        uint16_t sourceNode = packet->src;

        ESP_LOGI(TAG, "📤 Forwarding sensor data from node 0x%04X to UART", sourceNode);
        ESP_LOGI(TAG, "🔢 Counter: %u, 🌡️ Temp: %.1f°C, 💧 Hum: %.1f%%, 🔋 Batt: %.2fV, 🕒 Ts: %u", 
             s->counter, s->temperature, s->humidity, s->battery, s->timestamp);

        // Convert sensorData into the UART dataPacket format expected by external ESP32
        dataPacket dp;
        dp.counter = s->counter;
        dp.timestamp = s->timestamp;
        dp.nodeId = s->nodeId ? s->nodeId : sourceNode;

        // Send via UART
        if (uartProtocol->sendDataPacket(dp, sourceNode)) {
            bridgeState.packetsForwarded++;
            led_pattern_message(); // Flash LED on successful forward
        } else {
            bridgeState.uartErrors++;
            ESP_LOGW(TAG, "Failed to send packet via UART");
        }
    } else {
        ESP_LOGW(TAG, "Packet too small to contain sensorData structure");
    }
}

void BridgeApp::sendBridgeStatus() {
    if (!uartProtocol || !bridgeState.uartConnected) {
        return;
    }

    // Prepare status packet
    UartBridgeStatus status;
    status.bridgeId = BRIDGE_ID;
    status.uptime = millis() / 1000; // Convert to seconds
    status.connectedNodes = radio.routingTableSize();
    status.totalPacketsReceived = bridgeState.totalMeshPackets;
    status.totalPacketsSent = bridgeState.packetsForwarded;
    status.freeHeap = ESP.getFreeHeap() / 1024; // Convert to KB
    status.lastRSSI = -99; // TODO: Get from last received packet
    status.lastSNR = 10;   // TODO: Get from last received packet
    status.meshHealth = (status.connectedNodes > 0) ? 100 : 0; // Simple health metric

    ESP_LOGI(TAG, "Sending status - Nodes: %d, Packets: %d/%d, Heap: %dKB",
             status.connectedNodes, status.totalPacketsReceived, status.totalPacketsSent, status.freeHeap);

    uartProtocol->sendStatusPacket(status);
}

void BridgeApp::updateUARTConnection() {
    static uint32_t lastCheck = 0;
    uint32_t currentTime = millis();

    if (currentTime - lastCheck >= 5000) { // Check every 5 seconds
        bool wasConnected = bridgeState.uartConnected;
        bridgeState.uartConnected = uartProtocol && uartProtocol->isConnected();

        if (wasConnected != bridgeState.uartConnected) {
            if (bridgeState.uartConnected) {
                ESP_LOGI(TAG, "UART connection established");
                led_pattern_connected();
            } else {
                ESP_LOGW(TAG, "UART connection lost");
                led_pattern_error();
            }
        }

        lastCheck = currentTime;
    }
}

// Static callback for processing bridge packets
void BridgeApp::processBridgePackets(void* parameter) {
    ESP_LOGI(TAG, "[BRIDGE-TASK] Bridge packet processing task started");

    for (;;) {
        // ESP_LOGI(TAG, "[BRIDGE-TASK] Waiting for mesh packet notification...");
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        ESP_LOGI(TAG, "[BRIDGE-TASK] GOT NOTIFICATION! Processing bridge packets...");
        led_pattern_message();

        while (BridgeApp::instance->radio.getReceivedQueueSize() > 0) {
            ESP_LOGD(TAG, "[BRIDGE-TASK] Processing received mesh packet for bridge");
            ESP_LOGD(TAG, "[BRIDGE-TASK] Queue size: %d", BridgeApp::instance->radio.getReceivedQueueSize());

            AppPacket<uint8_t>* packet = BridgeApp::instance->radio.getNextAppPacket<uint8_t>();

            // Cast to the correct structure - AppPacket with sensorData payload
            AppPacket<sensorData>* sensorPacket = reinterpret_cast<AppPacket<sensorData>*>(packet);

            // Forward to UART instead of MQTT
            BridgeApp::instance->forwardToUART(sensorPacket);

            BridgeApp::instance->radio.deletePacket(packet);
        }
    }
}

TaskHandle_t BridgeApp::createBridgeReceiveTask() {
    TaskHandle_t taskHandle = NULL;

    ESP_LOGI(TAG, "Creating bridge receive task...");

    int res = xTaskCreate(
        processBridgePackets,
        "Bridge Receive Task",
        4096,
        (void*) 1,
        2,
        &taskHandle);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Error: Bridge task creation failed: %d", res);
        led_pattern_error();
        return NULL;
    }

    ESP_LOGI(TAG, "Bridge task created successfully, handle: %p", taskHandle);
    return taskHandle;
}

// Static callback for receiving netkey from UART
void BridgeApp::onNetkeyReceived(const UartNetworkKey& netkey) {
    ESP_LOGI(TAG, "*** NETKEY RECEIVED FROM UART ***");
    ESP_LOGI(TAG, "Key version: %d, Network ID: 0x%04X", netkey.keyVersion, netkey.networkId);
    
    // Build NetworkConfig for persistence
    NetworkConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.networkKey, netkey.networkKey, sizeof(cfg.networkKey));
    memcpy(cfg.authToken, netkey.authToken, sizeof(cfg.authToken));
    cfg.networkId = netkey.networkId;
    cfg.keyVersion = netkey.keyVersion;
    cfg.timestamp = netkey.timestamp;
    cfg.initialized = true;

    // Save to NVS for future use (provisioning new devices)
    if (!NVSStorageService::isInitialized()) {
        ESP_LOGW(TAG, "NVS not initialized - attempting to initialize now");
        NVSStorageService::initialize();
    }

    bool nvsSaveSuccess = false;
    if (NVSStorageService::saveNetworkConfig(cfg)) {
        ESP_LOGI(TAG, "Network config saved to NVS successfully");
        nvsSaveSuccess = true;
    } else {
        ESP_LOGE(TAG, "Failed to save network config to NVS");
    }

    // Update Bridge's local network key (for bridge operation)
    bool localUpdateSuccess = false;
    if (NetkeyDistributionService::updateLocalNetworkKey(netkey.networkKey, netkey.authToken, netkey.networkId, netkey.keyVersion)) {
        ESP_LOGI(TAG, "Bridge local network key updated successfully");
        localUpdateSuccess = true;
    } else {
        ESP_LOGE(TAG, "Failed to update Bridge local network key");
    }

    // **SIMPLIFIED APPROACH**: Automatically distribute netkey to ALL nodes in routing table
    ESP_LOGI(TAG, "*** DISTRIBUTING NETKEY TO ALL NODES IN ROUTING TABLE ***");
    
    bool distributionSuccess = false;
    size_t routingTableSize = RoutingTableService::routingTableSize();
    
    if (routingTableSize > 0) {
        ESP_LOGI(TAG, "Found %d nodes in routing table, distributing netkey...", (int)routingTableSize);
        
        // Get all nodes from routing table
        NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
        if (nodes) {
            distributionSuccess = NetkeyDistributionService::distributeNetkeyToAllNodes(
                netkey.networkKey, 
                netkey.authToken, 
                netkey.networkId, 
                netkey.keyVersion,
                nodes,
                routingTableSize
            );
            
            delete[] nodes; // Clean up allocated memory
            
            ESP_LOGI(TAG, "Network-wide netkey distribution: %s", 
                     distributionSuccess ? "SUCCESS" : "FAILED");
        } else {
            ESP_LOGW(TAG, "Failed to get nodes from routing table");
        }
    } else {
        ESP_LOGW(TAG, "No nodes in routing table - netkey saved for when nodes join");
        distributionSuccess = true; // Not an error if no nodes yet
    }

    // Send confirmation back via UART
    bool overallSuccess = nvsSaveSuccess && localUpdateSuccess && distributionSuccess;
    if (BridgeApp::instance && BridgeApp::instance->uartProtocol) {
        BridgeApp::instance->uartProtocol->sendNetkeyUpdateConfirm(overallSuccess);
    }

    ESP_LOGI(TAG, "Netkey processing completed - NVS: %s, Local: %s, Distribution: %s", 
             nvsSaveSuccess ? "SUCCESS" : "FAILED", 
             localUpdateSuccess ? "SUCCESS" : "FAILED",
             distributionSuccess ? "SUCCESS" : "FAILED");
}

// Static callback for provisioning control from UART
void BridgeApp::onProvisioningControl(const UartProvisioningControl& control) {
    ESP_LOGI(TAG, "*** SIMPLIFIED PROVISIONING CONTROL RECEIVED FROM UART ***");
    ESP_LOGI(TAG, "Action: %d (0=stop, 1=start, 2=get_status)", control.action);
    
    // SIMPLIFIED: No complex provisioning mode
    // Just acknowledge the command since netkey distribution is automatic
    
    switch (control.action) {
        case 0: { // Stop provisioning
            ESP_LOGI(TAG, "*** STOP PROVISIONING - Returning to normal operation mode ***");
            
            // Stop fast discovery mode and return to normal hello mode
            if (BridgeApp::instance) {
                // STEP 1: Stop fast discovery mode on Bridge itself
                BridgeApp::instance->radio.stopFastDiscoveryMode();
                ESP_LOGI(TAG, "Fast discovery mode stopped - Bridge returning to normal operation (120s)");
                
                // STEP 2: Broadcast command to all nodes to return to normal mode
                BridgeApp::instance->radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
                ESP_LOGI(TAG, "Broadcasted normal mode command to all nodes in network");
            }
            
            ESP_LOGI(TAG, "*** Bridge and all nodes transitioning to normal operation mode (120s intervals) ***");
            break;
        }

        case 1: { // Start provisioning  
            ESP_LOGI(TAG, "Provisioning control - Start: Triggering fast discovery mode");
            ESP_LOGI(TAG, "Duration: %dms, MaxSessions: %d", control.durationMs, control.maxSessions);
            
            // Phase 1: Start fast discovery mode for specified duration
            if (BridgeApp::instance) {
                uint32_t duration = (control.durationMs > 0) ? control.durationMs : (HELLO_DISCOVERY_DURATION * 1000);
                
                // STEP 1: Activate fast discovery mode on Bridge itself
                BridgeApp::instance->radio.startFastDiscoveryMode(duration);
                ESP_LOGI(TAG, "Fast discovery mode activated on Bridge for %dms", duration);
                
                // STEP 2: Broadcast command to all nodes to enter fast discovery mode
                BridgeApp::instance->radio.broadcastHelloModeChange(HELLO_MODE_FAST_DISCOVERY, duration);
                ESP_LOGI(TAG, "Broadcasted fast discovery mode command to all nodes in network");
            }
            
            ESP_LOGI(TAG, "*** NOTE: Fast hello mode (30s) enables quick routing table building for new nodes ***");
            break;
        }

        case 2: { // Get status
            ESP_LOGI(TAG, "Provisioning status requested");
            
            // Send simplified status
            if (BridgeApp::instance && BridgeApp::instance->uartProtocol) {
                UartProvisioningStatus status;
                memset(&status, 0, sizeof(status));
                status.active = false; // Always inactive in simplified mode
                status.remainingTimeMs = 0;
                status.activeSessions = 0;
                status.maxSessions = 0;
                status.totalRequests = 0;
                status.successfulProvisions = 0; 
                status.rejectedRequests = 0;
                
                BridgeApp::instance->uartProtocol->sendProvisioningStatus(status);
                ESP_LOGI(TAG, "Sent simplified provisioning status (always inactive)");
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown provisioning control action: %d", control.action);
            break;
    }
}

void BridgeApp::getProvisioningStatus(UartProvisioningStatus& status) const {
    // SIMPLIFIED: Always return inactive status since provisioning is replaced by netkey distribution
    memset(&status, 0, sizeof(status));
    status.active = false;
    status.remainingTimeMs = 0;
    status.activeSessions = 0;
    status.maxSessions = 0;
    status.totalRequests = 0;
    status.successfulProvisions = 0;
    status.rejectedRequests = 0;
    
    ESP_LOGI(TAG, "Simplified Provisioning Status - Always inactive, using netkey distribution");
}

// REMOVED: saveRoutingTableToNVS() - routing table no longer persisted
// Network rebuilds routes naturally via HELLO protocol
// Benefits: Zero flash wear, no stale routes, simpler code
/*
void BridgeApp::saveRoutingTableToNVS() {
    ESP_LOGI(TAG, "Saving routing table to NVS...");
    
    LM_LinkedList<RouteNode>* routingTable = RoutingTableService::routingTableList;
    if (!routingTable) {
        ESP_LOGW(TAG, "Routing table is null");
        return;
    }

    routingTable->setInUse();
    size_t totalNodes = routingTable->getLength();    if (totalNodes == 0) {
        ESP_LOGI(TAG, "Routing table is empty, clearing NVS entries");
        routingTable->releaseInUse();
        NVSStorageService::saveRoutingTable(nullptr, 0);
        return;
    }
    
    // Allocate buffer for routing entries
    RouteEntry* entries = new RouteEntry[totalNodes];
    if (!entries) {
        ESP_LOGE(TAG, "Failed to allocate memory for routing entries");
        routingTable->releaseInUse();
        return;
    }
    
    // FIX #3: Copy routing table to entries array - FILTER for NVS save
    // Only save direct neighbors (metric==1) or gateway nodes
    size_t index = 0;
    size_t filteredCount = 0;
    if (routingTable->moveToStart()) {
        do {
            RouteNode* node = routingTable->getCurrent();
            if (node && index < totalNodes) {
                // FIX #3: Only save direct neighbors (metric==1) or gateway nodes
                // Rationale: Indirect routes will be rediscovered after reboot
                bool isDirect = (node->networkNode.metric == 1);
                bool isGateway = (node->networkNode.role & ROLE_GATEWAY);
                
                if (isDirect || isGateway) {
                    // CRITICAL FIX: Set ALL fields including networkId, lastSeen, and isValid
                    entries[index] = {
                        .address = node->networkNode.address,
                        .via = node->via,
                        .metric = node->networkNode.metric,
                        .role = node->networkNode.role,
                        .networkId = node->networkNode.networkId,
                        .lastSeen = (uint32_t)(esp_timer_get_time() / 1000000),
                        .isValid = true
                    };
                    
                    ESP_LOGD(TAG, "Entry[%d]: 0x%04X via 0x%04X hops:%d role:0x%02X netId:0x%04X", 
                             index, entries[index].address, entries[index].via, 
                             entries[index].metric, entries[index].role, entries[index].networkId);
                    index++;
                } else {
                    filteredCount++;
                    ESP_LOGD(TAG, "FIX #3: Filtered indirect route from NVS save: 0x%04X via 0x%04X (hops: %d)",
                             node->networkNode.address, node->via, node->networkNode.metric);
                }
            }
        } while (routingTable->next() && index < totalNodes);
    }
    
    routingTable->releaseInUse();
    
    // Save to NVS
    uint16_t validEntries = index;
    if (validEntries > 0) {
        if (NVSStorageService::saveRoutingTable(entries, validEntries)) {
            ESP_LOGI(TAG, "Routing table saved to NVS: %d direct/gateway routes (filtered %d indirect)", 
                     validEntries, filteredCount);
        } else {
            ESP_LOGW(TAG, "Failed to save routing table to NVS");
        }
    } else {
        ESP_LOGI(TAG, "No direct/gateway routes to save to NVS (filtered %d indirect)", filteredCount);
    }
    
    delete[] entries;
}
*/

// REMOVED: loadRoutingTableFromNVS() - routing table no longer persisted
// Network rebuilds routes naturally via HELLO protocol after reboot
// Benefits:
//   - Zero flash wear (no NVS writes)
//   - No stale routes (always fresh after reboot)
//   - Simpler code (no suspend/resume logic)
//   - Faster convergence with 120s HELLO_NORMAL_INTERVAL
//
// Old implementation commented out for reference:
/*
void BridgeApp::loadRoutingTableFromNVS() {
    ESP_LOGI(TAG, "Loading routing table from NVS...");
    RouteEntry entries[50];
    uint16_t count = NVSStorageService::loadRoutingTable(entries, 50);
    if (count == 0) return;
    
    RoutingTableService::suspendCallback();
    for (uint16_t i = 0; i < count; i++) {
        NetworkNode netNode = {...};
        RoutingTableService::processRoute(entries[i].via, &netNode);
    }
    RoutingTableService::resumeCallback();
}
*/
