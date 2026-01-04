#include "node_app.h"
#include "node_offline_buffer.h"
#include "mesh_security_config.h"
#include "soil_sensor_service.h"
#include "sensor_task.h"
#include "../../utils/factory_reset.h"
#include "components/lora_mesh_manager/src/services/ProvisioningService.h"
#include "components/lora_mesh_manager/src/services/NetkeyDistributionService.h"
#include "components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "components/lora_mesh_manager/src/services/NVSStorageService.h"

#ifdef POWER_SAVE_NODE
    #include <WiFi.h>
#endif

#define LM_TAG "NodeApp"

// Forward declarations for static helper functions
static uint16_t findGatewayAddress();
static void saveRoutingTableToNVS();
static void loadRoutingTableFromNVS();
static void onRoutingTableChanged();  // SCENARIO 2: Callback for buffer sync on gateway detection

// Compute a 16-bit Node ID using the last 2 bytes of the WiFi MAC (STA MAC)
static uint16_t computeNodeIdFromWifiMac() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    // Use the last 2 bytes of the MAC as a 16-bit ID (big-endian)
    uint16_t id = ((uint16_t)mac[4] << 8) | (uint16_t)mac[5];
    return id;
}

// Static instance pointer for callbacks
static NodeApp* nodeAppInstance = nullptr;

// Static variables for simplified gateway tracking  
static bool wasGatewayAvailable = false;  // Track previous gateway state for logging
static uint16_t lastKnownGateway = 0;     // Cache last known gateway address for logging

// Static member initialization
NodeApp* NodeApp::instance = nullptr;
TaskHandle_t NodeApp::timeSyncTaskHandle = nullptr;

NodeApp::NodeApp() 
    : radio(LoraMesher::getInstance()), 
      dataCounter(0), 
      nodePacket(new dataPacket),
      provisionManager(nullptr),
      provisioningState(NODE_STATE_UNPROVISIONED),
      lastProvisionAttempt(0),
      provisionRetryCount(0),
      assignedAddress(0),
      hasValidNetworkKey(false),
      networkId(0),
      keyVersion(0) {
    
    instance = this;
    nodeAppInstance = this;
    memset(deviceUUID, 0, sizeof(deviceUUID));
    memset(networkKey, 0, sizeof(networkKey));
    memset(authToken, 0, sizeof(authToken));
}

NodeApp::~NodeApp() {
    delete nodePacket;
    delete provisionManager;
}

// Helper function to find gateway node - simplified: routing table only + broadcast fallback
static uint16_t findGatewayAddress() {
    // Find best gateway by role in routing table
    RouteNode* bestGateway = RoutingTableService::getBestNodeByRole(ROLE_GATEWAY);

    if (bestGateway) {
        // Ensure we can actually reach the gateway; RoutingTableService::getNextHop returns via (0 means unknown)
        uint16_t nextHop = RoutingTableService::getNextHop(bestGateway->networkNode.address);
        if (nextHop != 0) {
            uint16_t selected = bestGateway->networkNode.address;
            ESP_LOGI(LM_TAG, "🔍 Found gateway in routing table: 0x%04X (hops: %d) via next-hop 0x%04X",
                     selected, bestGateway->networkNode.metric, nextHop);
            return selected;
        } else {
            ESP_LOGW(LM_TAG, "Gateway 0x%04X has no known next-hop (stale entry)", bestGateway->networkNode.address);
        }
    }

    // No valid gateway found - fallback to broadcast
    ESP_LOGW(LM_TAG, "⚠️ No reachable gateway in routing table, using broadcast");
    return BROADCAST_ADDR;
}

