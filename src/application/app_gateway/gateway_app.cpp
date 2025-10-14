#include "gateway_app.h"
#include "components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "mesh_security_config.h"
#include <esp_log.h>

static const char* TAG = "GATEWAY";

// Static member initialization
GatewayApp* GatewayApp::instance = nullptr;

GatewayApp::GatewayApp()
    : radio(LoraMesher::getInstance()),
      wifiService(nullptr),
      firebaseClient(nullptr),
      statusCounter(0),
      statusPacket(new gatewayStatus) {
    instance = this;
    gatewayState.bootTime = millis();
}

GatewayApp::~GatewayApp() {
    delete statusPacket;
    delete firebaseClient;
    delete wifiService;
}

void GatewayApp::setup() {
    ESP_LOGI(TAG, "=== LoRaMesh Gateway Application ===");
    ESP_LOGI(TAG, "Gateway ID: 0x%X", GATEWAY_ID);

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
    
    setupLoRaMesher();
    setupWiFi();
    setupFirebase();
    
    // Register callback to upload routing table when it changes
    RoutingTableService::setRoutingTableChangedCallback(onRoutingTableChanged);
    ESP_LOGI(TAG, "Registered routing table change callback for real-time Firebase upload");
    
    // Routing table will be built from scratch via HELLO packets
    ESP_LOGI(TAG, "Routing table will be built from scratch via HELLO protocol");

    ESP_LOGI(TAG, "Gateway setup complete");
    led_pattern_connected();
}

