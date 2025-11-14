#include "gateway_app.h"
#include "firebase_queue.h"  // Include Firebase queue system
#include "components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "mesh_security_config.h"
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <map>

static const char* TAG = "GATEWAY";

// Helper macros for command poller operations (WiFi vs Cellular)
#ifdef USE_CELLULAR
    #define COMMAND_POLLER cellularCommandPoller
#else
    #define COMMAND_POLLER commandPoller
#endif

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
#ifdef USE_CELLULAR
      cellularService(nullptr),
      sslClient(nullptr),
      firebaseClient(nullptr),
      cellularCommandPoller(nullptr),
      commandQueue(nullptr),
#else
      wifiService(nullptr),
      firebaseClient(nullptr),
      commandPoller(nullptr),
#endif
      statusCounter(0),
      sensorCounter(0),
      lastStatusUploadTime(0),
      statusPacket(new gatewayStatus),
      provisionManager(nullptr) {
    instance = this;
    gatewayState.bootTime = millis();
}

GatewayApp::~GatewayApp() {
    delete statusPacket;
    delete firebaseClient;
    delete provisionManager;
    
#ifdef USE_CELLULAR
    delete cellularCommandPoller;
    delete commandQueue;
    delete sslClient;
    delete cellularService;
#else
    delete commandPoller;
    delete wifiService;
#endif
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
    
    // Only setup mesh, WiFi/Cellular and Firebase if provisioned
    if (provisionManager && provisionManager->isProvisioned()) {
        setupLoRaMesher();
        
#ifdef USE_CELLULAR
        setupCellular();
#else
        setupWiFi();
#endif

        // Run time sync before Firebase to keep AT channel quiet while enabling CLTS/NTP
        setupTimeSync();

        setupFirebase();
        
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

    ESP_LOGI(TAG, "🎉✨ Setup complete. Gateway is ready! 🚀");
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

    // Periodic CPU core and task monitoring (every 30 seconds)
    static uint32_t lastCpuCheck = 0;
    if (millis() - lastCpuCheck > 30000) {  // Mỗi 30s
        ESP_LOGI(TAG, "[CPU] Core 0 running: %d tasks", uxTaskGetNumberOfTasks());
        ESP_LOGI(TAG, "[CPU] Core ID executing loop: %d", xPortGetCoreID());
        lastCpuCheck = millis();
    }

    // Show provision success LED pattern once
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

#ifdef USE_CELLULAR
    // Update Cellular service (handles auto-reconnect) - only if provisioned
    if (cellularService) {
        cellularService->update();
    }

    // Direct upload to Firebase when cellular connected (no offline buffer for cellular mode)
    if (isProvisioned && gatewayState.cellularConnected && firebaseClient) {
#else
    // Update WiFi service (handles auto-reconnect) - only if provisioned
    if (wifiService) {
        wifiService->update();
    }

    // Sync offline buffer to Firebase when online (only if provisioned)
    if (isProvisioned && gatewayState.wifiConnected && firebaseClient) {
#endif
        static uint32_t lastBufferSync = 0;
        const uint32_t BUFFER_SYNC_INTERVAL = 60000; // Sync every 1 minute when online
        
        if (currentTime - lastBufferSync >= BUFFER_SYNC_INTERVAL) {
            uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
            
            if (bufferedCount > 0) {
                ESP_LOGI(TAG, "📤 Syncing offline buffer: %u samples pending", bufferedCount);

                // Upload up to 1 sample per cycle to avoid blocking
                const uint16_t MAX_UPLOADS_PER_CYCLE = 1;
                uint16_t uploaded = 0;
                
                for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
                    String nodeId;
                    sensorData data;
                    
                    if (OfflineDataBuffer::getOldestData(nodeId, data)) {
                        bool success = false;
                        
#ifdef USE_CELLULAR
                        // Cellular: Direct upload (blocking)
                        auto result = firebaseClient->uploadSensorData(data, 0, 0.0f);
                        success = result.success;
#else
                        // WiFi: Use queue for non-blocking upload
                        if (firebaseClient->isQueueRunning()) {
                            // Queue-based upload (non-blocking, runs on CPU1)
                            success = firebaseClient->queueSensorData(data, 0, 0.0f, 2); // Normal priority
                            ESP_LOGD(TAG, "📤 Queued buffered data from %s", nodeId.c_str());
                        } else {
                            // Fallback to direct upload (blocking) - rare case
                            ESP_LOGD(TAG, "📤 Direct upload buffered data from %s", nodeId.c_str());
                            
                            // NO WDT RESET: This is rare fallback, should complete or timeout naturally
                            auto result = firebaseClient->uploadSensorData(data, 0, 0.0f);
                            success = result.success;
                        }
#endif
                        
                        if (success) {
                            // Remove from buffer after successful queue or upload
                            OfflineDataBuffer::removeOldest();
                            uploaded++;
                            bufferedCount--;
                            ESP_LOGD(TAG, "✅ Synced buffered data from %s", nodeId.c_str());
                        } else {
                            // Failed to upload, keep in buffer and retry later
                            ESP_LOGW(TAG, "❌ Failed to sync buffered data from %s", nodeId.c_str());
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
    
#ifdef USE_CELLULAR
    // Re-sync with NTP every hour (if cellular connected and provisioned)
    if (isProvisioned && gatewayState.cellularConnected && (currentTime - lastNTPSync >= NTP_RESYNC_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic NTP re-sync over cellular");
#else
    // Re-sync with NTP every hour (if WiFi connected and provisioned)
    if (isProvisioned && gatewayState.wifiConnected && (currentTime - lastNTPSync >= NTP_RESYNC_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic NTP re-sync");
#endif
        if (TimeSyncService::syncWithNTP("pool.ntp.org", 25200, 0)) {
            gatewayState.ntpSynced = true;
            ESP_LOGI(TAG, "✅ NTP re-sync successful");
        }
        lastNTPSync = currentTime;
    }
    
    // Routing table upload strategy:
    // 1. Primary: Immediate upload via onRoutingTableChanged() callback when changes occur
    // 2. Initial: Upload once immediately after Firebase connection (post-reboot)
    // 3. Backup: Periodic upload every 5 minutes as fallback
    static bool firstRoutingTableUploadDone = false;
    
    // Upload immediately on first Firebase connection after boot
    if (gatewayState.firebaseConnected && !firstRoutingTableUploadDone) {
        ESP_LOGI(TAG, "📡 Initial routing table upload (post-reboot)");
        
#ifdef USE_CELLULAR
        // Cellular: Direct upload
        uploadRoutingTable();
#else
        // WiFi: Use queue for non-blocking routing table upload
        queueRoutingTableUpload(3); // High priority for initial upload
#endif
        gatewayState.lastRoutingTableUpload = currentTime;
        firstRoutingTableUploadDone = true;
    }
    // Then continue with periodic backup uploads
    else if (gatewayState.firebaseConnected &&
        (currentTime - gatewayState.lastRoutingTableUpload >= GATEWAY_ROUTING_TABLE_INTERVAL)) {
        ESP_LOGI(TAG, "⏰ Periodic backup routing table upload");
        
#ifdef USE_CELLULAR
        // Cellular: Direct upload
        uploadRoutingTable();
#else
        // WiFi: Use queue for non-blocking routing table upload
        queueRoutingTableUpload(2); // Normal priority for periodic upload
#endif
        // Always update timestamp even if upload fails to prevent rapid retries
        gatewayState.lastRoutingTableUpload = currentTime;
    }

    // Gateway sensor data collection and upload
    static uint32_t lastSensorUpload = 0;
    static bool firstUploadDone = false;
    
    // Upload immediately on first connection after boot
    if (gatewayState.firebaseConnected && !firstUploadDone) {
        ESP_LOGI(TAG, "📊 Initial Gateway sensor data upload (post-reboot)");
        
#ifdef USE_CELLULAR
        // Cellular: Direct upload
        uploadGatewaySensorData();
#else
        // WiFi: Use queue for non-blocking gateway sensor upload
        queueGatewaySensorDataUpload(3); // High priority for initial upload
#endif
        lastSensorUpload = currentTime;
        firstUploadDone = true;
    }
    // Then continue with periodic uploads
    else if (gatewayState.firebaseConnected &&
        (currentTime - lastSensorUpload >= GATEWAY_SENSOR_INTERVAL)) {
        ESP_LOGI(TAG, "📊 Periodic Gateway sensor data collection");
        
#ifdef USE_CELLULAR
        // Cellular: Direct upload
        uploadGatewaySensorData();
#else
        // WiFi: Use queue for non-blocking gateway sensor upload
        queueGatewaySensorDataUpload(2); // Normal priority for periodic upload
#endif
        lastSensorUpload = currentTime;
    }

    // Simple status LED indication
    if (statusCounter++ % 100 == 0) {
#ifdef USE_CELLULAR
        if (gatewayState.cellularConnected && gatewayState.firebaseConnected) {
            led_pattern_message(); // Quick flash for active
        } else if (gatewayState.cellularConnected) {
            led_flash(1, 500);     // Single slow flash for Cellular only
        } else {
#else
        if (gatewayState.wifiConnected && gatewayState.firebaseConnected) {
            led_pattern_message(); // Quick flash for active
        } else if (gatewayState.wifiConnected) {
            led_flash(1, 500);     // Single slow flash for WiFi only
        } else {
#endif
            led_pattern_error();   // Error pattern for disconnected
        }
    }

    // Check for Firebase commands (both WiFi and Cellular modes)
    if (isProvisioned && gatewayState.firebaseConnected) {
#ifdef USE_CELLULAR
        // Cellular mode: Check for commands (polling runs in dedicated task on CPU1)
        if (cellularCommandPoller && cellularCommandPoller->hasCommand()) {
            auto cmd = cellularCommandPoller->getNextCommand();
#else
        // WiFi mode: Check for commands (polling runs in dedicated task on CPU1)
        if (commandPoller && commandPoller->hasCommand()) {
            auto cmd = commandPoller->getNextCommand();
#endif
            
            ESP_LOGI(TAG, "📥 Processing command from Firebase:");
            ESP_LOGI(TAG, "  ID: %s", cmd.id.c_str());
            ESP_LOGI(TAG, "  Type: %s", cmd.type.c_str());
            ESP_LOGI(TAG, "  Priority: %d", cmd.priority);
            
            // Move to processing immediately
#ifdef USE_CELLULAR
            cellularCommandPoller->moveToProcessing(cmd);
#else
            commandPoller->moveToProcessing(cmd);
#endif
            
            // Handle command based on type
            if (cmd.type == "start_provisioning") {
                handleStartProvisioning(cmd);
            } else if (cmd.type == "stop_provisioning") {
                handleStopProvisioning(cmd);
            } else if (cmd.type == "assign_netkey") {
                handleAssignNetkey(cmd);
            } else {
                ESP_LOGW(TAG, "Unknown command type: %s", cmd.type.c_str());
                String message = "Unknown command type: ";
                message += cmd.type;
#ifdef USE_CELLULAR
                cellularCommandPoller->moveToFailed(cmd, "UNKNOWN_COMMAND", message);
#else
                commandPoller->moveToFailed(cmd, "UNKNOWN_COMMAND", message);
#endif
            }
        }
    }
    
    // Update provisioning progress if active
    if (gatewayState.provisioningActive) {
        updateProvisioningProgress();
        
        // Check if provisioning should end
        if (currentTime >= gatewayState.provisioningEndTime) {
            ESP_LOGI(TAG, "⏰ Provisioning timeout reached - stopping");
            
            // Stop provisioning
            radio.stopFastDiscoveryMode();
            radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
            
            // Mark command as completed
#ifdef USE_CELLULAR
            if (cellularCommandPoller && !gatewayState.provisioningCommandId.isEmpty()) {
                CellularFirebaseCommandPoller::Command cmd;
#else
            if (commandPoller && !gatewayState.provisioningCommandId.isEmpty()) {
                FirebaseCommandPoller::Command cmd;
#endif
                cmd.id = gatewayState.provisioningCommandId;
                String message = "Provisioning completed. Discovered " + String(gatewayState.nodesDiscoveredDuringProvisioning) + " nodes";
#ifdef USE_CELLULAR
                cellularCommandPoller->moveToCompleted(cmd, "success", message);
#else
                commandPoller->moveToCompleted(cmd, "success", message);
#endif
            }
            
            // Reset state
            gatewayState.provisioningActive = false;
            gatewayState.provisioningCommandId = "";
            gatewayState.nodesDiscoveredDuringProvisioning = 0;
        }
    }
    
    // Time synchronization with NTP (loop-based with retry and periodic refresh)
    bool shouldAttemptNtpSync = false;
    uint32_t ntpSyncInterval = 0;
    
    // Determine if NTP sync is needed
    if (!gatewayState.ntpSyncInProgress) {
        if (!gatewayState.ntpSynced) {
            // First sync: Wait 30s after boot, then retry every 30s (up to 3 attempts)
            if ((currentTime - gatewayState.bootTime >= 30000) && 
                (currentTime - gatewayState.lastNtpSyncAttempt >= 30000) && 
                (gatewayState.ntpRetryCount < 3)) {
                shouldAttemptNtpSync = true;
                ntpSyncInterval = 30000; // 30s retry interval
            }
        } else {
            // Periodic re-sync every 1 hour after successful first sync
            if (currentTime - gatewayState.lastSuccessfulNtpSync >= 3600000) { // 1 hour
                shouldAttemptNtpSync = true;
                ntpSyncInterval = 3600000; // 1 hour interval
            }
        }
    }
    
    // Perform time sync if needed
    if (shouldAttemptNtpSync) {
#ifdef USE_CELLULAR
        if (!gatewayState.cellularConnected) {
            ESP_LOGW(TAG, "🕒 Skipping time sync - Cellular not connected");
        } else {
            gatewayState.ntpSyncInProgress = true;
            gatewayState.lastNtpSyncAttempt = currentTime;
            
            ESP_LOGI(TAG, "🕒 Attempting modem time sync (attempt %d/%d)...", 
                     gatewayState.ntpRetryCount + 1, 3);
            
            // Cellular mode: Get time from modem
            if (syncTimeFromModem()) {
                ESP_LOGI(TAG, "✅ Time synchronized successfully via modem");
                
                gatewayState.ntpSynced = true;
                gatewayState.lastSuccessfulNtpSync = currentTime;
                gatewayState.ntpRetryCount = 0; // Reset retry counter on success
                gatewayState.ntpSyncInProgress = false;
                
                // Broadcast time to mesh nodes
                broadcastTimeSync();
            } else {
                ESP_LOGW(TAG, "❌ Modem time sync failed (attempt %d/%d)", 
                         gatewayState.ntpRetryCount + 1, 3);
                
                gatewayState.ntpRetryCount++;
                gatewayState.ntpSyncInProgress = false;
                
                // If all retries exhausted, wait for periodic re-attempt
                if (gatewayState.ntpRetryCount >= 3) {
                    ESP_LOGE(TAG, "🚨 Time sync failed after 3 attempts - will retry in 1 hour");
                    gatewayState.ntpSynced = false;
                    gatewayState.lastSuccessfulNtpSync = currentTime; // Prevent immediate retry
                }
            }
        }
#else
        if (!gatewayState.wifiConnected) {
            ESP_LOGW(TAG, "🕒 Skipping NTP sync - WiFi not connected");
        } else {
            gatewayState.ntpSyncInProgress = true;
            gatewayState.lastNtpSyncAttempt = currentTime;
            
            ESP_LOGI(TAG, "🕒 Attempting NTP time sync (attempt %d/%d)...", 
                     gatewayState.ntpRetryCount + 1, 3);
            
            // WiFi mode: Use NTP
            if (TimeSyncService::syncWithNTP("pool.ntp.org", 25200, 0)) {
                ESP_LOGI(TAG, "✅ NTP time synchronized successfully");
                
                gatewayState.ntpSynced = true;
                gatewayState.lastSuccessfulNtpSync = currentTime;
                gatewayState.ntpRetryCount = 0; // Reset retry counter on success
                gatewayState.ntpSyncInProgress = false;
                
                // Broadcast time to mesh nodes
                broadcastTimeSync();
            } else {
                ESP_LOGW(TAG, "❌ NTP sync failed (attempt %d/%d)", 
                         gatewayState.ntpRetryCount + 1, 3);
                
                gatewayState.ntpRetryCount++;
                gatewayState.ntpSyncInProgress = false;
                
                // If all retries exhausted, wait for periodic re-attempt
                if (gatewayState.ntpRetryCount >= 3) {
                    ESP_LOGE(TAG, "🚨 NTP sync failed after 3 attempts - will retry in 1 hour");
                    gatewayState.ntpSynced = false;
                    gatewayState.lastSuccessfulNtpSync = currentTime; // Prevent immediate retry
                }
            }
        }
#endif
    }

    // Upload gateway status every 60 seconds (use queue for non-blocking)
    static uint32_t lastStatusUploadTime = 0;
    const uint32_t STATUS_UPLOAD_INTERVAL = 60000; // 60 seconds
    if (currentTime - lastStatusUploadTime >= STATUS_UPLOAD_INTERVAL) {
#ifdef USE_CELLULAR
        // Cellular: Direct upload
        uploadGatewayStatusPeriodic();
#else
        // WiFi: Use queue for non-blocking status upload
        queueGatewayStatusUpload(1); // Low priority for periodic status
#endif
        lastStatusUploadTime = currentTime;
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

#ifdef USE_CELLULAR
void GatewayApp::setupCellular() {
    ESP_LOGI(TAG, "Setting up Cellular connection...");

    // Create cellular service instance with APN config
    CellularConnectionService::APNConfig apnConfig(CELLULAR_APN, CELLULAR_APN_USER, CELLULAR_APN_PASS);
    cellularService = new CellularConnectionService(
        apnConfig,
        true,  // auto-reconnect enabled
        5000   // reconnect interval: 5 seconds
    );

    // Register cellular event callback
    cellularService->onEvent([this](CellularConnectionService::Event event, int8_t rssi) {
        this->handleCellularEvent(event, rssi);
    });

    // Set signal quality threshold for low signal warning (10 = -93 dBm approximately)
    cellularService->setSignalQualityThreshold(10);

    // Initialize cellular service
    if (!cellularService->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize cellular service");
        led_pattern_error();
        return;
    }

    // Connect to cellular network (with 60-second timeout)
    ESP_LOGI(TAG, "Connecting to cellular network...");
    if (cellularService->connect(60000)) {
        gatewayState.cellularConnected = true;
        int8_t signalStrength = cellularService->getSignalStrength();
        ESP_LOGI(TAG, "Cellular connected! Operator: %s, RSSI: %d dBm",
                 cellularService->getOperator().c_str(), signalStrength);
    } else {
        ESP_LOGW(TAG, "Cellular connection failed, but auto-reconnect is enabled");
        gatewayState.cellularConnected = false;
    }
}
#else
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
#endif

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
#ifdef USE_CELLULAR
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String gatewayMAC = String(macStr);
#else
    String gatewayMAC = WiFi.macAddress();
#endif
    ESP_LOGI(TAG, "Gateway MAC: %s", gatewayMAC.c_str());

#ifdef USE_CELLULAR
    // Cellular-based Firebase client with HTTPS
    ESP_LOGI(TAG, "Creating Cellular HTTPS Firebase client...");
    
    // Create SSL client first
    sslClient = new CellularSSLClient(cellularService);
    
    // // Attempt time sync from modem (opportunistic, not blocking)
    // // This is just an attempt - main retry logic is in loop() with periodic backoff
    // if (!gatewayState.ntpSynced && cellularService) {
    //     ESP_LOGI(TAG, "🕒 Attempting to sync time from cellular modem...");
    //     if (cellularService->syncTimeFromNetwork()) {
    //         gatewayState.ntpSynced = true;
    //         gatewayState.lastSuccessfulNtpSync = millis();
    //         gatewayState.ntpRetryCount = 0;  // Reset retry counter on success
    //         ESP_LOGI(TAG, "✅ Time initialized from cellular network (modem)");
    //     } else {
    //         // Don't block setup - let loop() handle periodic NTP retry
    //         // This allows setup to continue while NTP retries in background
    //         ESP_LOGW(TAG, "⚠️ Modem time sync failed; will retry via NTP in loop (30s intervals)");
    //         // Keep ntpSynced = false so loop() will retry
    //     }
    // }
    
    // Create HTTPS Firebase client
    firebaseClient = new CellularFirebaseHTTPSClient(
        sslClient,
        FIREBASE_HOST,
        FIREBASE_AUTH,
        gatewayMAC.c_str()
    );
    
    // Set user context for multi-user Firebase paths
    firebaseClient->setUserContext(userUID, gatewayMAC);

    // Initialize Firebase client (this will initialize SSL client too)
    if (!firebaseClient->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize Cellular HTTPS Firebase client");
        led_pattern_error();
        return;
    }

    // ⚠️ NOTE: Skipping testConnection() because modem doesn't receive response (may timeout)
    // However, actual data uploads work fine - issue is response receiving, not sending
    // So we'll assume connected and attempt uploads. Real status determined by successful uploads.
    gatewayState.firebaseConnected = true;
    ESP_LOGI(TAG, "🔥 Cellular HTTPS Firebase READY (connection test skipped - will retry on first upload)");
    ESP_LOGI(TAG, "🔥 User: %s, Gateway: %s", userUID.c_str(), gatewayMAC.c_str());

    // Log gateway started event
    firebaseClient->logEvent("gateway_started", gatewayMAC, "Gateway initialized with cellular HTTPS");
    
    // Initialize Cellular Command Poller
    cellularCommandPoller = new CellularFirebaseCommandPoller(
        firebaseClient,
        userUID,
        gatewayMAC
    );
    // // Start command polling task on CPU1 (same as WiFi mode)
    // // Stack: 8KB, Priority: 1, Core: 1 (CPU1)
    // cellularCommandPoller->begin(8192, 1, 1);
    // ESP_LOGI(TAG, "✅ Cellular Command Poller initialized (30s interval, CPU1 task)");
    
    // ESP_LOGI(TAG, "✅ Cellular Firebase ready (HTTPS mode with command polling on CPU1)");
    
#else
    // WiFi-based Firebase client (existing code)
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

        // NEW: Initialize Firebase Queue System on CPU1
        ESP_LOGI(TAG, "🚀 Initializing Firebase Queue System...");
        if (firebaseClient->initializeQueue()) {
            ESP_LOGI(TAG, "✅ Firebase Queue System initialized successfully");
            ESP_LOGI(TAG, "  Worker task running on CPU1");
            ESP_LOGI(TAG, "  Queue size: 100 items");
            ESP_LOGI(TAG, "  Min operation interval: 500ms");
            
            // Phase 3: Configure advanced features
            FirebaseQueueManager& queueManager = FirebaseQueueManager::getInstance();
            
            // Enable batch optimization for better throughput (using available method)
            queueManager.enableBatchOptimization(true);
            ESP_LOGI(TAG, "📦 Batch optimization enabled");
            
            // Set adaptive retry strategy for intelligent retry handling
            queueManager.setRetryStrategy(RETRY_STRATEGY_ADAPTIVE);
            ESP_LOGI(TAG, "🔄 Adaptive retry strategy enabled");
            
            // Configure for throughput optimization mode
            queueManager.setQueueMode(QUEUE_MODE_THROUGHPUT_OPTIMIZED);
            ESP_LOGI(TAG, "🚀 Queue configured for throughput optimization");
            
            // Enable queue persistence for reliability (using available method)
            queueManager.enableQueuePersistence(true);
            ESP_LOGI(TAG, "💾 Queue persistence enabled");
            
        } else {
            ESP_LOGE(TAG, "❌ Failed to initialize Firebase Queue System");
            ESP_LOGW(TAG, "   Will fall back to direct Firebase calls (blocking)");
        }

        // Upload initial gateway info (use queue if available)
        if (firebaseClient->isQueueRunning()) {
            // Queue-based upload (non-blocking)
            ESP_LOGI(TAG, "📤 Queuing initial gateway info upload...");
            firebaseClient->queueLogEvent("gateway_started", gatewayMAC, 
                                         "Gateway initialized with queue system", 3); // High priority
        } else {
            // Direct upload (blocking - fallback)
            ESP_LOGI(TAG, "📤 Direct upload of initial gateway info...");
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
        }
        
        // NEW: Create and initialize command poller with dedicated task
        ESP_LOGI(TAG, "Initializing Firebase Command Poller...");
        commandPoller = new FirebaseCommandPoller(
            firebaseClient->getFirebaseData(),  // Share FirebaseData with client
            userUID,
            gatewayMAC
        );
        // CRASH FIX (Oct 24, 2025): Move Command Poller to CPU0 to reduce CPU1 load
        // CPU1 already runs Firebase Worker task - moving this to CPU0 balances the load
        // Also increased stack from 8KB to 12KB for safety
        // Stack: 12KB (was 8KB), Priority: 1, Core: 0 (was 1 - moved from CPU1 to CPU0)
        commandPoller->begin(12288, 1, 0);
        ESP_LOGI(TAG, "✅ Command poller ready - runs on CPU0 (Firebase Worker on CPU1)");
        
    } else {
        ESP_LOGW(TAG, "Firebase connection failed: %s", firebaseClient->getLastError().c_str());
        gatewayState.firebaseConnected = false;
    }
#endif
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

    // ⚠️ CRITICAL: Memory safety check before processing (Oct 21, 2025)
    // Prevents stack overflow and heap corruption
    uint32_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    uint32_t heapFree = esp_get_free_heap_size();
    
    const uint32_t MIN_STACK_THRESHOLD = 3072;   // 3 KB minimum stack
    const uint32_t MIN_HEAP_THRESHOLD = 20480;   // 20 KB minimum heap
    
    if (stackFree < MIN_STACK_THRESHOLD || heapFree < MIN_HEAP_THRESHOLD) {
        ESP_LOGW(TAG, "❌ [SAFETY] Insufficient memory - Stack: %u bytes (need %u), Heap: %u bytes (need %u). Skipping packet processing.",
                 stackFree, MIN_STACK_THRESHOLD, heapFree, MIN_HEAP_THRESHOLD);
        gatewayState.uploadErrors++;
        
        // Buffer data if possible for later processing
        if (packet->payloadSize >= sizeof(sensorData)) {
            sensorData* s = reinterpret_cast<sensorData*>(packet->payload);
            char nodeIdStr[16];
            snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", packet->src);
            String nodeIdStr_obj(nodeIdStr);
            OfflineDataBuffer::addData(nodeIdStr_obj, *s);
            ESP_LOGI(TAG, "📦 Buffered sensor data for node %s to offline storage", nodeIdStr);
        }
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

        // Check if this is a duplicate packet (same counter from same node)
        bool isDuplicate = false;
        auto it = lastProcessedCounter.find(sourceNode);
        if (it != lastProcessedCounter.end() && it->second == s->counter) {
            isDuplicate = true;
            ESP_LOGD(TAG, "⚠️ Duplicate sensor data detected from node %s (counter: %u) - skipping", 
                     nodeIdStr, s->counter);
        }

        // Try to upload to Firebase if online and provisioned (and not duplicate)
        if (firebaseClient && gatewayState.firebaseConnected && !isDuplicate) {
            // ESP_LOGI(TAG, "☁️ Uploading sensor data from node %s to Firebase", nodeIdStr);
            
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

            bool success = false;
            
#ifdef USE_CELLULAR
            // Cellular: Direct upload (blocking)
            auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
            success = result.success;
#else
            // WiFi: Use queue for non-blocking sensor upload
            if (firebaseClient->isQueueRunning()) {
                // Queue-based upload (non-blocking, runs on CPU1)
                success = firebaseClient->queueSensorData(*s, rssi, snr, 2); // Normal priority
                ESP_LOGD(TAG, "📤 Queued sensor data from node %s", nodeIdStr);
            } else {
                // Fallback to direct upload (blocking) - rare case when queue not available
                ESP_LOGW(TAG, "📤 Queue not available, using direct upload (blocking)");
                
                // NO WDT RESET: Fallback is rare, should complete or timeout naturally
                auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
                success = result.success;
            }
#endif

            if (success) {
                gatewayState.packetsUploaded++;
                led_pattern_message(); // Flash LED on successful upload
                ESP_LOGI(TAG, "✅ Upload queued/successful");
                
                // Update last processed counter for this node
                lastProcessedCounter[sourceNode] = s->counter;
                
                // MEMORY FIX (Oct 23, 2025): Force garbage collection after upload
                // Small delay to allow TCP connection cleanup
                delay(10);
            } else {
                gatewayState.uploadErrors++;
                
                // Buffer data when upload fails (network issue, Firebase error, etc.)
                // This covers: WiFi connected but no internet, Firebase overload, API errors
                ESP_LOGW(TAG, "❌ Firebase queue/upload failed");
                ESP_LOGI(TAG, "📦 Buffering data to NVS for later sync...");
                
                if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
                    uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
                    ESP_LOGI(TAG, "✅ Data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
                    
                    // Update last processed counter to prevent duplicate buffering
                    lastProcessedCounter[sourceNode] = s->counter;
                } else {
                    ESP_LOGW(TAG, "⚠️ Failed to buffer data - NVS full or error");
                    // Don't update counter - allow retry on next packet
                }
            }
        } else if (!isDuplicate) {
            // Not provisioned or offline - buffer data to NVS
            ESP_LOGD(TAG, "📦 Gateway offline - buffering data from node %s", nodeIdStr);
            
            if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
                uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
                ESP_LOGI(TAG, "📦 Data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
                
                // Update last processed counter to prevent duplicate buffering
                lastProcessedCounter[sourceNode] = s->counter;
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

    // NO WDT RESET: Let operation complete naturally or timeout
    auto result = firebaseClient->uploadRoutingTable(routingTable);

    if (result.success) {
        gatewayState.lastRoutingTableUpload = millis();
        if (routingTable.size() == 0) {
            ESP_LOGI(TAG, "✅ Empty routing table uploaded - Firebase cleared");
        } else {
#ifdef USE_CELLULAR
            ESP_LOGI(TAG, "✅ Routing table uploaded (%d ms)", result.responseTime);
#else
            ESP_LOGI(TAG, "✅ Routing table uploaded (%d bytes)", result.payloadSize);
#endif
        }
    } else {
#ifdef USE_CELLULAR
        ESP_LOGW(TAG, "❌ Failed to upload routing table: %s", result.message.c_str());
#else
        ESP_LOGW(TAG, "❌ Failed to upload routing table: %s", result.errorMessage.c_str());
#endif
    }
}

#ifdef USE_CELLULAR
void GatewayApp::handleCellularEvent(CellularConnectionService::Event event, int8_t rssi) {
    switch (event) {
        case CellularConnectionService::Event::CONNECTED:
            ESP_LOGI(TAG, "✅ Cellular CONNECTED! Operator: %s, RSSI: %d dBm",
                     cellularService->getOperator().c_str(), rssi);
            gatewayState.cellularConnected = true;
            gatewayState.cellularRSSI = rssi;
            led_pattern_connected();

            // Try to reconnect Firebase if it was disconnected
            if (firebaseClient && !gatewayState.firebaseConnected) {
                if (firebaseClient->testConnection()) {
                    gatewayState.firebaseConnected = true;
                    firebaseClient->logEvent("firebase_reconnected", "", "Cellular reconnected");
                }
            }
            break;

        case CellularConnectionService::Event::DISCONNECTED:
            ESP_LOGW(TAG, "❌ Cellular DISCONNECTED!");
            gatewayState.cellularConnected = false;
            gatewayState.firebaseConnected = false;
            led_pattern_error();

            if (firebaseClient) {
                firebaseClient->logEvent("cellular_disconnected", "", "");
            }
            break;

        case CellularConnectionService::Event::RECONNECTING:
            ESP_LOGI(TAG, "⏳ Cellular RECONNECTING...");
            led_flash(2, 250);  // Double flash pattern for reconnecting
            break;

        case CellularConnectionService::Event::CONNECTION_FAILED:
            ESP_LOGE(TAG, "❌ Cellular CONNECTION FAILED!");
            gatewayState.cellularConnected = false;
            led_pattern_error();
            break;

        case CellularConnectionService::Event::SIGNAL_LOW:
            ESP_LOGW(TAG, "⚠️ Cellular signal LOW! RSSI: %d dBm", rssi);
            gatewayState.cellularRSSI = rssi;
            break;
    }
}
#else
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
#endif

// Static callback for processing gateway packets
void GatewayApp::processGatewayPackets(void* parameter) {
    ESP_LOGI(TAG, "[GATEWAY-TASK] Gateway packet processing task started (Oct 29, 2025 - v2 - WDT FIX)");
    
    // Stack monitoring - check initial stack
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[GATEWAY-TASK] Initial stack high water mark: %d bytes free", stackHighWaterMark);
    
    // Memory leak detection - track heap usage
    uint32_t initialFreeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = initialFreeHeap;
    uint32_t packetCount = 0;
    ESP_LOGI(TAG, "[GATEWAY-TASK] Initial free heap: %u bytes", initialFreeHeap);

    // WDT configuration: Reset every N packets to prevent false-positive reboots
    const uint32_t MAX_PACKETS_PER_WDT_CYCLE = 20; // Reset WDT every 20 packets max
    uint32_t packetsThisCycle = 0;

    for (;;) {
        // CRITICAL FIX (Oct 29, 2025): Reset WDT before blocking wait
        // This prevents watchdog timeout during idle periods
        esp_task_wdt_reset();
        
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

        // Reset cycle counter for new batch
        packetsThisCycle = 0;

        while (GatewayApp::instance->radio.getReceivedQueueSize() > 0) {
            ESP_LOGD(TAG, "[GATEWAY-TASK] Processing received mesh packet");
            ESP_LOGD(TAG, "[GATEWAY-TASK] Queue size: %d", GatewayApp::instance->radio.getReceivedQueueSize());

            // CRITICAL FIX (Oct 29, 2025): Reset WDT periodically during heavy load
            // Previous design: NO WDT reset → timeout if Firebase slow → random reboot
            // New design: Reset every N packets to prevent false-positive timeout
            packetsThisCycle++;
            if (packetsThisCycle >= MAX_PACKETS_PER_WDT_CYCLE) {
                esp_task_wdt_reset();
                packetsThisCycle = 0;
                ESP_LOGD(TAG, "[GATEWAY-TASK] WDT reset after %u packets", MAX_PACKETS_PER_WDT_CYCLE);
            }

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
        
        // MEMORY FIX (Oct 23, 2025): Adjusted threshold from 30KB to 20KB
        // With StaticJsonDocument fixes, memory should stabilize above 35KB
        // Critical warning at 20KB gives 5KB buffer before OOM
        if (freeHeapAfter < 20000) {
            ESP_LOGE(TAG, "🚨 [CRITICAL] Low heap memory! Only %u bytes free!", freeHeapAfter);
        }
    }
}

TaskHandle_t GatewayApp::createGatewayReceiveTask() {
    TaskHandle_t taskHandle = NULL;

    ESP_LOGI(TAG, "Creating gateway receive task...");

    // CPU ARCHITECTURE (Oct 23, 2025):
    // Pin to CPU0 for mesh/protocol processing separation from Firebase (CPU1)
    int res = xTaskCreatePinnedToCore(
        processGatewayPackets,
        "Gateway Receive Task",
        8192,  // Stack: 8KB (prevents stack overflow)
        (void*) 1,
        2,      // Priority: 2 (higher than Firebase queue)
        &taskHandle,
        0);     // Core 0: Mesh/protocol processing

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

    ESP_LOGI(TAG, "🔄 Routing table changed - triggering immediate upload");
#ifdef USE_CELLULAR
    // Cellular: Direct upload
    instance->uploadRoutingTable();
#else
    // WiFi: Use queue for non-blocking upload
    instance->queueRoutingTableUpload(3); // High priority for immediate changes
#endif
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
    
#ifdef USE_CELLULAR
    // Wait for Cellular connection before time sync
    if (!gatewayState.cellularConnected) {
        ESP_LOGW(TAG, "Cellular not connected yet, will sync time later");
        return;
    }
    
    // Cellular mode: Get time via modem AT command (most reliable)
    if (syncTimeFromModem()) {
        gatewayState.ntpSynced = true;
        gatewayState.lastTimeSyncBroadcast = millis();
        ESP_LOGI(TAG, "✅ Time sync successful via modem - Gateway time synchronized");
        
        // Immediately broadcast time to nodes
        broadcastTimeSync();
    } else {
        ESP_LOGE(TAG, "❌ Modem time sync failed - will retry later");
        gatewayState.ntpSynced = false;
    }
#else
    // WiFi mode: Use NTP (works well over WiFi)
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
#endif
}

#ifdef USE_CELLULAR
bool GatewayApp::syncTimeFromModem() {
    ESP_LOGI(TAG, "🕒 Syncing time from modem via network time (CLTS)...");
    
    if (!cellularService || !cellularService->getATHandler()) {
        ESP_LOGE(TAG, "Cellular service or AT handler not available");
        return false;
    }
    
    ATCommandHandler* atHandler = cellularService->getATHandler();
    
    // Step 1: Enabling automatic timezone update
    ESP_LOGI(TAG, "Step 1: Enabling automatic timezone update (AT+CTZU=1)...");
    auto ctzuResp = atHandler->sendCommand("+CTZU=1", 3000);
    if (!ctzuResp.success) {
        ESP_LOGW(TAG, "CTZU command failed: %s", ctzuResp.data.c_str());
    }
    delay(500);
    
    // Step 2: Query modem clock via AT+CCLK?
    ESP_LOGI(TAG, "Step 2: Querying modem clock (AT+CCLK?)...");
    bool gotValidTime = false;
    String clockLine;
    const int maxClockAttempts = 20;
    const uint32_t clockRetryDelayMs = 3000;  // allow network time propagation
    for (int attempt = 0; attempt < maxClockAttempts; attempt++) {
        ESP_LOGD(TAG, "  CCLK attempt %d/%d", attempt + 1, maxClockAttempts);
        auto clockResp = atHandler->sendCommand("+CCLK?", 3000);
        if (clockResp.success) {
            int idx = clockResp.data.indexOf("+CCLK:");
            if (idx >= 0) {
                clockLine = clockResp.data.substring(idx);
                if (clockLine.indexOf("70/01/01") >= 0) {
                    ESP_LOGW(TAG, "Modem clock still default (70/01/01...). Waiting for network sync...");
                } else {
                    gotValidTime = true;
                    break;
                }
            }
        }
        delay(clockRetryDelayMs);
    }

    if (!gotValidTime) {
        ESP_LOGE(TAG, "Failed to read valid modem time after %d attempts (~%lus). Ensure SIM has network time service enabled.",
                 maxClockAttempts,
                 (unsigned long)((maxClockAttempts * clockRetryDelayMs) / 1000));
        return false;
    }

    int quoteStart = clockLine.indexOf('"');
    int quoteEnd = clockLine.indexOf('"', quoteStart + 1);
    if (quoteStart < 0 || quoteEnd < 0) {
        ESP_LOGE(TAG, "Malformed +CCLK response: %s", clockLine.c_str());
        return false;
    }

    String timeStr = clockLine.substring(quoteStart + 1, quoteEnd);
    ESP_LOGI(TAG, "Modem reported time: %s", timeStr.c_str());

    int year, month, day, hour, minute, second, tzValue;
    char tzSignChar;
    if (sscanf(timeStr.c_str(), "%d/%d/%d,%d:%d:%d%c%d", &year, &month, &day, &hour, &minute, &second, &tzSignChar, &tzValue) != 8) {
        ESP_LOGE(TAG, "Unable to parse +CCLK string: %s", timeStr.c_str());
        return false;
    }

    if (tzSignChar != '+' && tzSignChar != '-') {
        ESP_LOGE(TAG, "Invalid timezone sign in +CCLK response: %c", tzSignChar);
        return false;
    }

    // Convert to UTC epoch
    int yearFull = 2000 + year;
    struct tm localTime = {0};
    localTime.tm_year = yearFull - 1900;
    localTime.tm_mon = month - 1;
    localTime.tm_mday = day;
    localTime.tm_hour = hour;
    localTime.tm_min = minute;
    localTime.tm_sec = second;
    localTime.tm_isdst = 0;
    time_t localSeconds = mktime(&localTime);
    if (localSeconds == -1) {
        ESP_LOGE(TAG, "mktime failed for parsed modem time");
        return false;
    }

    int tzMinutes = tzValue * 15;  // value is in 15-minute increments
    if (tzSignChar == '-') {
        tzMinutes = -tzMinutes;
    }
    time_t utcSeconds = localSeconds - (tzMinutes * 60);
    if (utcSeconds <= 0) {
        ESP_LOGE(TAG, "Computed UTC timestamp invalid (%ld)", (long)utcSeconds);
        return false;
    }

    uint32_t unixtime = static_cast<uint32_t>(utcSeconds);
    ESP_LOGI(TAG, "Extracted unixtime from modem: %u", unixtime);
    
    // Step 3: Set system time from unixtime
    struct timeval tv = { 
        .tv_sec = (time_t)unixtime,
        .tv_usec = 0
    };
    
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to set system time");
        return false;
    }

    // Update internal time sync service so application timestamps use the new clock
    TimeSyncService::setManualTimestamp(unixtime, true);
    
    // Log the synchronized time
    time_t systemTime = time(nullptr);
    struct tm* tminfo = localtime(&systemTime);
    char systemTimeStr[64];
    strftime(systemTimeStr, sizeof(systemTimeStr), "%Y-%m-%d %H:%M:%S", tminfo);

    int tzHours = tzMinutes / 60;
    int tzRemainMinutes = abs(tzMinutes % 60);
    ESP_LOGI(TAG, "✅ Time synchronized from modem network time");
    ESP_LOGI(TAG, "   📅 System time: %s (local TZ %+d:%02d)", systemTimeStr, tzHours, tzRemainMinutes);
    ESP_LOGI(TAG, "   🕒 Unix timestamp: %u", (uint32_t)systemTime);
    
    return true;
}
#endif

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
    
    // Get signal strength (WiFi RSSI or Cellular RSSI)
#ifdef USE_CELLULAR
    int16_t signalRssi = cellularService ? cellularService->getSignalStrength() : -90;
#else
    int8_t signalRssi = wifiService ? wifiService->getRSSI() : -90;
#endif
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
        ESP_LOGI(TAG, "📶 Signal - RSSI: %d dBm, SNR: %.1f dB", signalRssi, gatewaySnr);
        
        // NO WDT RESET: Upload should complete or timeout naturally
        auto result = firebaseClient->uploadSensorData(gatewaySensor, signalRssi, gatewaySnr);
        
        if (result.success) {
            gatewayState.packetsUploaded++;
#ifdef USE_CELLULAR
            ESP_LOGI(TAG, "✅ Gateway sensor upload successful (%d ms)", result.responseTime);
#else
            ESP_LOGI(TAG, "✅ Gateway sensor upload successful (%d bytes)", result.payloadSize);
#endif
        } else {
            gatewayState.uploadErrors++;
#ifdef USE_CELLULAR
            ESP_LOGW(TAG, "❌ Gateway sensor upload failed: %s", result.message.c_str());
#else
            ESP_LOGW(TAG, "❌ Gateway sensor upload failed: %s", result.errorMessage.c_str());
#endif
            ESP_LOGI(TAG, "📦 Buffering Gateway sensor data to NVS for later sync...");
            
            // Buffer Gateway sensor data to NVS (same as Node data)
            if (OfflineDataBuffer::addData(String(nodeIdStr), gatewaySensor)) {
                uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
                ESP_LOGI(TAG, "✅ Gateway sensor data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
            } else {
                ESP_LOGW(TAG, "⚠️ Failed to buffer Gateway sensor data - NVS full or error");
            }
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
    
#ifdef USE_CELLULAR
    // Cellular mode: No isConnected() method - just check if client exists
    if (!firebaseClient) {
        lastStatusUploadTime = currentTime;
        return;
    }
#else
    // WiFi mode: Check isConnected()
    if (!firebaseClient || !firebaseClient->isConnected()) {
        lastStatusUploadTime = currentTime;
        return;
    }
#endif
    
    // Collect metrics
    uint16_t connectedNodes = 0;
    LM_LinkedList<RouteNode>* rtList = RoutingTableService::routingTableList;
    if (rtList) {
        connectedNodes = rtList->getLength();
    }
    
    uint32_t totalPacketsReceived = gatewayState.totalMeshPackets;
    uint32_t totalPacketsSent = gatewayState.packetsUploaded;
    
    // Get signal strength (WiFi RSSI or Cellular RSSI)
#ifdef USE_CELLULAR
    int16_t signalRssi = cellularService ? cellularService->getSignalStrength() : -90;
#else
    int8_t signalRssi = WiFi.RSSI();
#endif
    
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t uptimeSeconds = millis() / 1000;
    
    ESP_LOGI(TAG, "📊 Uploading Gateway status: nodes=%u, rx=%u, tx=%u, rssi=%d, heap=%u, uptime=%u",
             connectedNodes, totalPacketsReceived, totalPacketsSent, signalRssi, freeHeap, uptimeSeconds);
    
    // NO WDT RESET: Upload should complete or timeout naturally
    auto result = firebaseClient->uploadGatewayStatus(
        connectedNodes,
        totalPacketsReceived,
        totalPacketsSent,
        signalRssi,
        freeHeap,
        uptimeSeconds
    );
    
    if (result.success) {
        ESP_LOGI(TAG, "✅ Gateway status uploaded successfully");
        lastStatusUploadTime = currentTime;
    } else {
#ifdef USE_CELLULAR
        ESP_LOGW(TAG, "⚠️ Failed to upload Gateway status: %s", result.message.c_str());
        
        // Always update timestamp to prevent spam
        lastStatusUploadTime = currentTime;
        
        // For error cases, back off
        if (result.message.indexOf("timeout") != -1) {
            ESP_LOGD(TAG, "📊 Status upload timeout - backing off for 60 seconds");
        }
#else
        ESP_LOGW(TAG, "⚠️ Failed to upload Gateway status: %s", result.errorMessage.c_str());
        
        // Always update timestamp to prevent spam, especially for circuit breaker
        // This ensures we respect the 60-second interval even on failures
        lastStatusUploadTime = currentTime;
        
        // For circuit breaker errors, we should definitely back off
        if (result.errorMessage.indexOf("Circuit breaker") != -1 ||
            result.errorMessage.indexOf("cooldown") != -1) {
            ESP_LOGD(TAG, "📊 Circuit breaker active - backing off for 60 seconds");
        }
#endif
    }
}

// Command handler implementations (both WiFi and Cellular modes)

#ifdef USE_CELLULAR
void GatewayApp::handleStartProvisioning(const CellularFirebaseCommandPoller::Command& cmd) {
#else
void GatewayApp::handleStartProvisioning(const FirebaseCommandPoller::Command& cmd) {
#endif
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  START PROVISIONING via Firebase Command                  ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    // Parse parameters from JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, cmd.params);
    
    uint32_t durationMs = 600000;  // Default: 10 minutes
    uint16_t maxNodes = 0;         // Default: unlimited
    
    if (!error) {
        if (doc["durationMs"].is<uint32_t>()) {
            durationMs = doc["durationMs"];
        }
        if (doc["maxNodes"].is<uint16_t>()) {
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
#ifdef USE_CELLULAR
    if (cellularCommandPoller) {
        cellularCommandPoller->updateProgress(cmd.id, 0, durationMs);
#else
    if (commandPoller) {
        commandPoller->updateCommandResult(
            cmd.id,
            "processing",
            "Fast discovery mode activated - waiting for nodes to join"
        );
#endif
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Provisioning Mode ACTIVE                                  ║");
    ESP_LOGI(TAG, "║  - New nodes can join automatically                        ║");
    ESP_LOGI(TAG, "║  - Will stop automatically in %.1f minutes                 ║", durationMs / 60000.0f);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
}

#ifdef USE_CELLULAR
void GatewayApp::handleStopProvisioning(const CellularFirebaseCommandPoller::Command& cmd) {
#else
void GatewayApp::handleStopProvisioning(const FirebaseCommandPoller::Command& cmd) {
#endif
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  STOP PROVISIONING via Firebase Command                   ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    if (!gatewayState.provisioningActive) {
        ESP_LOGW(TAG, "Provisioning not active - nothing to stop");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "NOT_ACTIVE", 
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
    if (COMMAND_POLLER) {
        String message = "Provisioning stopped. ";
        message += String(gatewayState.nodesDiscoveredDuringProvisioning);
        message += " nodes discovered in ";
        message += String(duration / 1000);
        message += " seconds";
        
        COMMAND_POLLER->moveToCompleted(cmd, "success", message);
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

#ifdef USE_CELLULAR
void GatewayApp::handleAssignNetkey(const CellularFirebaseCommandPoller::Command& cmd) {
#else
void GatewayApp::handleAssignNetkey(const FirebaseCommandPoller::Command& cmd) {
#endif
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ASSIGN NETKEY via Firebase Command                       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Command ID: %s", cmd.id.c_str());
    
    // Load network config from NVS (netkey was stored during gateway registration)
    NetworkConfig cfg;
    if (!NVSStorageService::loadNetworkConfig(cfg)) {
        ESP_LOGE(TAG, "❌ Failed to load network config from NVS - Gateway not registered?");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "NO_NETKEY", 
                                       "Network key not found. Please register gateway first.");
        }
        return;
    }
    
    if (!cfg.initialized) {
        ESP_LOGE(TAG, "❌ Network config not initialized - Gateway not registered?");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "NETKEY_NOT_INITIALIZED", 
                                       "Network key not initialized. Please register gateway first.");
        }
        return;
    }
    
    ESP_LOGI(TAG, "✅ Loaded netkey from NVS:");
    ESP_LOGI(TAG, "   Network ID: 0x%04X", cfg.networkId);
    ESP_LOGI(TAG, "   Key Version: %d", cfg.keyVersion);
    
    // Update Gateway's local network key (ensure it's current) - fast operation
    if (!NetkeyDistributionService::updateLocalNetworkKey(cfg.networkKey, cfg.authToken, 
                                                          cfg.networkId, cfg.keyVersion)) {
        ESP_LOGE(TAG, "❌ Failed to update Gateway local network key");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "LOCAL_UPDATE_FAILED", 
                                       "Failed to update Gateway's local network key");
        }
        return;
    }
    
    ESP_LOGI(TAG, "✅ Gateway local network key updated successfully");
    
    // Check if there are nodes to provision
    size_t routingTableSize = RoutingTableService::routingTableSize();
    if (routingTableSize == 0) {
        ESP_LOGW(TAG, "⚠️  No nodes in routing table - nothing to provision");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "NO_NODES", 
                                       "No nodes found in routing table. Start provisioning first.");
        }
        return;
    }
    
    ESP_LOGI(TAG, "Found %d nodes in routing table", (int)routingTableSize);
    
    // ⚡ CPU OPTIMIZATION: Offload heavy netkey distribution to CPU1 worker task
    // Reason: Broadcasting netkey to ALL nodes can take 5-25 seconds (50+ nodes × 100-500ms each)
    //         Running on CPU0 main loop would block protocol processing
    ESP_LOGI(TAG, "🚀 Offloading netkey distribution to CPU1 worker task (non-blocking)");
    
    // Create task parameters
    NetkeyDistributionTask* taskParams = new NetkeyDistributionTask();
    taskParams->cmd = cmd;
    taskParams->config = cfg;
    taskParams->completed = new bool(false);
    
    // Create worker task on CPU1 (isolated from CPU0 main loop)
    BaseType_t result = xTaskCreatePinnedToCore(
        netkeyDistributionWorker,
        "NetkeyWorker",
        8192,  // 8KB stack for netkey distribution
        taskParams,
        1,     // Priority 1 (same as command poller)
        &m_netkeyWorkerHandle,
        1);    // CPU1 (application tasks)
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create netkey worker task");
        
        if (COMMAND_POLLER) {
            COMMAND_POLLER->moveToFailed(cmd, "TASK_CREATE_FAILED", 
                                       "Failed to create worker task for netkey distribution");
        }
        
        delete taskParams->completed;
        delete taskParams;
        return;
    }
    
    ESP_LOGI(TAG, "✅ Netkey distribution worker task created on CPU1");
    ESP_LOGI(TAG, "   Main loop continues normally while distribution happens in background");
}

void GatewayApp::updateProvisioningProgress() {
    if (!gatewayState.provisioningActive || !COMMAND_POLLER) {
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
    if (COMMAND_POLLER) {
        COMMAND_POLLER->updateProgress(
            gatewayState.provisioningCommandId,
            gatewayState.nodesDiscoveredDuringProvisioning,
            timeRemaining
        );
    }
    
    ESP_LOGD(TAG, "📊 Provisioning progress: %d nodes, %u ms remaining",
             gatewayState.nodesDiscoveredDuringProvisioning, timeRemaining);
}

// ===== NEW: Queue-based Firebase Upload Helpers =====

void GatewayApp::queueRoutingTableUpload(uint8_t priority) {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        ESP_LOGD(TAG, "Firebase not available for routing table upload");
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
        ESP_LOGI(TAG, "📡 Queuing EMPTY routing table to clear Firebase data");
    } else {
        ESP_LOGI(TAG, "📡 Queuing routing table (%d nodes, priority %d)", routingTable.size(), priority);
    }

#ifdef USE_CELLULAR
    // Cellular mode - direct upload (no queue system)
    ESP_LOGD(TAG, "Cellular mode - using direct upload");
    uploadRoutingTable();
#else
    // WiFi mode - use queue if available
    if (firebaseClient->isQueueRunning()) {
        bool success = firebaseClient->queueRoutingTable(routingTable, priority);
        if (success) {
            ESP_LOGD(TAG, "✅ Routing table queued successfully");
        } else {
            ESP_LOGW(TAG, "❌ Failed to queue routing table");
        }
    } else {
        // Fallback to direct upload
        ESP_LOGW(TAG, "Queue not available, using direct routing table upload");
        uploadRoutingTable();
    }
#endif
}

void GatewayApp::queueGatewaySensorDataUpload(uint8_t priority) {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        ESP_LOGD(TAG, "Firebase not available for gateway sensor upload");
        return;
    }

    // Generate gateway sensor data
    sensorData gatewaySensor = simulateGatewaySensorData();
    
    // Get signal strength (WiFi RSSI or Cellular RSSI)
#ifdef USE_CELLULAR
    int16_t signalRssi = cellularService ? cellularService->getSignalStrength() : -90;
#else
    int8_t signalRssi = wifiService ? wifiService->getRSSI() : -90;
#endif
    float gatewaySnr = 10.0f;  // Fixed SNR value for Gateway data
    
    char nodeIdStr[16];
    snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", gatewaySensor.nodeId);

    ESP_LOGI(TAG, "🏠 Queuing Gateway sensor data (priority %d)", priority);
    
    // // Log Gateway sensor data
    // switch (gatewaySensor.deviceType) {
    //     case DeviceType::SOIL_SENSOR:
    //         ESP_LOGI(TAG, "🌱 Gateway Soil - Counter: %u, Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, Batt: %.2fV",
    //                  gatewaySensor.counter, gatewaySensor.data.soil.soilMoisture, 
    //                  gatewaySensor.data.soil.soilTemperature, gatewaySensor.data.soil.pH,
    //                  gatewaySensor.battery);
    //         break;
    //     case DeviceType::ENV_SENSOR:
    //         ESP_LOGI(TAG, "🌡️ Gateway Env - Counter: %u, Temp: %.1f°C, Hum: %.1f%%, Batt: %.2fV",
    //                  gatewaySensor.counter, gatewaySensor.data.environment.temperature, 
    //                  gatewaySensor.data.environment.humidity, gatewaySensor.battery);
    //         break;
    //     default:
    //         ESP_LOGI(TAG, "📊 Gateway - Counter: %u, Type: %s, Batt: %.2fV",
    //                  gatewaySensor.counter, deviceTypeToString(gatewaySensor.deviceType), 
    //                  gatewaySensor.battery);
    //         break;
    // }

#ifdef USE_CELLULAR
    // Cellular mode - direct upload (no queue system)
    ESP_LOGD(TAG, "Cellular mode - using direct upload");
    uploadGatewaySensorData();
#else
    // WiFi mode - use queue if available
    if (firebaseClient->isQueueRunning()) {
        firebaseClient->queueSensorData(gatewaySensor, signalRssi, gatewaySnr, priority);
    } else {
        // Fallback to direct upload
        ESP_LOGW(TAG, "Queue not available, using direct gateway sensor upload");
        uploadGatewaySensorData();
    }
#endif
}

void GatewayApp::queueGatewayStatusUpload(uint8_t priority) {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        ESP_LOGD(TAG, "Firebase not available for gateway status upload");
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
    
    // Get signal strength (WiFi RSSI or Cellular RSSI)
#ifdef USE_CELLULAR
    int16_t signalRssi = cellularService ? cellularService->getSignalStrength() : -90;
#else
    int8_t signalRssi = WiFi.RSSI();
#endif
    
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t uptimeSeconds = millis() / 1000;
    
    ESP_LOGI(TAG, "📊 Queuing Gateway status (priority %d): nodes=%u, rx=%u, tx=%u, rssi=%d, heap=%u, uptime=%u",
             priority, connectedNodes, totalPacketsReceived, totalPacketsSent, signalRssi, freeHeap, uptimeSeconds);

#ifdef USE_CELLULAR
    // Cellular mode - direct upload (no queue system)
    ESP_LOGD(TAG, "Cellular mode - using direct upload");
    uploadGatewayStatusPeriodic();
#else
    // WiFi mode - use queue if available
    if (firebaseClient->isQueueRunning()) {
        bool success = firebaseClient->queueGatewayStatus(
            connectedNodes, totalPacketsReceived, totalPacketsSent,
            signalRssi, freeHeap, uptimeSeconds, priority
        );
        
        if (success) {
            ESP_LOGD(TAG, "✅ Gateway status queued successfully");
        } else {
            ESP_LOGW(TAG, "❌ Failed to queue gateway status");
        }
    } else {
        // Fallback to direct upload
        ESP_LOGW(TAG, "Queue not available, using direct gateway status upload");
        uploadGatewayStatusPeriodic();
    }
#endif
}

// ===== Netkey Distribution Worker Task (CPU1) =====
// Offloads heavy netkey broadcasting to CPU1 to avoid blocking CPU0 main loop
void GatewayApp::netkeyDistributionWorker(void* parameter) {
    NetkeyDistributionTask* task = static_cast<NetkeyDistributionTask*>(parameter);
    
    ESP_LOGI(TAG, "[NETKEY-WORKER] Started on CPU%d", xPortGetCoreID());
    ESP_LOGI(TAG, "[NETKEY-WORKER] Distributing netkey to all nodes in routing table");
    
    // Distribute netkey to all nodes in routing table
    bool distributionSuccess = false;
    size_t routingTableSize = RoutingTableService::routingTableSize();
    int nodesSuccessful = 0;
    
    if (routingTableSize > 0) {
        ESP_LOGI(TAG, "[NETKEY-WORKER] Found %d nodes, broadcasting netkey...", 
                 (int)routingTableSize);
        
        // Get all nodes from routing table
        NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
        if (nodes) {
            // HEAVY OPERATION: This can take 5-25 seconds depending on node count
            // Each node: ~100-500ms for LoRa broadcast + ACK
            distributionSuccess = NetkeyDistributionService::distributeNetkeyToAllNodes(
                task->config.networkKey, 
                task->config.authToken, 
                task->config.networkId, 
                task->config.keyVersion,
                nodes,
                routingTableSize
            );
            
            // Count successful nodes (approximation)
            nodesSuccessful = distributionSuccess ? routingTableSize : 0;
            
            delete[] nodes; // Clean up allocated memory
            
            ESP_LOGI(TAG, "[NETKEY-WORKER] Distribution: %s", 
                     distributionSuccess ? "SUCCESS" : "PARTIAL/FAILED");
        } else {
            ESP_LOGE(TAG, "[NETKEY-WORKER] Failed to get nodes from routing table");
            
#ifdef USE_CELLULAR
            if (GatewayApp::instance && GatewayApp::instance->cellularCommandPoller) {
                GatewayApp::instance->cellularCommandPoller->moveToFailed(
#else
            if (GatewayApp::instance && GatewayApp::instance->commandPoller) {
                GatewayApp::instance->commandPoller->moveToFailed(
#endif
                    task->cmd, "NO_NODES", 
                    "Failed to retrieve nodes from routing table"
                );
            }
            
            delete task->completed;
            delete task;
            vTaskDelete(NULL);
            return;
        }
    } else {
        ESP_LOGW(TAG, "[NETKEY-WORKER] Routing table is empty!");
    }
    
    // Mark command as completed
#ifdef USE_CELLULAR
    if (GatewayApp::instance && GatewayApp::instance->cellularCommandPoller) {
#else
    if (GatewayApp::instance && GatewayApp::instance->commandPoller) {
#endif
        String message = "Netkey distributed to ";
        message += String(nodesSuccessful);
        message += " of ";
        message += String((int)routingTableSize);
        message += " nodes";
        
        if (distributionSuccess) {
#ifdef USE_CELLULAR
            GatewayApp::instance->cellularCommandPoller->moveToCompleted(
#else
            GatewayApp::instance->commandPoller->moveToCompleted(
#endif
                task->cmd, "success", message
            );
        } else {
#ifdef USE_CELLULAR
            GatewayApp::instance->cellularCommandPoller->moveToFailed(
#else
            GatewayApp::instance->commandPoller->moveToFailed(
#endif
                task->cmd, "PARTIAL_FAILURE", message
            );
        }
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Netkey Distribution Completed (CPU1 Worker)               ║");
    ESP_LOGI(TAG, "║  Nodes: %d/%d successful                                   ║", 
             nodesSuccessful, (int)routingTableSize);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // Mark as completed and clean up
    *(task->completed) = true;
    delete task->completed;
    delete task;
    
    ESP_LOGI(TAG, "[NETKEY-WORKER] Task completed, deleting self");
    vTaskDelete(NULL);
}