void NodeApp::setup() {
    ESP_LOGI(LM_TAG, "=== LoRaMesh Node Application ===");
    uint16_t runtimeNodeId = computeNodeIdFromWifiMac();
    ESP_LOGI(LM_TAG, "Node ID (from WiFi MAC last 2 bytes): 0x%04X", runtimeNodeId);
    
    led_init();
    led_pattern_startup();
    
    // ===== RS485 SOIL SENSOR INITIALIZATION =====
    // Initialize RS485 soil sensor for soil parameter monitoring
    ESP_LOGI(LM_TAG, "Initializing RS485 Soil Sensor...");
    if (!SoilSensorService::initialize()) {
        ESP_LOGW(LM_TAG, "⚠️ Soil sensor initialization failed - will use simulated data");
        ESP_LOGW(LM_TAG, "   Check RS485 connections (TX=21, RX=20, DE=42)");
        ESP_LOGW(LM_TAG, "   Check baud rate: 9600 bps");
        ESP_LOGW(LM_TAG, "   Check sensor address: 0x01");
    } else {
        ESP_LOGI(LM_TAG, "✅ Soil sensor initialized successfully");
        
        // ===== START SENSOR TASK (CORE 0, 10-MIN INTERVAL) =====
        // Start dedicated FreeRTOS task on core 0 for periodic sensor reading
        if (SensorTaskManager::initialize()) {
            ESP_LOGI(LM_TAG, "✅ Sensor task initialized - will read every 10 minutes on core 0");
        } else {
            ESP_LOGW(LM_TAG, "⚠️ Failed to start sensor task - periodic reading disabled");
        }
        // ===== END SENSOR TASK STARTUP =====
    }
    // ===== END RS485 SOIL SENSOR INITIALIZATION =====
    
    // ===== BLE PROVISIONING CHECK =====
    // Create provision manager and check if node is provisioned
    provisionManager = new ProvisionManagerNode();
    
    if (!provisionManager->isProvisioned()) {
        ESP_LOGW(LM_TAG, "⚠️ Node not provisioned! Starting BLE provisioning mode");
        ESP_LOGI(LM_TAG, "- Mesh network: DISABLED (no network key)");
        ESP_LOGI(LM_TAG, "- BLE Advertising: ACTIVE (KAGRI-NODE-XXXX)");
        ESP_LOGI(LM_TAG, "");
        ESP_LOGI(LM_TAG, "To provision, use mobile app to connect via BLE");
        // led_pattern_error(); // Indicate provisioning required
        
        // Start BLE provisioning (blocking - don't initialize mesh)
        provisionManager->startProvisioningIfNeeded();
        
        ESP_LOGI(LM_TAG, "🔵 Node in BLE provisioning mode - waiting for mobile app...");
        return; // Don't continue setup until provisioned
    } else {
        ESP_LOGI(LM_TAG, "✅ Node is provisioned, continuing setup...");
        
        // Load provisioned node address
        uint16_t provisionedAddress = 0;
        if (provisionManager->getNodeAddress(provisionedAddress)) {
            assignedAddress = provisionedAddress;
            ESP_LOGI(LM_TAG, "  Provisioned Address: 0x%04X", assignedAddress);
        }
    }
    // ===== END BLE PROVISIONING CHECK =====

#ifdef POWER_SAVE_NODE
    // Basic power-saving: Node doesn't use WiFi after provisioning.
    // Keep this conservative (no deep sleep) to avoid changing the mesh protocol behavior.
    setCpuFrequencyMhz(80);
    WiFi.mode(WIFI_OFF);
    WiFi.disconnect(true, true);
    ESP_LOGI(LM_TAG, "[POWER_SAVE_NODE] CPU=80MHz, WiFi OFF");
#endif
    
    // NOTE: Do not clear gateway cache here. We'll restore routing table from NVS after
    // NVS is initialized so cached gateway validation can check restored routes.
    
    // Initialize mesh security first
    if (!initializeMeshSecurity()) {
        ESP_LOGE(LM_TAG, "Failed to initialize mesh security");
        // led_pattern_error();
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
    
    // Register callback for routing table changes (when nodes are removed due to timeout)
    // SCENARIO 2: Now also detects when Gateway becomes available and triggers buffer sync
    // This ensures routing table is saved to NVS when nodes become inactive
    RoutingTableService::setRoutingTableChangedCallback(onRoutingTableChanged);
    ESP_LOGI(LM_TAG, "Routing table change callback registered for NVS save + buffer sync detection");
    
    // Initialize Node offline buffer for sensor data when gateway unavailable
    if (NodeOfflineBuffer::initialize()) {
        uint16_t count, maxSize;
        uint8_t percentFull;
        NodeOfflineBuffer::getStats(count, maxSize, percentFull);
        ESP_LOGI(LM_TAG, "📦 Node offline buffer ready: %u/%u samples (%u%% full)", 
                 count, maxSize, percentFull);
    } else {
        ESP_LOGW(LM_TAG, "⚠️ Failed to initialize node offline buffer");
    }
    
    // Initialize ProvisioningService for Node
    if (!ProvisioningService::initialize()) {
        ESP_LOGE(LM_TAG, "Failed to initialize Provisioning Service");
        // led_pattern_error();
        return;
    }
    // Register callback to receive provisioning packets
    ProvisioningService::setNodePacketCallback(onProvisioningPacketReceived);
    ESP_LOGI(LM_TAG, "Provisioning Service initialized for node");
    
    // Initialize provisioning
    ESP_LOGI(LM_TAG, "Initializing node provisioning...");
    generateDeviceUUID();
    
    // Initialize Soil Sensor Service (RS485 Modbus RTU)
    if (!SoilSensorService::initialize()) {
        ESP_LOGE(LM_TAG, "⚠️ Failed to initialize Soil Sensor Service - sensor data will be unavailable");
        // Don't return - allow node to continue without sensor
    } else {
        ESP_LOGI(LM_TAG, "✅ Soil Sensor Service initialized - ready to read 7-parameter soil sensor");
    }
    
    // Start sensor reading task on core 0 (10-minute interval, non-blocking)
    if (!SensorTaskManager::initialize()) {
        ESP_LOGW(LM_TAG, "⚠️ Failed to start sensor task - will read sensor in main loop instead");
    } else {
        ESP_LOGI(LM_TAG, "✅ Sensor task started on core 0 - 10-minute read interval");
    }
    
    setupLoRaMesher();
    setupTimeSync();  // Initialize time sync service for node

    ESP_LOGI(LM_TAG, "Node setup complete. Send interval: %d ms", SEND_INTERVAL_MS);
    ESP_LOGI(LM_TAG, "Node provisioning state: %s", 
             (provisioningState == NODE_STATE_UNPROVISIONED) ? "UNPROVISIONED" :
             (provisioningState == NODE_STATE_PROVISIONING) ? "PROVISIONING" :
             (provisioningState == NODE_STATE_PROVISIONED) ? "PROVISIONED" : "FAILED");
    ESP_LOGI(LM_TAG, "Node status: HasValidNetworkKey=%s, AssignedAddress=0x%04X, LocalAddress=0x%04X", 
             hasValidNetworkKey ? "YES" : "NO", assignedAddress, runtimeNodeId);
    
    // Initialize factory reset button (IO13 - 5 second hold)
    FactoryReset::initialize();
    ESP_LOGI(LM_TAG, "🔧 Factory reset ready - Hold IO13 for 5 seconds to reset");
    
    ESP_LOGI(LM_TAG, "=== NODE SETUP COMPLETED - STARTING MAIN LOOP ===");
}

void NodeApp::loop() {
    // Check factory reset button (IO13 - 5 second hold)
    FactoryReset::loop();

    // Check if BLE provisioning just completed
    if (provisionManager && provisionManager->provisionSucceeded()) {
        ESP_LOGI(LM_TAG, "");
        ESP_LOGI(LM_TAG, "╔════════════════════════════════════════════════════════════╗");
        ESP_LOGI(LM_TAG, "║  🎉 NODE PROVISIONING SUCCESSFUL! 🎉                       ║");
        ESP_LOGI(LM_TAG, "╚════════════════════════════════════════════════════════════╝");
        ESP_LOGI(LM_TAG, "");
        
        // Show success LED pattern for 5 seconds
        // led_pattern_connected();
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Clear flag and restart
        provisionManager->clearProvisionSuccessFlag();
        
        if (provisionManager->needsRestart()) {
            ESP_LOGI(LM_TAG, "Restarting Node to apply provisioning...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
        }
        return;
    }
    
    // If not provisioned, stay in BLE provisioning mode
    if (!provisionManager || !provisionManager->isProvisioned()) {
        // BLE provisioning is active, just wait
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    
    // Send sensor data periodically if provisioned
    uint32_t currentTime = millis();

    
    if (provisioningState == NODE_STATE_PROVISIONED && hasValidNetworkKey) {
        static uint32_t lastDataSend = 0;
        static uint32_t lastRoutingSave = 0;
        static uint32_t setupTime = 0;
        static bool firstDataSent = false;  // Track if first data has been sent
        
        // Initialize setup time on first call
        if (setupTime == 0) {
            setupTime = currentTime;
        }
        
        // Send data 30 seconds after setup, then use normal interval
        bool shouldSendData = false;
        if (!firstDataSent && (currentTime - setupTime >= 30000)) {
            shouldSendData = true;
            firstDataSent = true;
            ESP_LOGI(LM_TAG, "📤 Sending initial sensor data (30s after setup)");
        } else if (firstDataSent && (currentTime - lastDataSend >= SEND_INTERVAL_MS)) {
            shouldSendData = true;
        }
        
        if (shouldSendData) {
            // Check if we're in provisioning mode (Fast Discovery)
            // Don't send sensor data during provisioning - only Hello packets
            uint8_t currentMode = radio.getCurrentHelloMode();
            if (currentMode == HELLO_MODE_FAST_DISCOVERY) {
                ESP_LOGD(LM_TAG, "Skipping sensor data send - in Fast Discovery Mode (provisioning)");
                lastDataSend = currentTime; // Update timestamp to avoid spam logs
                // Continue to next iteration - Hello packets will still be sent automatically
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                return;
            }
            
            // Get sensor data from task queue (non-blocking, 0ms timeout)
            // SensorTaskManager::getData returns latest reading if available
            // If no new data from sensor task, this will be false and we skip sensor send
            sensorData s;
            bool hasSensorData = SensorTaskManager::getData(s, 0);
            
            if (!hasSensorData) {
                // No new sensor data available yet from task
                // Use previous sensor data if available, or create error-filled struct
                s.error = true;  // Mark as error/stale data
                s.deviceType = DeviceType::SOIL_SENSOR;
                ESP_LOGD(LM_TAG, "No new sensor data available from task queue");
            }

            // Populate sensor metadata - use local LoRa address from LoraMesher
            uint16_t localAddr = LoraMesher::getInstance().getLocalAddress();
            s.counter = ++dataCounter;
            s.timestamp = currentTime;
            s.nodeId = assignedAddress ? assignedAddress : localAddr;

            // Log with appropriate icon based on device type
            switch (s.deviceType) {
                case DeviceType::SOIL_SENSOR:
                    if (hasSensorData && !s.error) {
                        ESP_LOGI(LM_TAG, "🌱 Sending soil sensor data #%d - Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, Batt: %.2fV",
                                 s.counter, s.data.soil.soilMoisture, s.data.soil.soilTemperature, s.data.soil.pH, s.battery);
                    } else {
                        ESP_LOGD(LM_TAG, "📤 Sending packet #%d (no new sensor data from task yet)",
                                 s.counter);
                    }
                    break;
                case DeviceType::ENV_SENSOR:
                    ESP_LOGI(LM_TAG, "🌡️ Sending environment sensor data #%d - Temp: %.1f°C, Hum: %.1f%%, Batt: %.2fV",
                             s.counter, s.data.environment.temperature, s.data.environment.humidity, s.battery);
                    break;
                default:
                    ESP_LOGI(LM_TAG, "📊 Sending sensor data #%d - Type: %d, Batt: %.2fV",
                             s.counter, (uint8_t)s.deviceType, s.battery);
                    break;
            }

            // Send sensorData struct to Gateway (use createPacketAndSend so secure wrapping is applied when enabled)
            // Find gateway node by role in routing table; fallback to broadcast
            {
                uint16_t dst = findGatewayAddress();
                
                // Always send data - either to specific gateway or broadcast
                if (dst == BROADCAST_ADDR) {
                    ESP_LOGI(LM_TAG, "📡 No gateway in routing table - sending via broadcast");
                    radio.createPacketAndSend<sensorData>(dst, &s, 1);
                } else {
                    ESP_LOGI(LM_TAG, "📤 Sending sensor data to gateway at address 0x%04X", dst);
                    radio.createPacketAndSend<sensorData>(dst, &s, 1);
                }
            }
            
            // led_pattern_message(); // Flash LED to indicate data sent/buffered

            lastDataSend = currentTime;
        }
        
        // NOTE: Routing table is now saved to NVS ONLY when changes occur (node added/removed)
        // via callback mechanism. Periodic save removed to reduce flash wear.
        // See: RoutingTableService::setRoutingTableChangedCallback() in setup()
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
    
    // Set device type - This node simulates a soil sensor
    data.deviceType = DeviceType::SOIL_SENSOR;
    
    // ===== TRY TO READ FROM QUEUE FIRST (populated by SensorTaskManager on core 0) =====
    if (SensorTaskManager::getData(data, 0)) {
        ESP_LOGI(LM_TAG, "✅ Real sensor data from queue: Moisture=%.1f%%, Temp=%.1f°C, pH=%.2f, EC=%.2f",
                 data.data.soil.soilMoisture, data.data.soil.soilTemperature,
                 data.data.soil.pH, data.data.soil.conductivity);
        
        // Use real timestamp if synchronized, otherwise use millis() as fallback
        if (TimeSyncService::isTimeSynced()) {
            data.timestamp = TimeSyncService::getCurrentTimestamp();
        } else {
            data.timestamp = millis() / 1000;
        }
        
        return data;
    }
    
    // ===== PRIORITY 2: READ DIRECTLY FROM SENSOR (if task not running) =====
    if (SoilSensorService::isConnected()) {
        ESP_LOGD(LM_TAG, "📡 Reading soil data directly from RS485 sensor (task not available)...");
        sensorData sensorResult = SoilSensorService::readData();  // Returns sensorData
        if (!sensorResult.error) {  // Check if read was successful
            data = sensorResult;
            ESP_LOGI(LM_TAG, "✅ Real sensor data (direct): Moisture=%.1f%%, Temp=%.1f°C, pH=%.2f, EC=%.2f",
                     data.data.soil.soilMoisture, data.data.soil.soilTemperature,
                     data.data.soil.pH, data.data.soil.conductivity);
            
            // Use real timestamp if synchronized, otherwise use millis() as fallback
            if (TimeSyncService::isTimeSynced()) {
                data.timestamp = TimeSyncService::getCurrentTimestamp();
            } else {
                data.timestamp = millis() / 1000;
            }
            
            return data;
        } else {
            ESP_LOGW(LM_TAG, "⚠️ Sensor read failed - marking as error (NO SIMULATION)");
            // Return error-filled struct, NOT simulation
            memset(&data.data.soil, 0, sizeof(data.data.soil));
            data.error = true;
            data.battery = 0.0f;
            data.timestamp = millis() / 1000;
            return data;
        }
    }
    
    // ===== NO FALLBACK: Return error struct (NO SIMULATION) =====
    ESP_LOGW(LM_TAG, "❌ Sensor not available - returning error struct (NO SIMULATION)");
    
    // Return error-filled struct (NO SIMULATION)
    memset(&data.data.soil, 0, sizeof(data.data.soil));
    data.error = true;
    data.battery = 0.0f;
    
    // Use real timestamp if synchronized, otherwise use millis() as fallback
    if (TimeSyncService::isTimeSynced()) {
        data.timestamp = TimeSyncService::getCurrentTimestamp();
    } else {
        data.timestamp = millis() / 1000;
    }
    
    return data;  // Return error struct
}

void NodeApp::setupLoRaMesher() {
    LoraMesher::LoraMesherConfig config;
    #if DEVICE_MODE == 1  // Node mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #elif DEVICE_MODE == 2  // Bridge mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #elif DEVICE_MODE == 3  // Node mode
        config.loraCs = LORA_CS;
        config.loraRst = LORA_RST;
        config.loraIrq = LORA_IRQ;
        config.loraIo1 = LORA_IO1;
        config.module = LORA_MODULE;
    #endif 
    
    radio.begin(config);
    
    // Use regular receive task - provisioning now handled by ProvisioningService
    TaskHandle_t receiveHandle = createReceiveTask("Node Receive Task");
    if (receiveHandle) {
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(LM_TAG, "LoRaMesher initialized with ProvisioningService integration");
        // led_pattern_connected();
    } else {
        ESP_LOGE(LM_TAG, "Failed to initialize LoRaMesher");
        // led_pattern_error();
    }
    
    // Create time sync receive task to handle time broadcasts from Gateway
    timeSyncTaskHandle = createTimeSyncReceiveTask();
    if (timeSyncTaskHandle) {
        ESP_LOGI(LM_TAG, "Time sync receive task created successfully (handle: %p)", timeSyncTaskHandle);
    } else {
        ESP_LOGW(LM_TAG, "Failed to create time sync receive task");
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
    // led_pattern_message(); // Flash LED to indicate key update
    
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
    
    // Find gateway by role in routing table; if none known send broadcast to reach any bridge
    {
        uint16_t dst = findGatewayAddress();
        ESP_LOGI(LM_TAG, "Sending provision request to 0x%04X", dst);
        radio.sendReliablePacket(dst, (uint8_t*)&request, sizeof(request));
    }
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
        // led_pattern_connected();
        
        // Save initial routing table after successful provisioning
        ESP_LOGI(LM_TAG, "Saving initial routing table after provisioning");
        saveRoutingTableToNVS();
    } else {
        ESP_LOGE(LM_TAG, "Failed to send provision complete");
        provisioningState = NODE_STATE_PROVISION_FAILED;
        // led_pattern_error();
    }
}

void NodeApp::handleProvisionReject(const ProvisionRejectPacket* reject) {
    ESP_LOGW(LM_TAG, "Provision request rejected. Reason: %d", reject->rejectReason);
    provisioningState = NODE_STATE_PROVISION_FAILED;
    // led_pattern_error();
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
    
    // Send provision complete to gateway if known, otherwise broadcast
    {
        uint16_t dst = findGatewayAddress();
        ESP_LOGI(LM_TAG, "Sending provision complete to 0x%04X", dst);
        radio.sendReliablePacket(dst, (uint8_t*)&complete, sizeof(complete));
    }
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

// Helper function to save current routing table to NVS for persistence
// FIX #3: Save only DIRECT routes (metric==1) or GATEWAY nodes to reduce NVS bloat
static void saveRoutingTableToNVS() {
    LM_LinkedList<RouteNode>* routingTable = LoraMesher::getInstance().routingTableListCopy();
    if (!routingTable || routingTable->getLength() == 0) {
        ESP_LOGD(LM_TAG, "No routing table to save to NVS");
        if (routingTable) delete routingTable;
        return;
    }
    
    // Convert routing table to RouteEntry array - FILTER for NVS save
    uint16_t entryCount = routingTable->getLength();
    RouteEntry* entries = new RouteEntry[entryCount];
    uint16_t validEntries = 0;
    uint16_t filteredCount = 0;
    
    for (int i = 0; i < entryCount; i++) {
        RouteNode* route = (*routingTable)[i];
        if (route && route->networkNode.address != 0) {
            // FIX #3: Only save direct neighbors (metric==1) or gateway nodes
            // Rationale: Indirect routes will be rediscovered after reboot
            bool isDirect = (route->networkNode.metric == 1);
            bool isGateway = (route->networkNode.role & ROLE_GATEWAY);
            
            if (isDirect || isGateway) {
                entries[validEntries] = {
                    .address = route->networkNode.address,
                    .via = route->via,
                    .metric = (uint8_t)route->networkNode.metric,
                    .role = route->networkNode.role,
                    .networkId = route->networkNode.networkId,
                    .lastSeen = (uint32_t)(esp_timer_get_time() / 1000000),
                    .isValid = true
                };
                validEntries++;
            } else {
                filteredCount++;
                ESP_LOGD(LM_TAG, "FIX #3: Filtered indirect route from NVS save: 0x%04X via 0x%04X (hops: %d)",
                         route->networkNode.address, route->via, route->networkNode.metric);
            }
        }
    }
    
    if (validEntries > 0) {
        if (NVSStorageService::saveRoutingTable(entries, validEntries)) {
            ESP_LOGI(LM_TAG, "Routing table saved to NVS: %u direct/gateway routes (filtered %u indirect)", 
                     validEntries, filteredCount);
        } else {
            ESP_LOGW(LM_TAG, "Failed to save routing table to NVS");
        }
    } else {
        ESP_LOGI(LM_TAG, "No direct/gateway routes to save to NVS (filtered %u indirect)", filteredCount);
    }
    
    delete[] entries;
    delete routingTable;
}

// Helper function to load routing table from NVS on startup
static void loadRoutingTableFromNVS() {
    ESP_LOGI(LM_TAG, "loadRoutingTableFromNVS: Starting...");
    
    const uint16_t maxEntries = 50; // Reasonable limit
    RouteEntry* entries = new RouteEntry[maxEntries];
    
    ESP_LOGI(LM_TAG, "loadRoutingTableFromNVS: Calling NVSStorageService::loadRoutingTable");
    uint16_t loadedCount = NVSStorageService::loadRoutingTable(entries, maxEntries);
    ESP_LOGI(LM_TAG, "loadRoutingTableFromNVS: Loaded %u entries", loadedCount);
    
    if (loadedCount > 0) {
        ESP_LOGI(LM_TAG, "Loaded %u routing entries from NVS", loadedCount);
        
        // Log loaded entries for debugging
        for (uint16_t i = 0; i < loadedCount; i++) {
            if (entries[i].isValid) {
                ESP_LOGD(LM_TAG, "  Entry %u: 0x%04X via 0x%04X (hops: %u, role: 0x%02X, netId: 0x%04X)", 
                        i, entries[i].address, entries[i].via, entries[i].metric, entries[i].role, entries[i].networkId);
            }
        }
        
        // CRITICAL FIX: Suspend callbacks during bulk restore to prevent premature NVS saves
        // Problem: Each processRoute() triggers save → overwrites remaining entries in NVS!
        RoutingTableService::suspendCallback();
        
        // Populate LoraMesher routing table with loaded entries
        for (uint16_t i = 0; i < loadedCount; i++) {
            if (entries[i].isValid) {
                // Populate the runtime routing table by processing each saved entry.
                // Construct a NetworkNode and let RoutingTableService handle insertion/update.
                NetworkNode node(entries[i].address, entries[i].metric, entries[i].role, entries[i].networkId);
                ESP_LOGD(LM_TAG, "Restoring route from NVS: addr=0x%04X via=0x%04X hops=%u role=0x%02X",
                         entries[i].address, entries[i].via, entries[i].metric, entries[i].role);
                RoutingTableService::processRoute(entries[i].via, &node);
            }
        }
        
        // Resume callbacks after all entries restored
        RoutingTableService::resumeCallback();
    } else {
        ESP_LOGD(LM_TAG, "No routing table entries found in NVS");
    }
    
    delete[] entries;
    ESP_LOGI(LM_TAG, "loadRoutingTableFromNVS: Completed");
}

/**
 * @brief SCENARIO 2: Callback triggered when routing table changes
 * 
 * Detects when Gateway becomes available after being offline and triggers
 * immediate sync of buffered sensor data from NodeOfflineBuffer.
 * 
 * This solves Scenario 2:
 * - Node A has buffered data (gateway unavailable)
 * - Routing table updates (Gateway C found)
 * - Immediately syncs ALL buffered data to gateway
 * 
 * Optimization: Only sync when gateway transitions from unavailable → available
 */
static void onRoutingTableChanged() {
    // Save routing table for persistence (original functionality)
    saveRoutingTableToNVS();
    
    // SCENARIO 2: Check if gateway just became available
    // Simplified gateway tracking - no complex state management needed
    // since we now send data regardless (unicast or broadcast)
    uint16_t currentGateway = findGatewayAddress();
    bool isGatewayAvailable = (currentGateway != BROADCAST_ADDR);
    
    // Log gateway status changes for monitoring
    if (!wasGatewayAvailable && isGatewayAvailable) {
        ESP_LOGI(LM_TAG, "✅ Gateway detected: 0x%04X (switching from broadcast to unicast)", currentGateway);
        wasGatewayAvailable = true;
        lastKnownGateway = currentGateway;
    }
    else if (wasGatewayAvailable && !isGatewayAvailable) {
        ESP_LOGW(LM_TAG, "⚠️ Gateway lost: 0x%04X (switching to broadcast mode)", lastKnownGateway);
        wasGatewayAvailable = false;
    }
    else if (isGatewayAvailable && currentGateway != lastKnownGateway) {
        ESP_LOGI(LM_TAG, "🔄 Gateway changed: 0x%04X → 0x%04X", lastKnownGateway, currentGateway);
        lastKnownGateway = currentGateway;
    }
}

void NodeApp::setupTimeSync() {
    ESP_LOGI(LM_TAG, "Setting up Time Synchronization (Node mode)...");
    
    // Initialize time sync service as Node (will receive time from Gateway)
    if (!TimeSyncService::initialize(false)) {
        ESP_LOGE(LM_TAG, "Failed to initialize Time Sync Service");
        return;
    }
    
    ESP_LOGI(LM_TAG, "✅ Time Sync Service initialized - waiting for Gateway broadcasts");
}

void NodeApp::handleTimeSyncPacket(AppPacket<TimeSyncService::TimeSyncPacket>* packet) {
    if (!packet || packet->payloadSize < sizeof(TimeSyncService::TimeSyncPacket)) {
        ESP_LOGW(LM_TAG, "Invalid time sync packet received");
        return;
    }
    
    TimeSyncService::TimeSyncPacket* timeSyncData = 
        reinterpret_cast<TimeSyncService::TimeSyncPacket*>(packet->payload);
    
    // Process time sync packet
    TimeSyncService::processTimeSyncPacket(*timeSyncData);
    
    ESP_LOGI(LM_TAG, "⏰ Time synchronized from Gateway 0x%04X: %u.%03u", 
             packet->src, timeSyncData->timestamp, timeSyncData->milliseconds);
    ESP_LOGI(LM_TAG, "Time sync age: %u seconds", TimeSyncService::getTimeSinceLastSync());
}

// Static task for receiving time sync packets
void NodeApp::processTimeSyncPackets(void* parameter) {
    ESP_LOGI(LM_TAG, "[TIMESYNC-TASK] Time sync receive task started");
    
    for (;;) {
        // Wait for notification from mesh receiver
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        
        ESP_LOGD(LM_TAG, "[TIMESYNC-TASK] Processing time sync packets...");
        
        while (NodeApp::instance && NodeApp::instance->radio.getReceivedQueueSize() > 0) {
            AppPacket<uint8_t>* packet = NodeApp::instance->radio.getNextAppPacket<uint8_t>();
            
            if (!packet) {
                ESP_LOGW(LM_TAG, "[TIMESYNC-TASK] Null packet received!");
                continue;
            }
            
            // Check if it's a time sync packet (8 bytes)
            if (packet->payloadSize == sizeof(TimeSyncService::TimeSyncPacket)) {
                AppPacket<TimeSyncService::TimeSyncPacket>* timeSyncPacket = 
                    reinterpret_cast<AppPacket<TimeSyncService::TimeSyncPacket>*>(packet);
                
                NodeApp::instance->handleTimeSyncPacket(timeSyncPacket);
            } else {
                ESP_LOGD(LM_TAG, "[TIMESYNC-TASK] Skipping non-time-sync packet (size: %d)", 
                         packet->payloadSize);
            }
            
            // Delete packet to free memory
            NodeApp::instance->radio.deletePacket(packet);
        }
    }
}

TaskHandle_t NodeApp::createTimeSyncReceiveTask() {
    TaskHandle_t taskHandle = NULL;
    
    ESP_LOGI(LM_TAG, "Creating time sync receive task...");
    
    // Create task with 3KB stack (time sync is lightweight)
    int res = xTaskCreate(
        processTimeSyncPackets,
        "Time Sync Task",
        3072,  // 3KB stack
        (void*) 1,
        2,     // Same priority as other receive tasks
        &taskHandle);
    
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Error: Time sync task creation failed: %d", res);
        return NULL;
    }
    
    ESP_LOGI(LM_TAG, "Time sync task created successfully, handle: %p", taskHandle);
    return taskHandle;
}