void GatewayApp::initializeServices() {
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

void GatewayApp::loop() {
    uint32_t currentTime = millis();
    
    // Memory leak detection - log heap status every 30 seconds
    static uint32_t lastHeapCheck = 0;
    static uint32_t minFreeHeapGlobal = ESP.getFreeHeap();
    if (currentTime - lastHeapCheck >= 30000) {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t largestFreeBlock = ESP.getMaxAllocHeap();
        
        if (freeHeap < minFreeHeapGlobal) {
            minFreeHeapGlobal = freeHeap;
        }
        
        ESP_LOGI(TAG, "[MEMORY] Free heap: %u bytes (min: %u), Largest block: %u bytes, Uptime: %u min",
                 freeHeap, minFreeHeapGlobal, largestFreeBlock, currentTime / 60000);
        
        // Warn if heap fragmentation detected (large gap between free and largest block)
        if (freeHeap > 50000 && largestFreeBlock < (freeHeap / 2)) {
            ESP_LOGW(TAG, "⚠️ [FRAGMENTATION] Heap fragmented: %u bytes free but largest block only %u bytes",
                     freeHeap, largestFreeBlock);
        }
        
        // Critical low heap warning
        if (freeHeap < 30000) {
            ESP_LOGE(TAG, "🚨 [CRITICAL] Low heap in main loop! Only %u bytes free!", freeHeap);
        }
        
        lastHeapCheck = currentTime;
    }

    // Update WiFi service (handles auto-reconnect)
    if (wifiService) {
        wifiService->update();
    }

    // Backup periodic upload (every 5 minutes)
    // Primary upload happens immediately via onRoutingTableChanged() callback
    // This periodic upload serves as:
    // - Backup mechanism in case callback fails
    // - Ensures Firebase data stays fresh even if no changes occur
    // - Re-syncs routing table after Firebase reconnection
    if (gatewayState.firebaseConnected &&
        (currentTime - gatewayState.lastRoutingTableUpload >= GATEWAY_ROUTING_TABLE_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic backup routing table upload");
        uploadRoutingTable();
        // Always update timestamp even if upload fails to prevent rapid retries
        gatewayState.lastRoutingTableUpload = currentTime;
    }

    // Simple status LED indication
    if (statusCounter++ % 100 == 0) {
        if (gatewayState.wifiConnected && gatewayState.firebaseConnected) {
            led_pattern_message(); // Quick flash for active
        } else if (gatewayState.wifiConnected) {
            led_flash(1, 500);     // Single slow flash for WiFi only
        } else {
            led_pattern_error();   // Error pattern for disconnected
        }
    }

    delay(100); // Main loop delay
}

void GatewayApp::setupLoRaMesher() {
    ESP_LOGI(TAG, "Setting up LoRaMesher...");

    LoraMesher::LoraMesherConfig config;

    #if DEVICE_MODE == 1  // Esp32c3 Node mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #elif DEVICE_MODE == 2 // Esp32 Gateway mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #elif DEVICE_MODE == 3 // Esp32 Node mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #endif 

    radio.begin(config);
    
    // Set Gateway role so nodes can discover it via getClosestGateway()
    radio.addGatewayRole();
    ESP_LOGI(TAG, "Gateway configured as Gateway role");

    TaskHandle_t receiveHandle = createGatewayReceiveTask();
    if (receiveHandle) {
        ESP_LOGI(TAG, "Setting task handle %p for gateway data", receiveHandle);
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(TAG, "LoRaMesher initialized for Gateway");
    } else {
        ESP_LOGE(TAG, "Failed to create gateway receive task");
        led_pattern_error();
    }
}

void GatewayApp::setupWiFi() {
    ESP_LOGI(TAG, "Setting up WiFi connection...");

    // Create WiFi service instance
    wifiService = new WiFiConnectionService(
        WIFI_SSID,
        WIFI_PASSWORD,
        true,   // auto-reconnect enabled
        1000    // initial reconnect interval: 1 second
    );

    // Register WiFi event callback
    wifiService->onEvent([this](WiFiConnectionService::WiFiEvent event, int8_t rssi) {
        this->handleWiFiEvent(event, rssi);
    });

    // Set RSSI threshold for low signal warning
    wifiService->setRSSIThreshold(-80);  // Warn if RSSI < -80 dBm

    // Initialize WiFi service
    if (!wifiService->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize WiFi service");
        led_pattern_error();
        return;
    }

    // Connect to WiFi (with 15-second timeout)
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    if (wifiService->connect(15000)) {
        gatewayState.wifiConnected = true;
        ESP_LOGI(TAG, "WiFi connected! IP: %s, MAC: %s, RSSI: %d dBm",
                 wifiService->getLocalIP().c_str(),
                 wifiService->getMACAddress().c_str(),
                 wifiService->getRSSI());
    } else {
        ESP_LOGW(TAG, "WiFi connection failed, but auto-reconnect is enabled");
        gatewayState.wifiConnected = false;
    }
}

void GatewayApp::setupFirebase() {
    ESP_LOGI(TAG, "Setting up Firebase connection...");

    // Create Gateway ID from MAC address
    String macAddr = WiFi.macAddress();
    macAddr.replace(":", "");
    String gatewayId = String(FIREBASE_GATEWAY_ID_PREFIX);
    gatewayId += macAddr;

    // Create Firebase client instance
    firebaseClient = new FirebaseClient(
        FIREBASE_HOST,
        FIREBASE_AUTH,
        gatewayId.c_str()
    );

    // Configure retry behavior
    firebaseClient->setRetryConfig(3, 1000);  // 3 retries, 1 second delay

    // Initialize Firebase client
    if (!firebaseClient->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize Firebase client");
        led_pattern_error();
        return;
    }

    // Connect to Firebase
    if (firebaseClient->connect()) {
        gatewayState.firebaseConnected = true;
        ESP_LOGI(TAG, "🔥 Firebase connected! Gateway ID: %s", gatewayId.c_str());

        // Upload initial gateway info
        auto result = firebaseClient->updateGatewayInfo(
            WiFi.macAddress(),
            wifiService->getLocalIP(),
            "1.0.0"  // Firmware version
        );

        if (result.success) {
            ESP_LOGI(TAG, "Gateway info uploaded to Firebase");
        }

        // Log gateway started event
        firebaseClient->logEvent("gateway_started", "", "");
    } else {
        ESP_LOGW(TAG, "Firebase connection failed: %s", firebaseClient->getLastError().c_str());
        gatewayState.firebaseConnected = false;
    }
}

void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        ESP_LOGW(TAG, "Firebase not available for upload");
        return;
    }

    // Check if we're in provisioning mode (Fast Discovery)
    // Don't process/upload sensor data during provisioning - only Hello packets
    uint8_t currentMode = radio.getCurrentHelloMode();
    if (currentMode == HELLO_MODE_FAST_DISCOVERY) {
        ESP_LOGD(TAG, "Dropping sensor data packet - in Fast Discovery Mode (provisioning)");
        ESP_LOGD(TAG, "Only Hello packets are processed during provisioning for routing table building");
        return;
    }

    gatewayState.totalMeshPackets++;

    // Extract data from packet - payload contains the actual sensorData
    if (packet->payloadSize >= sizeof(sensorData)) {
        sensorData* s = reinterpret_cast<sensorData*>(packet->payload);
        uint16_t sourceNode = packet->src;

        // Ensure nodeId is set correctly
        if (s->nodeId == 0) {
            s->nodeId = sourceNode;
        }

        ESP_LOGI(TAG, "☁️ Uploading sensor data from node 0x%04X to Firebase", sourceNode);
        ESP_LOGI(TAG, "🔢 Counter: %u, 🌡️ Temp: %.1f°C, 💧 Hum: %.1f%%, 🔋 Batt: %.2fV, 📡 NodeID: 0x%04X",
             s->counter, s->temperature, s->humidity, s->battery, s->nodeId);

        // Upload to Firebase (RSSI/SNR not available in AppPacket - stored in routing table)
        auto result = firebaseClient->uploadSensorData(*s, 0, 0);

        if (result.success) {
            gatewayState.packetsUploaded++;
            led_pattern_message(); // Flash LED on successful upload
            ESP_LOGI(TAG, "✅ Upload successful (%d bytes)", result.payloadSize);
        } else {
            gatewayState.uploadErrors++;
            ESP_LOGW(TAG, "❌ Firebase upload failed: %s", result.errorMessage.c_str());
        }
    } else {
        ESP_LOGW(TAG, "Packet too small to contain sensorData structure");
    }
}

