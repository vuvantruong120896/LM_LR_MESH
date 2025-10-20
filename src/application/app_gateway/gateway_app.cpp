#include "gateway_app.h"
#include "components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "mesh_security_config.h"
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "GATEWAY";

// Compute a 16-bit Node ID using the last 2 bytes of the WiFi MAC (STA MAC)
static uint16_t computeNodeIdFromWifiMac() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // Use the last 2 bytes of the MAC as a 16-bit ID (big-endian)
    uint16_t id = ((uint16_t)mac[4] << 8) | (uint16_t)mac[5];
    return id;
}

// Static member initialization
GatewayApp* GatewayApp::instance = nullptr;

GatewayApp::GatewayApp()
    : radio(LoraMesher::getInstance()),
      wifiService(nullptr),
      firebaseClient(nullptr),
      statusCounter(0),
      sensorCounter(0),
      lastStatusUploadTime(0),
      statusPacket(new gatewayStatus),
      provisionManager(nullptr),
      commandPoller(nullptr) {
    instance = this;
    gatewayState.bootTime = millis();
}

GatewayApp::~GatewayApp() {
    delete statusPacket;
    delete firebaseClient;
    delete wifiService;
    delete provisionManager;
    delete commandPoller;
}

void GatewayApp::setup() {
    ESP_LOGI(TAG, "=== LoRaMesh Gateway Application ===");
    
    // Get actual NodeID from MAC address
    uint16_t gatewayNodeId = computeNodeIdFromWifiMac();
    ESP_LOGI(TAG, "Gateway Node ID (from WiFi MAC last 2 bytes): 0x%04X", gatewayNodeId);

    led_init();
    led_pattern_startup();

    // ===== BLE PROVISIONING CHECK =====
    // Create provision manager and check if device is provisioned
    provisionManager = new ProvisionManager();
    
    if (!provisionManager->isProvisioned()) {
        ESP_LOGW(TAG, "⚠️ Device not provisioned! Gateway will operate in offline mode");
        ESP_LOGI(TAG, "- Mesh network: ACTIVE");
        ESP_LOGI(TAG, "- Sensor monitoring: ACTIVE");
        ESP_LOGI(TAG, "- Data buffering: ACTIVE (to Flash)");
        ESP_LOGI(TAG, "- Firebase upload: DISABLED (no user context)");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "To enable Firebase upload, provision via mobile app");
        led_pattern_error(); // Indicate provisioning required
        
        // Start BLE provisioning (non-blocking)
        provisionManager->startProvisioningIfNeeded();
        
        // Continue with setup in offline mode (no WiFi, no Firebase)
        // Skip to mesh initialization
        ESP_LOGI(TAG, "Continuing setup in OFFLINE mode...");
    } else {
        ESP_LOGI(TAG, "✅ Device is provisioned, continuing setup in ONLINE mode...");
    }
    // ===== END BLE PROVISIONING CHECK =====

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
    
    // Only setup mesh, WiFi and Firebase if provisioned
    if (provisionManager && provisionManager->isProvisioned()) {
        setupLoRaMesher();
        setupWiFi();
        setupFirebase();
        setupTimeSync();  // Setup NTP time synchronization
        
        // Register callback to upload routing table when it changes
        RoutingTableService::setRoutingTableChangedCallback(onRoutingTableChanged);
        ESP_LOGI(TAG, "Registered routing table change callback for real-time Firebase upload");
        
        // Initialize offline data buffer (only when provisioned)
        if (OfflineDataBuffer::initialize()) {
            uint16_t count, maxSize;
            uint8_t percentFull;
            OfflineDataBuffer::getStats(count, maxSize, percentFull);
            ESP_LOGI(TAG, "📦 Offline buffer ready: %u/%u samples (%u%% full)", count, maxSize, percentFull);
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to initialize offline buffer");
        }
        
        // Routing table will be built from scratch via HELLO packets
        ESP_LOGI(TAG, "Routing table will be built from scratch via HELLO protocol");
        
        ESP_LOGI(TAG, "✅ Gateway setup complete (provisioned mode)");
        led_pattern_connected();
    } else {
        ESP_LOGI(TAG, "⏭️ Skipping mesh/WiFi/Firebase setup (not provisioned - waiting for BLE provisioning)");
        ESP_LOGI(TAG, "🔵 Gateway in provisioning mode - use mobile app to configure");
        led_pattern_provisioning();  // Indicate provisioning mode
    }
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
    // Check if provisioning just succeeded (show LED success pattern)
    static bool successShown = false;
    if (provisionManager && provisionManager->provisionSucceeded() && !successShown) {
        ESP_LOGI(TAG, "🎉 Showing provision success LED pattern...");
        led_pattern_provision_success();  // 3 seconds of fast flashing
        provisionManager->clearProvisionSuccessFlag();
        successShown = true;
    }
    
    // Check if provisioning completed and needs restart
    if (provisionManager && provisionManager->needsRestart()) {
        ESP_LOGI(TAG, "🔄 Provisioning complete! Restarting NOW...");
        delay(500);  // Short delay for stability
        esp_restart();
    }
    
    // Check provision status once
    static bool provisionStatusChecked = false;
    static bool isProvisioned = false;
    
    if (!provisionStatusChecked && provisionManager) {
        isProvisioned = provisionManager->isProvisioned();
        provisionStatusChecked = true;
        ESP_LOGI(TAG, "Provision status: %s", isProvisioned ? "PROVISIONED" : "NOT PROVISIONED");
    }
    
    uint32_t currentTime = millis();
    
    // If not provisioned, only handle BLE provisioning events
    if (!isProvisioned) {
        // Wait for provisioning to complete
        delay(100); // Small delay to allow BLE events to process
        return;
    }
    
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

    // Update WiFi service (handles auto-reconnect) - only if provisioned
    if (wifiService) {
        wifiService->update();
    }

    // Sync offline buffer to Firebase when online (only if provisioned)
    if (isProvisioned && gatewayState.wifiConnected && firebaseClient) {
        static uint32_t lastBufferSync = 0;
        const uint32_t BUFFER_SYNC_INTERVAL = 5000; // Sync every 5 seconds when online
        
        if (currentTime - lastBufferSync >= BUFFER_SYNC_INTERVAL) {
            uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
            
            if (bufferedCount > 0) {
                ESP_LOGI(TAG, "📤 Syncing offline buffer: %u samples pending", bufferedCount);
                
                // Upload up to 10 samples per cycle to avoid blocking
                const uint16_t MAX_UPLOADS_PER_CYCLE = 10;
                uint16_t uploaded = 0;
                
                for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
                    String nodeId;
                    sensorData data;
                    
                    if (OfflineDataBuffer::getOldestData(nodeId, data)) {
                        // Upload to Firebase with RSSI/SNR = 0 (stale data)
                        auto result = firebaseClient->uploadSensorData(data, 0, 0.0f);
                        
                        if (result.success) {
                            // Remove from buffer after successful upload
                            OfflineDataBuffer::removeOldest();
                            uploaded++;
                            bufferedCount--;
                            ESP_LOGD(TAG, "✅ Synced buffered data from %s", nodeId.c_str());
                        } else {
                            // Failed to upload, keep in buffer and retry later
                            ESP_LOGW(TAG, "❌ Failed to sync buffered data: %s", result.errorMessage.c_str());
                            break; // Stop trying for this cycle
                        }
                    }
                }
                
                if (uploaded > 0) {
                    ESP_LOGI(TAG, "📤 Synced %u buffered samples (%u remaining)", uploaded, bufferedCount);
                }
            }
            
            lastBufferSync = currentTime;
        }
    }

    // Periodic NTP re-sync (every 1 hour) and time broadcast (every 5 minutes) - only if provisioned
    static uint32_t lastNTPSync = 0;
    const uint32_t NTP_RESYNC_INTERVAL = 3600000;  // 1 hour
    const uint32_t TIME_BROADCAST_INTERVAL = 300000;  // 5 minutes
    
    // Re-sync with NTP every hour (if WiFi connected and provisioned)
    if (isProvisioned && gatewayState.wifiConnected && (currentTime - lastNTPSync >= NTP_RESYNC_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic NTP re-sync");
        if (TimeSyncService::syncWithNTP("pool.ntp.org", 25200, 0)) {
            gatewayState.ntpSynced = true;
            ESP_LOGI(TAG, "✅ NTP re-sync successful");
        }
        lastNTPSync = currentTime;
    }
    
    // Broadcast time sync to nodes every 5 minutes
    if (gatewayState.ntpSynced && 
        (currentTime - gatewayState.lastTimeSyncBroadcast >= TIME_BROADCAST_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic time sync broadcast to nodes");
        broadcastTimeSync();
    }

    // Routing table upload strategy:
    // 1. Primary: Immediate upload via onRoutingTableChanged() callback when changes occur
    // 2. Initial: Upload once immediately after Firebase connection (post-reboot)
    // 3. Backup: Periodic upload every 5 minutes as fallback
    static bool firstRoutingTableUploadDone = false;
    
    // Upload immediately on first Firebase connection after boot
    if (gatewayState.firebaseConnected && !firstRoutingTableUploadDone) {
        ESP_LOGI(TAG, "📡 Initial routing table upload (post-reboot)");
        uploadRoutingTable();
        gatewayState.lastRoutingTableUpload = currentTime;
        firstRoutingTableUploadDone = true;
    }
    // Then continue with periodic backup uploads
    else if (gatewayState.firebaseConnected &&
        (currentTime - gatewayState.lastRoutingTableUpload >= GATEWAY_ROUTING_TABLE_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic backup routing table upload");
        uploadRoutingTable();
        // Always update timestamp even if upload fails to prevent rapid retries
        gatewayState.lastRoutingTableUpload = currentTime;
    }

    // Gateway sensor data collection and upload
    static uint32_t lastSensorUpload = 0;
    static bool firstUploadDone = false;
    
    // Upload immediately on first connection after boot
    if (gatewayState.firebaseConnected && !firstUploadDone) {
        ESP_LOGI(TAG, "📊 Initial Gateway sensor data upload (post-reboot)");
        uploadGatewaySensorData();
        lastSensorUpload = currentTime;
        firstUploadDone = true;
    }
    // Then continue with periodic uploads
    else if (gatewayState.firebaseConnected &&
        (currentTime - lastSensorUpload >= GATEWAY_SENSOR_INTERVAL)) {
        ESP_LOGI(TAG, "📊 Periodic Gateway sensor data collection");
        uploadGatewaySensorData();
        lastSensorUpload = currentTime;
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

    // NEW: Poll for Firebase commands (if provisioned and Firebase connected)
    if (isProvisioned && gatewayState.firebaseConnected && commandPoller) {
        commandPoller->poll();
        
        if (commandPoller->hasCommand()) {
            auto cmd = commandPoller->getNextCommand();
            
            ESP_LOGI(TAG, "📥 Processing command from Firebase:");
            ESP_LOGI(TAG, "  ID: %s", cmd.id.c_str());
            ESP_LOGI(TAG, "  Type: %s", cmd.type.c_str());
            ESP_LOGI(TAG, "  Priority: %d", cmd.priority);
            
            // Move to processing immediately
            commandPoller->moveToProcessing(cmd);
            
            // Handle command based on type
            if (cmd.type == "start_provisioning") {
                handleStartProvisioning(cmd);
            } else if (cmd.type == "stop_provisioning") {
                handleStopProvisioning(cmd);
            } else if (cmd.type == "assign_netkey") {
                handleAssignNetkey(cmd);
            } else {
                ESP_LOGW(TAG, "Unknown command type: %s", cmd.type.c_str());
                commandPoller->moveToFailed(cmd, "UNKNOWN_COMMAND", 
                                           "Unknown command type: " + cmd.type);
            }
        }
    }
    
    // NEW: Update provisioning progress if active
    if (gatewayState.provisioningActive) {
        updateProvisioningProgress();
        
        // Check if provisioning should end
        if (currentTime >= gatewayState.provisioningEndTime) {
            ESP_LOGI(TAG, "⏰ Provisioning timeout reached - stopping");
            
            // Stop provisioning
            radio.stopFastDiscoveryMode();
            radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
            
            // Mark command as completed
            if (commandPoller && !gatewayState.provisioningCommandId.isEmpty()) {
                FirebaseCommandPoller::Command cmd;
                cmd.id = gatewayState.provisioningCommandId;
                cmd.type = "start_provisioning";
                
                String message = String("Provisioning completed. ") + 
                                gatewayState.nodesDiscoveredDuringProvisioning + 
                                " nodes discovered";
                
                commandPoller->moveToCompleted(cmd, "success", message);
            }
            
            gatewayState.provisioningActive = false;
            gatewayState.provisioningCommandId = "";
        }
    }

    // Upload gateway status periodically (every 60 seconds)
    uploadGatewayStatusPeriodic();

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

    // Load WiFi credentials from provisioned data
    String ssid, password;
    if (!provisionManager->getWiFiCredentials(ssid, password)) {
        ESP_LOGE(TAG, "Failed to load WiFi credentials from NVS!");
        led_pattern_error();
        return;
    }
    
    ESP_LOGI(TAG, "Loaded WiFi SSID from provisioning: %s", ssid.c_str());

    // Create WiFi service instance with provisioned credentials
    wifiService = new WiFiConnectionService(
        ssid.c_str(),
        password.c_str(),
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
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
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

    // Load user UID from provisioning
    String userUID;
    if (!provisionManager->getUserUID(userUID)) {
        ESP_LOGE(TAG, "Failed to load user UID from NVS!");
        led_pattern_error();
        return;
    }
    
    ESP_LOGI(TAG, "User UID from provisioning: %s", userUID.c_str());

    // Create Gateway MAC address string
    String gatewayMAC = WiFi.macAddress();
    ESP_LOGI(TAG, "Gateway MAC: %s", gatewayMAC.c_str());

    // Create Firebase client instance
    firebaseClient = new FirebaseClient(
        FIREBASE_HOST,
        FIREBASE_AUTH,
        gatewayMAC.c_str()  // Use MAC as gateway ID
    );

    // Set user context for multi-user Firebase paths
    firebaseClient->setUserContext(userUID, gatewayMAC);

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
        ESP_LOGI(TAG, "🔥 Firebase connected! User: %s, Gateway: %s", 
                 userUID.c_str(), gatewayMAC.c_str());

        // Upload initial gateway info
        auto result = firebaseClient->updateGatewayInfo(
            gatewayMAC,
            wifiService->getLocalIP(),
            "1.0.0"  // Firmware version
        );

        if (result.success) {
            ESP_LOGI(TAG, "Gateway info uploaded to Firebase");
        }

        // Log gateway started event
        firebaseClient->logEvent("gateway_started", "", "");
        
        // NEW: Create and initialize command poller
        ESP_LOGI(TAG, "Initializing Firebase Command Poller...");
        commandPoller = new FirebaseCommandPoller(
            firebaseClient->getFirebaseData(),  // Share FirebaseData with client
            userUID,
            gatewayMAC
        );
        commandPoller->begin();
        ESP_LOGI(TAG, "✅ Command poller ready - Gateway can receive commands from Mobile App");
        
    } else {
        ESP_LOGW(TAG, "Firebase connection failed: %s", firebaseClient->getLastError().c_str());
        gatewayState.firebaseConnected = false;
    }
}

void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
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

        // Get RSSI and SNR from routing table for this node
        int8_t rssi = 0;
        float snr = 0.0f;
        
        RouteNode* routeNode = RoutingTableService::findNode(sourceNode);
        if (routeNode) {
            rssi = routeNode->receivedRSSI;
            snr = routeNode->receivedSNR;
        }

        char nodeIdStr[16];
        snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", sourceNode);

        // Try to upload to Firebase if online and provisioned
        if (firebaseClient && gatewayState.firebaseConnected) {
            ESP_LOGI(TAG, "☁️ Uploading sensor data from node %s to Firebase", nodeIdStr);
            
            // Log sensor-specific data based on device type
            switch (s->deviceType) {
                case DeviceType::SOIL_SENSOR:
                    ESP_LOGI(TAG, "🌱 Soil Sensor - Counter: %u, Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, Batt: %.2fV",
                             s->counter, s->data.soil.soilMoisture, s->data.soil.soilTemperature, 
                             s->data.soil.pH, s->battery);
                    break;
                case DeviceType::ENV_SENSOR:
                    ESP_LOGI(TAG, "🌡️ Env Sensor - Counter: %u, Temp: %.1f°C, Hum: %.1f%%, Batt: %.2fV",
                             s->counter, s->data.environment.temperature, s->data.environment.humidity, s->battery);
                    break;
                default:
                    ESP_LOGI(TAG, "📊 Sensor - Counter: %u, Type: %s, Batt: %.2fV",
                             s->counter, deviceTypeToString(s->deviceType), s->battery);
                    break;
            }

            auto result = firebaseClient->uploadSensorData(*s, rssi, snr);

            if (result.success) {
                gatewayState.packetsUploaded++;
                led_pattern_message(); // Flash LED on successful upload
                ESP_LOGI(TAG, "✅ Upload successful (%d bytes)", result.payloadSize);
            } else {
                gatewayState.uploadErrors++;
                // FIX: Không buffer khi upload fail - chỉ log error và retry sample tiếp theo
                // Upload failure có thể do lỗi tạm thời (network glitch, Firebase overload)
                // Sample tiếp theo sẽ thử lại - tránh lãng phí NVS
                ESP_LOGW(TAG, "❌ Firebase upload failed (will retry on next sample): %s", 
                         result.errorMessage.c_str());
            }
        } else {
            // Not provisioned or offline - buffer data to NVS
            ESP_LOGD(TAG, "📦 Gateway offline - buffering data from node %s", nodeIdStr);
            
            if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
                uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
                ESP_LOGI(TAG, "📦 Data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
            } else {
                ESP_LOGW(TAG, "⚠️ Failed to buffer data - NVS full or error");
            }
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

    // Convert LinkedList to vector for Firebase upload
    // Upload even if empty to clear stale data on Firebase
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

    if (routingTable.size() == 0) {
        ESP_LOGI(TAG, "📡 Uploading EMPTY routing table to clear Firebase data");
    } else {
        ESP_LOGI(TAG, "📡 Uploading routing table (%d nodes)", routingTable.size());
    }

    auto result = firebaseClient->uploadRoutingTable(routingTable);

    if (result.success) {
        gatewayState.lastRoutingTableUpload = millis();
        if (routingTable.size() == 0) {
            ESP_LOGI(TAG, "✅ Empty routing table uploaded - Firebase cleared");
        } else {
            ESP_LOGI(TAG, "✅ Routing table uploaded (%d bytes)", result.payloadSize);
        }
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
        
        // CRITICAL FIX: Reset task watchdog to prevent timeout during long Firebase uploads
        esp_task_wdt_reset();

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

            // CRITICAL FIX: Reset watchdog before each packet processing
            // Firebase upload can take 8-10 seconds per packet
            esp_task_wdt_reset();

            AppPacket<uint8_t>* packet = GatewayApp::instance->radio.getNextAppPacket<uint8_t>();
            
            if (!packet) {
                ESP_LOGW(TAG, "⚠️ [GATEWAY-TASK] Null packet received!");
                continue;
            }

            // Cast to the correct structure - AppPacket with sensorData payload
            AppPacket<sensorData>* sensorPacket = reinterpret_cast<AppPacket<sensorData>*>(packet);

            // Upload to Firebase (includes RSSI and SNR from packet)
            GatewayApp::instance->uploadToFirebase(sensorPacket);

            // CRITICAL FIX: Reset watchdog after Firebase upload (can take 8-10s)
            esp_task_wdt_reset();

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

void GatewayApp::setupTimeSync() {
    ESP_LOGI(TAG, "Setting up Time Synchronization...");
    
    // Initialize time sync service as Gateway
    if (!TimeSyncService::initialize(true)) {
        ESP_LOGE(TAG, "Failed to initialize Time Sync Service");
        return;
    }
    
    // Wait for WiFi connection before NTP sync
    if (!gatewayState.wifiConnected) {
        ESP_LOGW(TAG, "WiFi not connected yet, will sync NTP later");
        return;
    }
    
    // Sync with NTP server (Vietnam timezone GMT+7)
    // GMT offset: 7 * 3600 = 25200 seconds
    // Daylight saving: 0 (Vietnam doesn't use DST)
    if (TimeSyncService::syncWithNTP("pool.ntp.org", 25200, 0)) {
        gatewayState.ntpSynced = true;
        gatewayState.lastTimeSyncBroadcast = millis();
        ESP_LOGI(TAG, "✅ NTP sync successful - Gateway time synchronized");
        
        // Immediately broadcast time to nodes
        broadcastTimeSync();
    } else {
        ESP_LOGE(TAG, "❌ NTP sync failed - will retry later");
        gatewayState.ntpSynced = false;
    }
}

void GatewayApp::broadcastTimeSync() {
    if (!TimeSyncService::isTimeSynced()) {
        ESP_LOGW(TAG, "Cannot broadcast time - not synchronized yet");
        return;
    }
    
    // Create time sync packet
    TimeSyncService::TimeSyncPacket timeSyncPacket;
    TimeSyncService::createTimeSyncPacket(timeSyncPacket);
    
    // Broadcast to all nodes (destination = 0xFFFF = broadcast address)
    uint16_t broadcastAddr = 0xFFFF;
    
    ESP_LOGI(TAG, "📡 Broadcasting time sync to all nodes: %u.%03u", 
             timeSyncPacket.timestamp, timeSyncPacket.milliseconds);
    
    // Send via LoRa mesh (use createPacketAndSend for broadcast)
    radio.createPacketAndSend(broadcastAddr, &timeSyncPacket, 1);
    
    gatewayState.lastTimeSyncBroadcast = millis();
}

sensorData GatewayApp::simulateGatewaySensorData() {
    sensorData data;
    
    // Set device type - Gateway has soil sensor for demo/testing
    // NOTE: In production, this should be read from NVS/config
    data.deviceType = DeviceType::SOIL_SENSOR;
    
    // === Common fields (all sensor types) ===
    data.battery = 3.7 + (random(0, 60) / 100.0);  // 3.7-4.3V LiPo battery simulation
    data.counter = ++sensorCounter;
    data.nodeId = computeNodeIdFromWifiMac();
    
    // Use NTP synchronized timestamp if available
    if (TimeSyncService::isTimeSynced()) {
        data.timestamp = TimeSyncService::getCurrentTimestamp();
        ESP_LOGI(TAG, "✅ Gateway sensor using synced timestamp: %u (Unix time)", data.timestamp);
    } else {
        data.timestamp = millis() / 1000;  // Fallback to boot time
        ESP_LOGW(TAG, "⚠️ Gateway sensor using fallback timestamp: %u seconds", data.timestamp);
    }
    
    // === Sensor-specific data based on deviceType ===
    switch (data.deviceType) {
        case DeviceType::SOIL_SENSOR:
            // Simulate soil sensor readings (realistic agricultural ranges)
            data.data.soil.soilMoisture = 20.0 + (random(0, 600) / 10.0);      // 20-80% moisture
            data.data.soil.soilTemperature = 18.0 + (random(0, 150) / 10.0);   // 18-33°C soil temp
            data.data.soil.pH = 5.5 + (random(0, 250) / 100.0);                // pH 5.5-8.0
            data.data.soil.ec = 0.3 + (random(0, 300) / 100.0);                // 0.3-3.3 mS/cm (low to high fertility)
            data.data.soil.nitrogen = 50.0 + (random(0, 2000) / 10.0);         // 50-250 mg/kg
            data.data.soil.phosphorus = 20.0 + (random(0, 1000) / 10.0);       // 20-120 mg/kg
            data.data.soil.potassium = 80.0 + (random(0, 1500) / 10.0);        // 80-230 mg/kg
            
            ESP_LOGI(TAG, "🌱 Soil sensor data: Moisture=%.1f%%, Temp=%.1f°C, pH=%.2f, EC=%.2f mS/cm",
                     data.data.soil.soilMoisture, data.data.soil.soilTemperature, 
                     data.data.soil.pH, data.data.soil.ec);
            ESP_LOGI(TAG, "   NPK: N=%.0f, P=%.0f, K=%.0f mg/kg",
                     data.data.soil.nitrogen, data.data.soil.phosphorus, data.data.soil.potassium);
            break;
            
        case DeviceType::ENV_SENSOR:
            // Simulate environment sensor (indoor conditions)
            data.data.environment.temperature = 22.0 + (random(0, 100) / 10.0);    // 22-32°C
            data.data.environment.humidity = 30.0 + (random(0, 400) / 10.0);       // 30-70%
            data.data.environment.pressure = 1000.0 + (random(0, 300) / 10.0);     // 1000-1030 hPa
            data.data.environment.lightIntensity = 100.0 + (random(0, 9000) / 10.0); // 100-1000 lux
            
            ESP_LOGI(TAG, "🌡️ Environment sensor: Temp=%.1f°C, Hum=%.1f%%, Pres=%.1f hPa, Light=%.0f lux",
                     data.data.environment.temperature, data.data.environment.humidity,
                     data.data.environment.pressure, data.data.environment.lightIntensity);
            break;
            
        default:
            ESP_LOGW(TAG, "⚠️ Unknown device type, using default values");
            break;
    }
    
    return data;
}

void GatewayApp::uploadGatewaySensorData() {
    // Generate gateway sensor data (always, regardless of provision status)
    sensorData gatewaySensor = simulateGatewaySensorData();
    
    // Get WiFi RSSI for Gateway sensor data (Gateway signal quality to router)
    int8_t wifiRssi = wifiService ? wifiService->getRSSI() : -90;  // Default fallback if WiFi not available
    float gatewaySnr = 10.0f;  // Fixed SNR value for Gateway data
    
    char nodeIdStr[16];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", gatewaySensor.nodeId);
    
    // Try to upload if online and provisioned
    if (firebaseClient && gatewayState.firebaseConnected) {
        ESP_LOGI(TAG, "🏠 Uploading Gateway sensor data to Firebase");
        
        // Log Gateway sensor-specific data based on device type
        switch (gatewaySensor.deviceType) {
            case DeviceType::SOIL_SENSOR:
                ESP_LOGI(TAG, "🌱 Gateway Soil - Counter: %u, Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, Batt: %.2fV, NodeID: %s",
                         gatewaySensor.counter, gatewaySensor.data.soil.soilMoisture, 
                         gatewaySensor.data.soil.soilTemperature, gatewaySensor.data.soil.pH,
                         gatewaySensor.battery, nodeIdStr);
                break;
            case DeviceType::ENV_SENSOR:
                ESP_LOGI(TAG, "🌡️ Gateway Env - Counter: %u, Temp: %.1f°C, Hum: %.1f%%, Batt: %.2fV, NodeID: %s",
                         gatewaySensor.counter, gatewaySensor.data.environment.temperature, 
                         gatewaySensor.data.environment.humidity, gatewaySensor.battery, nodeIdStr);
                break;
            default:
                ESP_LOGI(TAG, "📊 Gateway - Counter: %u, Type: %s, Batt: %.2fV, NodeID: %s",
                         gatewaySensor.counter, deviceTypeToString(gatewaySensor.deviceType), 
                         gatewaySensor.battery, nodeIdStr);
                break;
        }
        ESP_LOGI(TAG, "📶 WiFi Signal - RSSI: %d dBm, SNR: %.1f dB", wifiRssi, gatewaySnr);
        
        // Upload to Firebase with WiFi RSSI and fixed SNR
        auto result = firebaseClient->uploadSensorData(gatewaySensor, wifiRssi, gatewaySnr);
        
        if (result.success) {
            gatewayState.packetsUploaded++;
            ESP_LOGI(TAG, "✅ Gateway sensor upload successful (%d bytes)", result.payloadSize);
        } else {
            gatewayState.uploadErrors++;
            // FIX: Không buffer khi upload fail - chỉ log error và retry sample tiếp theo
            ESP_LOGE(TAG, "❌ Gateway sensor upload failed (will retry on next sample): %s", 
                     result.errorMessage.c_str());
        }
    } else {
        // Not provisioned or offline - buffer data to NVS
        ESP_LOGD(TAG, "📦 Gateway offline - buffering sensor data");
        
        if (OfflineDataBuffer::addData(String(nodeIdStr), gatewaySensor)) {
            uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
            ESP_LOGI(TAG, "📦 Gateway data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to buffer Gateway data - NVS full or error");
        }
    }
}

void GatewayApp::uploadGatewayStatusPeriodic() {
    // Upload status every 60 seconds
    const uint32_t UPLOAD_INTERVAL_MS = 60000;
    
    uint32_t currentTime = millis();
    if (currentTime - lastStatusUploadTime < UPLOAD_INTERVAL_MS) {
        return; // Not time yet
    }
    
    // Check if provisioned and Firebase connected
    if (!provisionManager || !provisionManager->isProvisioned()) {
        // Update timestamp to avoid spam checking
        lastStatusUploadTime = currentTime;
        return;
    }
    
    if (!firebaseClient || !firebaseClient->isConnected()) {
        // Update timestamp to avoid spam checking when Firebase is disconnected
        lastStatusUploadTime = currentTime;
        return;
    }
    
    // Collect metrics
    uint16_t connectedNodes = 0;
    LM_LinkedList<RouteNode>* rtList = RoutingTableService::routingTableList;
    if (rtList) {
        connectedNodes = rtList->getLength();
    }
    
    uint32_t totalPacketsReceived = gatewayState.totalMeshPackets;
    uint32_t totalPacketsSent = gatewayState.packetsUploaded;
    int8_t wifiRssi = WiFi.RSSI();
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t uptimeSeconds = millis() / 1000;
    
    ESP_LOGI(TAG, "📊 Uploading Gateway status: nodes=%u, rx=%u, tx=%u, rssi=%d, heap=%u, uptime=%u",
             connectedNodes, totalPacketsReceived, totalPacketsSent, wifiRssi, freeHeap, uptimeSeconds);
    
    // Upload to Firebase
    auto result = firebaseClient->uploadGatewayStatus(
        connectedNodes,
        totalPacketsReceived,
        totalPacketsSent,
        wifiRssi,
        freeHeap,
        uptimeSeconds
    );
    
    if (result.success) {
        ESP_LOGI(TAG, "✅ Gateway status uploaded successfully");
        lastStatusUploadTime = currentTime;
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to upload Gateway status: %s", result.errorMessage.c_str());
        
        // Always update timestamp to prevent spam, especially for circuit breaker
        // This ensures we respect the 60-second interval even on failures
        lastStatusUploadTime = currentTime;
        
        // For circuit breaker errors, we should definitely back off
        if (result.errorMessage.find("Circuit breaker") != std::string::npos ||
            result.errorMessage.find("cooldown") != std::string::npos) {
            ESP_LOGD(TAG, "📊 Circuit breaker active - backing off for 60 seconds");
        }
    }
}

// NEW: Command handler implementations

void GatewayApp::handleStartProvisioning(const FirebaseCommandPoller::Command& cmd) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  START PROVISIONING via Firebase Command                  ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    // Parse parameters from JSON
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, cmd.params);
    
    uint32_t durationMs = 600000;  // Default: 10 minutes
    uint16_t maxNodes = 0;         // Default: unlimited
    
    if (!error) {
        if (doc.containsKey("durationMs")) {
            durationMs = doc["durationMs"];
        }
        if (doc.containsKey("maxNodes")) {
            maxNodes = doc["maxNodes"];
        }
    } else {
        ESP_LOGW(TAG, "Failed to parse params, using defaults");
    }
    
    ESP_LOGI(TAG, "Parameters:");
    ESP_LOGI(TAG, "  Duration: %u ms (%.1f minutes)", durationMs, durationMs / 60000.0f);
    ESP_LOGI(TAG, "  Max nodes: %d (0 = unlimited)", maxNodes);
    
    // Start fast discovery mode on Gateway
    radio.startFastDiscoveryMode(durationMs);
    ESP_LOGI(TAG, "✅ Gateway entered fast discovery mode (HELLO every 30s)");
    
    // Broadcast to all existing nodes to enter fast discovery mode
    radio.broadcastHelloModeChange(HELLO_MODE_FAST_DISCOVERY, durationMs);
    ESP_LOGI(TAG, "✅ Broadcasted fast discovery command to all nodes");
    
    // Update gateway state
    gatewayState.provisioningActive = true;
    gatewayState.provisioningStartTime = millis();
    gatewayState.provisioningEndTime = millis() + durationMs;
    gatewayState.provisioningCommandId = cmd.id;
    gatewayState.nodesDiscoveredDuringProvisioning = 0;
    
    // Update command result for Mobile App
    if (commandPoller) {
        commandPoller->updateCommandResult(
            cmd.id,
            "processing",
            "Fast discovery mode activated - waiting for nodes to join"
        );
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Provisioning Mode ACTIVE                                  ║");
    ESP_LOGI(TAG, "║  - New nodes can join automatically                        ║");
    ESP_LOGI(TAG, "║  - Will stop automatically in %.1f minutes                 ║", durationMs / 60000.0f);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

void GatewayApp::handleStopProvisioning(const FirebaseCommandPoller::Command& cmd) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  STOP PROVISIONING via Firebase Command                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    if (!gatewayState.provisioningActive) {
        ESP_LOGW(TAG, "Provisioning not active - nothing to stop");
        
        if (commandPoller) {
            commandPoller->moveToFailed(cmd, "NOT_ACTIVE", 
                                       "Provisioning mode is not active");
        }
        return;
    }
    
    // Stop fast discovery mode
    radio.stopFastDiscoveryMode();
    ESP_LOGI(TAG, "✅ Gateway returned to normal mode (HELLO every 120s)");
    
    // Broadcast to all nodes to return to normal mode
    radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
    ESP_LOGI(TAG, "✅ Broadcasted normal mode command to all nodes");
    
    // Calculate statistics
    uint32_t duration = millis() - gatewayState.provisioningStartTime;
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Provisioning Statistics:");
    ESP_LOGI(TAG, "  Duration: %.1f seconds", duration / 1000.0f);
    ESP_LOGI(TAG, "  Nodes discovered: %d", gatewayState.nodesDiscoveredDuringProvisioning);
    
    // Mark command as completed
    if (commandPoller) {
        String message = String("Provisioning stopped. ") + 
                        gatewayState.nodesDiscoveredDuringProvisioning + 
                        " nodes discovered in " + 
                        String(duration / 1000) + " seconds";
        
        commandPoller->moveToCompleted(cmd, "success", message);
    }
    
    // Reset provisioning state
    gatewayState.provisioningActive = false;
    gatewayState.provisioningCommandId = "";
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Provisioning Mode STOPPED                                 ║");
    ESP_LOGI(TAG, "║  Gateway returned to normal operation                      ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

void GatewayApp::handleAssignNetkey(const FirebaseCommandPoller::Command& cmd) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ASSIGN NETKEY via Firebase Command                       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    // Load network config from NVS (netkey was stored during gateway registration)
    NetworkConfig cfg;
    if (!NVSStorageService::loadNetworkConfig(cfg)) {
        ESP_LOGE(TAG, "❌ Failed to load network config from NVS - Gateway not registered?");
        
        if (commandPoller) {
            commandPoller->moveToFailed(cmd, "NO_NETKEY", 
                                       "Network key not found. Please register gateway first.");
        }
        return;
    }
    
    if (!cfg.initialized) {
        ESP_LOGE(TAG, "❌ Network config not initialized - Gateway not registered?");
        
        if (commandPoller) {
            commandPoller->moveToFailed(cmd, "NETKEY_NOT_INITIALIZED", 
                                       "Network key not initialized. Please register gateway first.");
        }
        return;
    }
    
    ESP_LOGI(TAG, "✅ Loaded netkey from NVS:");
    ESP_LOGI(TAG, "   Network ID: 0x%04X", cfg.networkId);
    ESP_LOGI(TAG, "   Key Version: %d", cfg.keyVersion);
    
    // Update Gateway's local network key (ensure it's current)
    if (!NetkeyDistributionService::updateLocalNetworkKey(cfg.networkKey, cfg.authToken, 
                                                          cfg.networkId, cfg.keyVersion)) {
        ESP_LOGE(TAG, "❌ Failed to update Gateway local network key");
        
        if (commandPoller) {
            commandPoller->moveToFailed(cmd, "LOCAL_UPDATE_FAILED", 
                                       "Failed to update Gateway's local network key");
        }
        return;
    }
    
    ESP_LOGI(TAG, "✅ Gateway local network key updated successfully");
    
    // Distribute netkey to all nodes in routing table
    ESP_LOGI(TAG, "*** DISTRIBUTING NETKEY TO ALL NODES IN ROUTING TABLE ***");
    
    bool distributionSuccess = false;
    size_t routingTableSize = RoutingTableService::routingTableSize();
    int nodesSuccessful = 0;
    
    if (routingTableSize > 0) {
        ESP_LOGI(TAG, "Found %d nodes in routing table, distributing netkey...", 
                 (int)routingTableSize);
        
        // Get all nodes from routing table
        NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
        if (nodes) {
            distributionSuccess = NetkeyDistributionService::distributeNetkeyToAllNodes(
                cfg.networkKey, 
                cfg.authToken, 
                cfg.networkId, 
                cfg.keyVersion,
                nodes,
                routingTableSize
            );
            
            // Count successful nodes (approximation - distributeNetkeyToAllNodes returns overall success)
            nodesSuccessful = distributionSuccess ? routingTableSize : 0;
            
            delete[] nodes; // Clean up allocated memory
            
            ESP_LOGI(TAG, "Network-wide netkey distribution: %s", 
                     distributionSuccess ? "SUCCESS" : "PARTIAL/FAILED");
        } else {
            ESP_LOGE(TAG, "❌ Failed to get nodes from routing table");
            
            if (commandPoller) {
                commandPoller->moveToFailed(cmd, "NO_NODES", 
                                           "Failed to retrieve nodes from routing table");
            }
            return;
        }
    } else {
        ESP_LOGW(TAG, "⚠️  No nodes in routing table - nothing to provision");
        
        if (commandPoller) {
            commandPoller->moveToFailed(cmd, "NO_NODES", 
                                       "No nodes found in routing table. Start provisioning first.");
        }
        return;
    }
    
    // Mark command as completed
    if (commandPoller) {
        String message = String("Netkey distributed to ") + 
                        String(nodesSuccessful) + " of " + 
                        String((int)routingTableSize) + " nodes";
        
        if (distributionSuccess) {
            commandPoller->moveToCompleted(cmd, "success", message);
        } else {
            commandPoller->moveToFailed(cmd, "PARTIAL_FAILURE", message);
        }
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Netkey Distribution Completed                             ║");
    ESP_LOGI(TAG, "║  Nodes: %d/%d successful                                   ║", 
             nodesSuccessful, (int)routingTableSize);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

void GatewayApp::updateProvisioningProgress() {
    if (!gatewayState.provisioningActive || !commandPoller) {
        return;
    }
    
    // Update every 5 seconds
    static uint32_t lastUpdate = 0;
    uint32_t now = millis();
    
    if (now - lastUpdate < 5000) {
        return;
    }
    
    lastUpdate = now;
    
    // Get current routing table size (nodes discovered)
    LM_LinkedList<RouteNode>* rtList = RoutingTableService::routingTableList;
    if (rtList) {
        gatewayState.nodesDiscoveredDuringProvisioning = rtList->getLength();
    }
    
    // Calculate time remaining
    uint32_t timeRemaining = 0;
    if (now < gatewayState.provisioningEndTime) {
        timeRemaining = gatewayState.provisioningEndTime - now;
    }
    
    // Update Firebase progress
    commandPoller->updateProgress(
        gatewayState.provisioningCommandId,
        gatewayState.nodesDiscoveredDuringProvisioning,
        timeRemaining
    );
    
    ESP_LOGD(TAG, "📊 Provisioning progress: %d nodes, %u ms remaining",
             gatewayState.nodesDiscoveredDuringProvisioning, timeRemaining);
}