void GatewayApp::uploadRoutingTable() {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        return;
    }

    // Access routing table from RoutingTableService
    LM_LinkedList<RouteNode>* rtList = RoutingTableService::routingTableList;
    if (!rtList) {
        ESP_LOGW(TAG, "Routing table is null");
        return;
    }

    rtList->setInUse();
    size_t tableSize = rtList->getLength();

    if (tableSize == 0) {
        ESP_LOGD(TAG, "Routing table is empty, skipping upload");
        rtList->releaseInUse();
        return;
    }

    // Convert LinkedList to vector for Firebase upload
    std::vector<RouteNode> routingTable;
    routingTable.reserve(tableSize);

    if (rtList->moveToStart()) {
        do {
            RouteNode* node = rtList->getCurrent();
            if (node) {
                routingTable.push_back(*node);
            }
        } while (rtList->next());
    }

    rtList->releaseInUse();

    ESP_LOGI(TAG, "📡 Uploading routing table (%d nodes)", routingTable.size());

    auto result = firebaseClient->uploadRoutingTable(routingTable);

    if (result.success) {
        gatewayState.lastRoutingTableUpload = millis();
        ESP_LOGI(TAG, "✅ Routing table uploaded (%d bytes)", result.payloadSize);
    } else {
        ESP_LOGW(TAG, "❌ Failed to upload routing table: %s", result.errorMessage.c_str());
    }
}

void GatewayApp::handleWiFiEvent(WiFiConnectionService::WiFiEvent event, int8_t rssi) {
    switch (event) {
        case WiFiConnectionService::WiFiEvent::CONNECTED:
            ESP_LOGI(TAG, "✅ WiFi CONNECTED! IP: %s, RSSI: %d dBm",
                     wifiService->getLocalIP().c_str(), rssi);
            gatewayState.wifiConnected = true;
            led_pattern_connected();

            // Try to reconnect Firebase if it was disconnected
            if (firebaseClient && !gatewayState.firebaseConnected) {
                if (firebaseClient->connect()) {
                    gatewayState.firebaseConnected = true;
                    firebaseClient->logEvent("firebase_reconnected", "", "");
                }
            }
            break;

        case WiFiConnectionService::WiFiEvent::DISCONNECTED:
            ESP_LOGW(TAG, "❌ WiFi DISCONNECTED!");
            gatewayState.wifiConnected = false;
            gatewayState.firebaseConnected = false;
            led_pattern_error();

            if (firebaseClient) {
                firebaseClient->logEvent("wifi_disconnected", "", "");
            }
            break;

        case WiFiConnectionService::WiFiEvent::RECONNECTING:
            ESP_LOGI(TAG, "⏳ WiFi RECONNECTING...");
            led_flash(2, 250);  // Double flash pattern for reconnecting
            break;

        case WiFiConnectionService::WiFiEvent::CONNECTION_FAILED:
            ESP_LOGE(TAG, "❌ WiFi CONNECTION FAILED!");
            gatewayState.wifiConnected = false;
            led_pattern_error();
            break;

        case WiFiConnectionService::WiFiEvent::RSSI_LOW:
            ESP_LOGW(TAG, "⚠️ WiFi signal LOW! RSSI: %d dBm", rssi);
            break;
    }
}

// Static callback for processing gateway packets
void GatewayApp::processGatewayPackets(void* parameter) {
    ESP_LOGI(TAG, "[GATEWAY-TASK] Gateway packet processing task started");
    
    // Stack monitoring - check initial stack
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[GATEWAY-TASK] Initial stack high water mark: %d bytes free", stackHighWaterMark);
    
    // Memory leak detection - track heap usage
    uint32_t initialFreeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = initialFreeHeap;
    uint32_t packetCount = 0;
    ESP_LOGI(TAG, "[GATEWAY-TASK] Initial free heap: %u bytes", initialFreeHeap);

    for (;;) {
        // Wait for notification from mesh receiver
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        ESP_LOGI(TAG, "[GATEWAY-TASK] Processing gateway packets...");
        led_pattern_message();

        // Memory leak detection - check heap before processing
        uint32_t freeHeapBefore = ESP.getFreeHeap();

        // Stack monitoring - check before processing packets
        stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (stackHighWaterMark < 1024) {
            ESP_LOGW(TAG, "⚠️ [GATEWAY-TASK] Low stack warning! Only %d bytes free", stackHighWaterMark);
        }

        while (GatewayApp::instance->radio.getReceivedQueueSize() > 0) {
            ESP_LOGD(TAG, "[GATEWAY-TASK] Processing received mesh packet");
            ESP_LOGD(TAG, "[GATEWAY-TASK] Queue size: %d", GatewayApp::instance->radio.getReceivedQueueSize());

            AppPacket<uint8_t>* packet = GatewayApp::instance->radio.getNextAppPacket<uint8_t>();
            
            if (!packet) {
                ESP_LOGW(TAG, "⚠️ [GATEWAY-TASK] Null packet received!");
                continue;
            }

            // Cast to the correct structure - AppPacket with sensorData payload
            AppPacket<sensorData>* sensorPacket = reinterpret_cast<AppPacket<sensorData>*>(packet);

            // Upload to Firebase (includes RSSI and SNR from packet)
            GatewayApp::instance->uploadToFirebase(sensorPacket);

            // CRITICAL: Delete packet to free memory
            GatewayApp::instance->radio.deletePacket(packet);
            packetCount++;
            
            // Stack monitoring - check after Firebase upload (most stack-intensive operation)
            stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "[GATEWAY-TASK] Stack high water mark after upload: %d bytes free", stackHighWaterMark);
            if (stackHighWaterMark < 1024) {
                ESP_LOGW(TAG, "⚠️ [GATEWAY-TASK] Low stack after upload! Only %d bytes free", stackHighWaterMark);
            }
        }
        
        // Memory leak detection - check heap after processing
        uint32_t freeHeapAfter = ESP.getFreeHeap();
        int32_t heapDelta = (int32_t)freeHeapAfter - (int32_t)freeHeapBefore;
        
        // Track minimum free heap
        if (freeHeapAfter < minFreeHeap) {
            minFreeHeap = freeHeapAfter;
        }
        
        // Log memory status periodically (every 10 packets) or if leak detected
        if (packetCount % 10 == 0 || heapDelta < -1000) {
            ESP_LOGI(TAG, "[MEMORY] Packets: %u, Free heap: %u bytes (min: %u), Delta: %d bytes",
                     packetCount, freeHeapAfter, minFreeHeap, heapDelta);
            
            // Warn if heap is decreasing significantly
            if ((int32_t)(initialFreeHeap - freeHeapAfter) > 10000) {
                ESP_LOGW(TAG, "⚠️ [MEMORY LEAK?] Heap decreased by %d bytes since start!",
                         (int32_t)(initialFreeHeap - freeHeapAfter));
            }
        }
        
        // Critical heap warning
        if (freeHeapAfter < 30000) {
            ESP_LOGE(TAG, "🚨 [CRITICAL] Low heap memory! Only %u bytes free!", freeHeapAfter);
        }
    }
}

TaskHandle_t GatewayApp::createGatewayReceiveTask() {
    TaskHandle_t taskHandle = NULL;

    ESP_LOGI(TAG, "Creating gateway receive task...");

    // CRITICAL FIX: Increased stack size from 4096 to 8192 bytes
    // Reason: Stack overflow when uploading to Firebase via WiFi TCP
    // - WiFi TCP connection stack usage: ~2KB
    // - Firebase client operations: ~2KB
    // - Nested function calls: ~1KB
    // - Safety margin: ~3KB
    int res = xTaskCreate(
        processGatewayPackets,
        "Gateway Receive Task",
        8192,  // Increased from 4096 to prevent stack overflow
        (void*) 1,
        2,
        &taskHandle);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Error: Gateway task creation failed: %d", res);
        led_pattern_error();
        return NULL;
    }

    ESP_LOGI(TAG, "Gateway task created successfully, handle: %p, stack: 8192 bytes", taskHandle);
    return taskHandle;
}

// Static callback for routing table changes
void GatewayApp::onRoutingTableChanged() {
    if (!instance) {
        return;
    }

    // Only upload if Firebase is connected
    if (!instance->gatewayState.firebaseConnected) {
        ESP_LOGD(TAG, "Routing table changed but Firebase not connected, skipping upload");
        return;
    }

    ESP_LOGI(TAG, "🔄 Routing table changed - triggering immediate Firebase upload");
    instance->uploadRoutingTable();
}

// REMOVED: Old UART callback functions - no longer used in WiFi+Firebase architecture
/*
// Static callback for receiving netkey from UART
void GatewayApp::onNetkeyReceived(const UartNetworkKey& netkey) {
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

    // Update Gateway's local network key (for gateway operation)
    bool localUpdateSuccess = false;
    if (NetkeyDistributionService::updateLocalNetworkKey(netkey.networkKey, netkey.authToken, netkey.networkId, netkey.keyVersion)) {
        ESP_LOGI(TAG, "Gateway local network key updated successfully");
        localUpdateSuccess = true;
    } else {
        ESP_LOGE(TAG, "Failed to update Gateway local network key");
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
    if (GatewayApp::instance && GatewayApp::instance->uartProtocol) {
        GatewayApp::instance->uartProtocol->sendNetkeyUpdateConfirm(overallSuccess);
    }

    ESP_LOGI(TAG, "Netkey processing completed - NVS: %s, Local: %s, Distribution: %s", 
             nvsSaveSuccess ? "SUCCESS" : "FAILED", 
             localUpdateSuccess ? "SUCCESS" : "FAILED",
             distributionSuccess ? "SUCCESS" : "FAILED");
}

// Static callback for provisioning control from UART
void GatewayApp::onProvisioningControl(const UartProvisioningControl& control) {
    ESP_LOGI(TAG, "*** SIMPLIFIED PROVISIONING CONTROL RECEIVED FROM UART ***");
    ESP_LOGI(TAG, "Action: %d (0=stop, 1=start, 2=get_status)", control.action);
    
    // SIMPLIFIED: No complex provisioning mode
    // Just acknowledge the command since netkey distribution is automatic
    
    switch (control.action) {
        case 0: { // Stop provisioning
            ESP_LOGI(TAG, "*** STOP PROVISIONING - Returning to normal operation mode ***");
            
            // Stop fast discovery mode and return to normal hello mode
            if (GatewayApp::instance) {
                // STEP 1: Stop fast discovery mode on Gateway itself
                GatewayApp::instance->radio.stopFastDiscoveryMode();
                ESP_LOGI(TAG, "Fast discovery mode stopped - Gateway returning to normal operation (120s)");
                
                // STEP 2: Broadcast command to all nodes to return to normal mode
                GatewayApp::instance->radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
                ESP_LOGI(TAG, "Broadcasted normal mode command to all nodes in network");
            }
            
            ESP_LOGI(TAG, "*** Gateway and all nodes transitioning to normal operation mode (120s intervals) ***");
            break;
        }

        case 1: { // Start provisioning  
            ESP_LOGI(TAG, "Provisioning control - Start: Triggering fast discovery mode");
            ESP_LOGI(TAG, "Duration: %dms, MaxSessions: %d", control.durationMs, control.maxSessions);
            
            // Phase 1: Start fast discovery mode for specified duration
            if (GatewayApp::instance) {
                uint32_t duration = (control.durationMs > 0) ? control.durationMs : (HELLO_DISCOVERY_DURATION * 1000);
                
                // STEP 1: Activate fast discovery mode on Gateway itself
                GatewayApp::instance->radio.startFastDiscoveryMode(duration);
                ESP_LOGI(TAG, "Fast discovery mode activated on Gateway for %dms", duration);
                
                // STEP 2: Broadcast command to all nodes to enter fast discovery mode
                GatewayApp::instance->radio.broadcastHelloModeChange(HELLO_MODE_FAST_DISCOVERY, duration);
                ESP_LOGI(TAG, "Broadcasted fast discovery mode command to all nodes in network");
            }
            
            ESP_LOGI(TAG, "*** NOTE: Fast hello mode (30s) enables quick routing table building for new nodes ***");
            break;
        }

        case 2: { // Get status
            ESP_LOGI(TAG, "Provisioning status requested");
            
            // Send simplified status
            if (GatewayApp::instance && GatewayApp::instance->uartProtocol) {
                UartProvisioningStatus status;
                memset(&status, 0, sizeof(status));
                status.active = false; // Always inactive in simplified mode
                status.remainingTimeMs = 0;
                status.activeSessions = 0;
                status.maxSessions = 0;
                status.totalRequests = 0;
                status.successfulProvisions = 0; 
                status.rejectedRequests = 0;
                
                GatewayApp::instance->uartProtocol->sendProvisioningStatus(status);
                ESP_LOGI(TAG, "Sent simplified provisioning status (always inactive)");
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown provisioning control action: %d", control.action);
            break;
    }
}

void GatewayApp::getProvisioningStatus(UartProvisioningStatus& status) const {
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
