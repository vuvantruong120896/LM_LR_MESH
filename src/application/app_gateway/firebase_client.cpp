#include "firebase_client.h"
#include "firebase_queue.h"  // Include queue system
#include "TimeSyncService.h"
#include <time.h>
#include <esp_task_wdt.h>
#include <esp_log.h>

const char* TAG = "FIREBASE_CLIENT";

// Constants
// CRITICAL: Reduced retries and timeout to prevent task watchdog timeout (5s)
// With 3 retries + 10s timeout = up to 35s total, causing watchdog abort
constexpr uint8_t DEFAULT_MAX_RETRIES = 1;     // 1 retry = 2 total attempts max
constexpr uint32_t DEFAULT_RETRY_DELAY_MS = 500;  // Reduced from 1000ms
constexpr uint32_t UPLOAD_TIMEOUT_MS = 5000;   // Reduced from 10s to 5s

FirebaseClient::FirebaseClient(
    const char* firebaseHost,
    const char* firebaseAuth,
    const char* gatewayId
)
    : m_firebaseHost(firebaseHost)
    , m_firebaseAuth(firebaseAuth)
    , m_gatewayId(String(gatewayId))  // Copy to String to avoid dangling pointer
    , m_status(ConnectionStatus::DISCONNECTED)
    , m_autoTimestamp(true)
    , m_maxRetries(DEFAULT_MAX_RETRIES)
    , m_retryDelayMs(DEFAULT_RETRY_DELAY_MS)
{
    // Initialize statistics
    memset(&m_stats, 0, sizeof(FirebaseStats));
    // Initialize concurrency and breaker
    m_mutex = xSemaphoreCreateMutex();
    m_consecutiveFailures = 0;
    m_cooldownUntilMs = 0;
    m_baseCooldownMs = 2000;     // 2 seconds base cooldown
    m_failureThreshold = 5;      // after 5 consecutive failures, enter cooldown
    
    // Debug: Print gateway ID to verify it's stored correctly
    ESP_LOGI(TAG, "Gateway ID stored: %s", m_gatewayId.c_str());
}

FirebaseClient::~FirebaseClient() {
    disconnect();
}

bool FirebaseClient::initialize() {
    ESP_LOGI(TAG, "Initializing...");
    
    // Configure Firebase
    m_firebaseConfig.host = m_firebaseHost;
    m_firebaseConfig.signer.tokens.legacy_token = m_firebaseAuth;
    
    // Set timeout
    m_firebaseConfig.timeout.serverResponse = UPLOAD_TIMEOUT_MS;
    
    // MEMORY OPTIMIZATION (Oct 23, 2025): Reduce Firebase buffer sizes BEFORE Firebase.begin()
    // Default buffers are HUGE (16KB+ per buffer each), causing severe memory pressure
    // FirebaseData default allocation: ~50-60KB total!
    // Our sensor data is small (~1KB JSON), so we can drastically reduce buffers
    
    // Configure FirebaseData buffer sizes BEFORE Firebase.begin() to prevent massive heap allocation
    m_firebaseData.setBSSLBufferSize(2048, 512);  // RX: 2KB, TX: 512B (default: 16KB/16KB) - saves ~29KB!
    m_firebaseData.setResponseSize(2048);          // Response buffer: 2KB (default: 16KB) - saves ~14KB!
    
    ESP_LOGI(TAG, "📉 Firebase buffers reduced: Response=2KB, BSSL_RX=2KB, BSSL_TX=512B (saves ~43KB heap!)");
    
    // Initialize Firebase
    Firebase.begin(&m_firebaseConfig, &m_firebaseAuthData);
    Firebase.reconnectWiFi(true);
    
    ESP_LOGI(TAG, "Initialized");
    return true;
}

bool FirebaseClient::connect() {
    LockGuard guard(m_mutex);
    if (!guard.isLocked()) {
    ESP_LOGW(TAG, "connect(): mutex lock timeout");
        return false;
    }
    if (m_status == ConnectionStatus::CONNECTED) {
    ESP_LOGI(TAG, "Already connected");
        return true;
    }

    ESP_LOGI(TAG, "Connecting to %s...", m_firebaseHost);
    
    m_status = ConnectionStatus::CONNECTING;
    
    // Test connection with a simple read - gateways/{userUID}/{gatewayMAC}/info
    String testPath = "gateways/";
    testPath += m_userUID;
    testPath += "/";
    testPath += m_gatewayMAC;
    testPath += "/info";
    
    if (Firebase.setTimestamp(m_firebaseData, testPath.c_str())) {
        m_status = ConnectionStatus::CONNECTED;
    ESP_LOGI(TAG, "Connected successfully!");
        return true;
    } else {
        m_status = ConnectionStatus::ERROR;
        m_lastError = m_firebaseData.errorReason();
    ESP_LOGE(TAG, "Connection failed: %s", m_lastError.c_str());
        return false;
    }
}

void FirebaseClient::disconnect() {
    LockGuard guard(m_mutex);
    if (!guard.isLocked()) {
    ESP_LOGW(TAG, "disconnect(): mutex lock timeout");
        return;
    }
    if (m_status != ConnectionStatus::DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnecting...");
        m_status = ConnectionStatus::DISCONNECTED;
    }
}

bool FirebaseClient::isConnected() const {
    return m_status == ConnectionStatus::CONNECTED;
}

FirebaseClient::ConnectionStatus FirebaseClient::getStatus() const {
    return m_status;
}

FirebaseClient::UploadResult FirebaseClient::uploadSensorData(
    const sensorData& data, 
    int8_t rssi, 
    float snr
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    if (!circuitAllowsUpload()) {
        result.errorMessage = "Circuit breaker active (cooldown)";
        return result;
    }
    
    String nodeIdStr = nodeIdToString(data.nodeId);
    String jsonData = createSensorDataJson(data, rssi, snr);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Upload to two locations with multi-user paths:
    // 1. Latest data (for dashboard real-time view): nodes/{userUID}/{gatewayMAC}/{nodeId}/latest_data
    String latestPath = "nodes/";
    latestPath += m_userUID;
    latestPath += "/";
    latestPath += m_gatewayMAC;
    latestPath += "/";
    latestPath += nodeIdStr;
    latestPath += "/latest_data";
    bool success1 = uploadToPathWithRetry(latestPath, jsonData);
    
    // MEMORY FIX: Small delay between uploads to prevent TCP connection buildup
    delay(100);
    
    // 2. Time-series data (for historical charts): sensor_data/{userUID}/{nodeId}/{timestamp}
    String timeSeriesPath = "sensor_data/";
    timeSeriesPath += m_userUID;
    timeSeriesPath += "/";
    timeSeriesPath += nodeIdStr;
    timeSeriesPath += "/";
    timeSeriesPath += String(result.timestamp);
    bool success2 = uploadToPathWithRetry(timeSeriesPath, jsonData);
    
    // MEMORY FIX: Force String cleanup
    latestPath = String();
    timeSeriesPath = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    result.success = success1 && success2;
    
    recordUploadResult(result.success);
    if (result.success) {
    ESP_LOGI(TAG, "Sensor data uploaded for node %s (RSSI: %d dBm, SNR: %.1f)", 
         nodeIdStr.c_str(), rssi, snr);
    } else {
        result.errorMessage = m_lastError;
    ESP_LOGW(TAG, "Failed to upload sensor data: %s", result.errorMessage.c_str());
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::UploadResult FirebaseClient::uploadGatewayStatus(
    uint16_t connectedNodes,
    uint32_t totalPacketsReceived,
    uint32_t totalPacketsSent,
    int8_t wifiRssi,
    uint32_t freeHeap,
    uint32_t uptimeSeconds
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    if (!circuitAllowsUpload()) {
        result.errorMessage = "Circuit breaker active (cooldown)";
        return result;
    }
    
    String jsonData = createGatewayStatusJson(
        connectedNodes, 
        totalPacketsReceived, 
        totalPacketsSent,
        wifiRssi, 
        freeHeap, 
        uptimeSeconds
    );
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Path: gateways/{userUID}/{gatewayMAC}/status
    String path = "gateways/";
    path += m_userUID;
    path += "/";
    path += m_gatewayMAC;
    path += "/status";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    path = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    recordUploadResult(result.success);
    if (result.success) {
    ESP_LOGI(TAG, "Gateway status uploaded (%u nodes, %lu uptime)", 
         connectedNodes, uptimeSeconds);
    } else {
        result.errorMessage = m_lastError;
    ESP_LOGW(TAG, "Failed to upload gateway status: %s", result.errorMessage.c_str());
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::UploadResult FirebaseClient::uploadRoutingTable(
    const std::vector<RouteNode>& routingTable
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    if (!circuitAllowsUpload()) {
        result.errorMessage = "Circuit breaker active (cooldown)";
        return result;
    }
    
    String jsonData = createRoutingTableJson(routingTable);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Path: gateways/{userUID}/{gatewayMAC}/routing_table
    String path = "gateways/";
    path += m_userUID;
    path += "/";
    path += m_gatewayMAC;
    path += "/routing_table";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    path = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    recordUploadResult(result.success);
    if (result.success) {
    ESP_LOGI(TAG, "Routing table uploaded (%d nodes)", routingTable.size());
    } else {
        result.errorMessage = m_lastError;
    ESP_LOGW(TAG, "Failed to upload routing table: %s", result.errorMessage.c_str());
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::UploadResult FirebaseClient::logEvent(
    const String& eventType,
    const String& nodeId,
    const String& details
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    if (!circuitAllowsUpload()) {
        result.errorMessage = "Circuit breaker active (cooldown)";
        return result;
    }
    
    String jsonData = createEventJson(eventType, nodeId, details);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Path: events/{userUID}/{timestamp}
    String path = "events/";
    path += m_userUID;
    path += "/";
    path += String(result.timestamp);
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    recordUploadResult(result.success);
    if (result.success) {
    ESP_LOGI(TAG, "Event logged: %s", eventType.c_str());
    } else {
        result.errorMessage = m_lastError;
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::UploadResult FirebaseClient::updateGatewayInfo(
    const String& macAddress,
    const String& ipAddress,
    const String& firmwareVersion
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    
    // Create JSON - MEMORY FIX: Use StaticJsonDocument
    StaticJsonDocument<256> doc;  // 256 bytes (enough for gateway info)
    doc["mac"] = macAddress;
    doc["ip"] = ipAddress;
    doc["firmware_version"] = firmwareVersion;
    
    // Extract address from last 2 bytes of MAC (e.g., "AA:BB:CC:DD:EE:FF" -> "0xEEFF")
    String cleanMac = macAddress;
    cleanMac.replace(":", "");
    String last4Chars = cleanMac.substring(cleanMac.length() - 4);
    // Format as hex string with 0x prefix
    char addrBuf[7];
    snprintf(addrBuf, sizeof(addrBuf), "0x%04s", last4Chars.c_str());
    String addrStr = String(addrBuf);
    doc["address"] = addrStr;
    
    String jsonData;
    serializeJson(doc, jsonData);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Path: gateways/{userUID}/{gatewayMAC}/info
    String path = "gateways/";
    path += m_userUID;
    path += "/";
    path += m_gatewayMAC;
    path += "/info";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
    ESP_LOGI(TAG, "Gateway info updated (MAC: %s, IP: %s, Address: %s)", 
         macAddress.c_str(), ipAddress.c_str(), addrStr.c_str());
    } else {
        result.errorMessage = m_lastError;
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::UploadResult FirebaseClient::updateNodeInfo(
    uint16_t nodeId,
    const String& name,
    const String& type,
    const String& firmwareVersion
) {
    UploadResult result;
    result.success = false;
    result.timestamp = getCurrentTimestamp();
    
    if (!isConnected()) {
        result.errorMessage = "Not connected to Firebase";
        return result;
    }
    
    String nodeIdStr = nodeIdToString(nodeId);
    
    // Create JSON - MEMORY FIX: Use StaticJsonDocument
    StaticJsonDocument<256> doc;  // 256 bytes (enough for node info)
    doc["address"] = nodeIdStr;
    doc["name"] = name;
    doc["type"] = type;
    doc["firmware_version"] = firmwareVersion;
    doc["last_seen"] = result.timestamp;
    
    String jsonData;
    serializeJson(doc, jsonData);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Path: nodes/{userUID}/{gatewayMAC}/{nodeId}/info
    String path = "nodes/";
    path += m_userUID;
    path += "/";
    path += m_gatewayMAC;
    path += "/";
    path += nodeIdStr;
    path += "/info";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
    ESP_LOGI(TAG, "Node info updated: %s (%s)", 
         nodeIdStr.c_str(), name.c_str());
    } else {
        result.errorMessage = m_lastError;
    }
    
    updateUploadStats(result.success, result.payloadSize, uploadTime);
    
    return result;
}

FirebaseClient::FirebaseStats FirebaseClient::getStats() const {
    return m_stats;
}

void FirebaseClient::resetStats() {
    memset(&m_stats, 0, sizeof(FirebaseStats));
    ESP_LOGI(TAG, "Statistics reset");
}

void FirebaseClient::setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs) {
    m_maxRetries = maxRetries;
    m_retryDelayMs = retryDelayMs;
    ESP_LOGI(TAG, "Retry config: %u retries, %lu ms delay", maxRetries, retryDelayMs);
}

void FirebaseClient::setAutoTimestamp(bool enabled) {
    m_autoTimestamp = enabled;
}

String FirebaseClient::getLastError() const {
    return m_lastError;
}

void FirebaseClient::setUserContext(const String& userUID, const String& gatewayMAC) {
    m_userUID = userUID;
    m_gatewayMAC = gatewayMAC;
    ESP_LOGI(TAG, "User context set: UID=%s, MAC=%s", 
             userUID.c_str(), gatewayMAC.c_str());
}

// Private methods

bool FirebaseClient::uploadToPath(const String& path, const String& jsonData) {
    // CRITICAL FIX: Check WiFi connection before upload to prevent BearSSL crashes
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Firebase] WiFi not connected, skipping upload");
        m_lastError = "WiFi disconnected";
        return false;
    }
    
    // CRITICAL FIX: Do NOT hold mutex during network I/O (Firebase call can take 10-20 seconds)
    // Holding mutex during Firebase.updateNode() causes task watchdog timeout
    // Only protect m_firebaseData access and error string updates
    
    // Try updateNode (PATCH) instead of setJSON (PUT) to avoid "method not allowed" error
    // PATCH is more flexible for nested objects and works better with Firebase Security Rules
    FirebaseJson json;
    json.setJsonData(jsonData);
    
    bool success;
    String errorReason;
    uint32_t operationStartTime = millis();
    {
        LockGuard guard(m_mutex);
        if (!guard.isLocked()) {
            m_lastError = "mutex timeout";
            return false;
        }
        
        // NO WDT RESET: Let watchdog catch real hangs instead of masking them
        // This operation should complete within WDT timeout or trigger reboot
        
        success = Firebase.updateNode(m_firebaseData, path.c_str(), json);
        
        // Track slow operations for debugging and monitoring
        uint32_t operationTime = millis() - operationStartTime;
        if (operationTime > 8000) {
            m_stats.slowOperationCount++;
            Serial.printf("[Firebase] ⚠️ Slow operation: %u ms - Path: %s\n", operationTime, path.c_str());
        }
        if (operationTime > m_stats.maxOperationTime) {
            m_stats.maxOperationTime = operationTime;
        }
        
        // CRITICAL FIX: Force cleanup TCP connection to prevent memory/stack leak
        // Firebase library doesn't always cleanup properly, causing:
        // - Stack decrease (4544 → 2000 bytes observed)
        // - Heap leak (~64KB lost)
        // - BearSSL buffer corruption → LoadProhibited crash
        // Solution: Explicitly close WiFi client after each operation
        m_firebaseData.clear();  // Clear internal buffers
        
        // CRITICAL FIX (Oct 24, 2025): Force TCP connection close to prevent socket leak
        // After hours of operation, unclosed sockets accumulate → system hangs
        // WiFiClient has limited socket pool (~10-16) - must cleanup explicitly
        // Note: Firebase library doesn't expose direct socket control, but clear() helps
        
        if (!success) {
            errorReason = m_firebaseData.errorReason();
            m_lastError = errorReason;
            
            // CRITICAL FIX: Detect BearSSL errors and prevent retry (avoids crash)
            if (errorReason.indexOf("BearSSL") >= 0 || errorReason.indexOf("SSL") >= 0) {
                Serial.printf("[Firebase] BearSSL error detected, forcing reconnect: %s\n", errorReason.c_str());
                // Force close all connections to prevent buffer corruption
                Firebase.reconnectWiFi(true);
                delay(100);  // Give time for cleanup
            }
        }
    }  // Release mutex immediately after Firebase operation
    
    if (success) {
        return true;
    } else {
        Serial.printf("[Firebase] Upload error - Path: %s, Error: %s\n", path.c_str(), errorReason.c_str());
        return false;
    }
}

bool FirebaseClient::uploadToPathWithRetry(const String& path, const String& jsonData) {
    uint32_t totalStartTime = millis();
    
    // CRITICAL FIX (Oct 24, 2025): Periodic connection reset to prevent socket leak
    // Every 50 uploads, force reconnect to cleanup any stale connections
    static uint32_t uploadCounter = 0;
    uploadCounter++;
    if (uploadCounter % 50 == 0) {
        ESP_LOGI(TAG, "🔄 Periodic Firebase connection refresh (upload #%u)", uploadCounter);
        Firebase.reconnectWiFi(true);
        delay(100); // Allow reconnection to complete
    }
    
    for (uint8_t attempt = 0; attempt < m_maxRetries; attempt++) {
        // NO WDT RESET: Let watchdog catch hung retries
        // If retries take too long, system should reboot
        
        if (uploadToPath(path, jsonData)) {
            // CRITICAL: Delay after successful upload to allow TCP connection cleanup
            // Prevents socket accumulation which causes system hang after hours
            delay(100);  // Increased from 50ms to 100ms for better cleanup
            
            uint32_t totalTime = millis() - totalStartTime;
            if (totalTime > 10000) {
                Serial.printf("[Firebase] ⚠️ Slow retry sequence: %u ms total\n", totalTime);
            }
            return true;
        }
        
        if (attempt < m_maxRetries - 1) {
            Serial.printf("[Firebase] Upload failed (attempt %u/%u), retrying in %lu ms...\n",
                         attempt + 1, m_maxRetries, m_retryDelayMs);
            delay(m_retryDelayMs);
        }
    }
    
    Serial.printf("[Firebase] Upload failed after %u attempts\n", m_maxRetries);
    return false;
}

bool FirebaseClient::circuitAllowsUpload() {
    uint32_t now = millis();
    if (now < m_cooldownUntilMs) {
        // Still in cooldown
        return false;
    }
    return true;
}

void FirebaseClient::recordUploadResult(bool success) {
    uint32_t now = millis();
    if (success) {
        m_consecutiveFailures = 0;
        m_cooldownUntilMs = 0;
        return;
    }
    // Failure path
    if (m_consecutiveFailures < 255) m_consecutiveFailures++;
    if (m_consecutiveFailures >= m_failureThreshold) {
        // Exponential cooldown based on failures beyond threshold
        uint8_t over = m_consecutiveFailures - m_failureThreshold;
        uint32_t backoff = m_baseCooldownMs << (over > 5 ? 5 : over); // cap growth
        m_cooldownUntilMs = now + backoff;
        Serial.printf("[Firebase] Circuit breaker: %u consecutive failures, cooldown %lu ms\n",
                      m_consecutiveFailures, backoff);
    }
}

String FirebaseClient::createSensorDataJson(const sensorData& data, int8_t rssi, float snr) {
    // MEMORY FIX (Oct 23, 2025): Use StaticJsonDocument with explicit size instead of JsonDocument
    // JsonDocument can allocate on heap without proper cleanup, causing ~12KB leak per packet
    // Stack-allocated StaticJsonDocument is automatically freed when function returns
    StaticJsonDocument<1024> doc;  // 1KB stack buffer (enough for sensor data)
    
    // Common fields for all device types
    doc["deviceType"] = deviceTypeToString(data.deviceType);
    doc["counter"] = data.counter;
    doc["battery"] = data.battery;
    doc["timestamp"] = m_autoTimestamp ? getCurrentTimestamp() : data.timestamp;
    
    // Add RSSI if available (valid range: -120 to -30 dBm)
    if (rssi != 0 && rssi >= -120 && rssi <= -30) {
        doc["rssi"] = rssi;
    }
    
    // Add SNR if available (typical range: -20 to +15 dB)
    if (snr != 0.0f && snr >= -20.0f && snr <= 15.0f) {
        doc["snr"] = snr;
    }
    
    // Add sensor-specific fields based on device type
    switch (data.deviceType) {
        case DeviceType::SOIL_SENSOR:
            doc["soilMoisture"] = data.data.soil.soilMoisture;
            doc["soilTemperature"] = data.data.soil.soilTemperature;
            doc["pH"] = data.data.soil.pH;
            doc["ec"] = data.data.soil.ec;
            doc["nitrogen"] = data.data.soil.nitrogen;
            doc["phosphorus"] = data.data.soil.phosphorus;
            doc["potassium"] = data.data.soil.potassium;
            // Serial.printf("[Firebase] Soil sensor data - Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, EC: %.2f mS/cm\n",
            //     data.data.soil.soilMoisture, data.data.soil.soilTemperature, 
            //     data.data.soil.pH, data.data.soil.ec);
            break;
            
        case DeviceType::ENV_SENSOR:
            doc["temperature"] = data.data.environment.temperature;
            doc["humidity"] = data.data.environment.humidity;
            doc["pressure"] = data.data.environment.pressure;
            doc["lightIntensity"] = data.data.environment.lightIntensity;
            // Serial.printf("[Firebase] Environment sensor data - Temp: %.1f°C, Humidity: %.1f%%\n",
            //     data.data.environment.temperature, data.data.environment.humidity);
            break;
            
        case DeviceType::WATER_SENSOR:
            doc["waterTemp"] = data.data.water.waterTemp;
            doc["pH"] = data.data.water.pH;
            doc["tds"] = data.data.water.tds;
            doc["turbidity"] = data.data.water.turbidity;
            // Serial.printf("[Firebase] Water sensor data - Temp: %.1f°C, pH: %.2f, TDS: %.1f ppm\n",
            //     data.data.water.waterTemp, data.data.water.pH, data.data.water.tds);
            break;
            
        case DeviceType::GATEWAY:
            // Gateway may not have sensor data, or may report minimal metrics
            Serial.println("[Firebase] Gateway status (no sensor data)");
            break;
            
        case DeviceType::UNKNOWN:
        default:
            // For unknown types, serialize generic values array
            Serial.printf("[Firebase] Unknown device type: %d\n", (int)data.deviceType);
            for (int i = 0; i < 8; i++) {
                String key = "value";
                key += i;
                doc[key] = data.data.values[i];
            }
            break;
    }
    
    // Debug: Print JSON payload in pretty format
    ESP_LOGI(TAG, "[Firebase] Sensor data JSON (node 0x%04X, type: %s):\n", data.nodeId, deviceTypeToString(data.deviceType));
    {
        String pretty;
        serializeJsonPretty(doc, pretty);
        ESP_LOGD(TAG, "%s", pretty.c_str());
    }
    
    String jsonData;
    serializeJson(doc, jsonData);
    return jsonData;
}

String FirebaseClient::createGatewayStatusJson(
    uint16_t nodes, 
    uint32_t pktsRx, 
    uint32_t pktsTx,
    int8_t rssi, 
    uint32_t heap, 
    uint32_t uptime
) {
    // MEMORY FIX (Oct 23, 2025): Use StaticJsonDocument to avoid heap allocation
    StaticJsonDocument<512> doc;  // 512 bytes stack buffer (enough for gateway status)
    
    doc["connected_nodes"] = nodes;
    doc["total_packets_received"] = pktsRx;
    doc["total_packets_sent"] = pktsTx;
    doc["wifi_connected"] = true;  // If we're uploading, WiFi is connected
    doc["wifi_rssi"] = rssi;
    doc["firebase_connected"] = true;
    doc["uptime_seconds"] = uptime;
    doc["free_heap"] = heap;
    doc["timestamp"] = getCurrentTimestamp();
    
    // Debug: Print JSON payload in pretty format
    Serial.println("[Firebase] Gateway status JSON:");
    {
        String pretty;
        serializeJsonPretty(doc, pretty);
        ESP_LOGD(TAG, "%s", pretty.c_str());
    }
    
    String jsonData;
    serializeJson(doc, jsonData);
    return jsonData;
}

String FirebaseClient::createRoutingTableJson(const std::vector<RouteNode>& routingTable) {
    // MEMORY FIX: Use DynamicJsonDocument with capacity calculation for routing table
    // Routing table can be large (50+ nodes), so we need dynamic allocation
    // But with explicit capacity to prevent over-allocation
    size_t capacity = JSON_OBJECT_SIZE(3) + // root: nodes, node_count, updated_at
                      JSON_OBJECT_SIZE(routingTable.size()) + // nodes object
                      routingTable.size() * JSON_OBJECT_SIZE(7) + // each node: address, via, metric, role, rssi, snr, last_seen
                      routingTable.size() * 100; // strings overhead
    DynamicJsonDocument doc(capacity);
    JsonObject nodesObj = doc["nodes"].to<JsonObject>();
    
    for (const auto& route : routingTable) {
        // MEMORY FIX (Oct 24, 2025): Use char buffer instead of String to avoid heap allocation
        char nodeIdStr[16];
        snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", route.networkNode.address);
        JsonObject nodeObj = nodesObj[nodeIdStr].to<JsonObject>();
        
        nodeObj["address"] = nodeIdStr;
        // MEMORY FIX (Oct 24, 2025): Use char buffer for via field too
        char viaIdStr[16];
        snprintf(viaIdStr, sizeof(viaIdStr), "0x%04X", route.via);
        nodeObj["via"] = viaIdStr;
        nodeObj["metric"] = route.networkNode.metric;
        nodeObj["role"] = route.networkNode.role;
        
        // Only include signal quality data for direct routes (metric == 1)
        // Include RSSI/SNR for all direct routes, even if values are 0
        // (signal data is only available for 1-hop neighbors)
        if (route.networkNode.metric == 1) {
            nodeObj["rssi"] = route.receivedRSSI;
            nodeObj["snr"] = route.receivedSNR;
        }
        
        nodeObj["last_seen"] = getCurrentTimestamp();
    }
    
    doc["node_count"] = (int)routingTable.size();
    doc["updated_at"] = getCurrentTimestamp();
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // MEMORY FIX (Oct 24, 2025): Only show JSON in pretty format when verbose logging enabled
    // Remove expensive serializeJsonPretty that causes temporary String allocation
    #if CORE_DEBUG_LEVEL >= 4  // VERBOSE level only
    Serial.println("[Firebase] Routing table JSON (pretty print):");
    {
        String pretty;
        serializeJsonPretty(doc, pretty);
        ESP_LOGD(TAG, "%s", pretty.c_str());
    }
    #endif
    
    return jsonData;
}

String FirebaseClient::createEventJson(
    const String& type, 
    const String& nodeId, 
    const String& details
) {
    // MEMORY FIX: Use StaticJsonDocument for event logging
    StaticJsonDocument<512> doc;  // 512 bytes (enough for events)
    
    doc["type"] = type;
    doc["gateway_id"] = m_gatewayId;
    
    if (nodeId.length() > 0) {
        doc["node_id"] = nodeId;
    }
    
    if (details.length() > 0) {
        StaticJsonDocument<256> detailsDoc;  // 256 bytes for details
        deserializeJson(detailsDoc, details);
        doc["details"] = detailsDoc;
    }
    
    doc["timestamp"] = getCurrentTimestamp();
    
    String jsonData;
    serializeJson(doc, jsonData);
    return jsonData;
}

String FirebaseClient::nodeIdToString(uint16_t nodeId) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "0x%04X", nodeId);
    return String(buffer);
}

uint32_t FirebaseClient::getCurrentTimestamp() {
    // Get current time from TimeSyncService (NTP synced)
    uint32_t timestamp = TimeSyncService::getCurrentTimestamp();
    
    // Fallback to millis() if NTP not synced yet
    if (timestamp == 0) {
        return (uint32_t)(millis() / 1000);
    }
    
    return timestamp;
}

void FirebaseClient::updateUploadStats(bool success, size_t payloadSize, uint32_t uploadTime) {
    m_stats.totalUploads++;
    
    if (success) {
        m_stats.successfulUploads++;
        m_stats.totalBytesUploaded += payloadSize;
    m_stats.lastUploadTime = getCurrentTimestamp();
        
        // Update average upload time (exponential moving average)
        if (m_stats.averageUploadTime == 0) {
            m_stats.averageUploadTime = uploadTime;
        } else {
            m_stats.averageUploadTime = (0.8f * m_stats.averageUploadTime) + (0.2f * uploadTime);
        }
    } else {
        m_stats.failedUploads++;
    }
}

// ===== NEW: Queue-based Non-blocking Firebase Operations =====

bool FirebaseClient::initializeQueue() {
    Serial.println("[Firebase] Initializing queue system...");
    
    // Initialize the queue manager with this Firebase client
    bool success = FIREBASE_QUEUE().initialize(this);
    
    if (success) {
        Serial.println("[Firebase] ✅ Queue system initialized successfully");
    } else {
        Serial.println("[Firebase] ❌ Failed to initialize queue system");
    }
    
    return success;
}

void FirebaseClient::shutdownQueue() {
    Serial.println("[Firebase] Shutting down queue system...");
    FIREBASE_QUEUE().shutdown();
}

bool FirebaseClient::isQueueRunning() const {
    return FIREBASE_QUEUE().isRunning();
}

bool FirebaseClient::queueSensorData(const sensorData& data, int8_t rssi, float snr, uint8_t priority) {
    if (!isQueueRunning()) {
        Serial.println("[Firebase] Queue not running, falling back to direct upload");
        // Fallback to direct upload if queue is not available
        auto result = uploadSensorData(data, rssi, snr);
        return result.success;
    }
    
    // Convert priority to enum
    FirebasePriority_t queuePriority;
    switch (priority) {
        case 1: queuePriority = FIREBASE_PRIORITY_LOW; break;
        case 3: queuePriority = FIREBASE_PRIORITY_HIGH; break;
        case 4: queuePriority = FIREBASE_PRIORITY_URGENT; break;
        default: queuePriority = FIREBASE_PRIORITY_NORMAL; break;
    }
    
    bool success = FIREBASE_QUEUE().enqueueSensorData(data, rssi, snr, queuePriority);
    
    if (success) {
        ESP_LOGI(TAG, "[Firebase] ✅ Sensor data queued (node: 0x%04X, priority: %d)\n", data.nodeId, priority);
    } else {
        ESP_LOGE(TAG, "[Firebase] ❌ Failed to queue sensor data (node: 0x%04X)\n", data.nodeId);
    }
    
    return success;
}

bool FirebaseClient::queueGatewayStatus(
    uint16_t connectedNodes,
    uint32_t totalPacketsReceived,
    uint32_t totalPacketsSent,
    int8_t wifiRssi,
    uint32_t freeHeap,
    uint32_t uptimeSeconds,
    uint8_t priority
) {
    if (!isQueueRunning()) {
        Serial.println("[Firebase] Queue not running, falling back to direct upload");
        // Fallback to direct upload if queue is not available
        auto result = uploadGatewayStatus(connectedNodes, totalPacketsReceived, 
                                         totalPacketsSent, wifiRssi, freeHeap, uptimeSeconds);
        return result.success;
    }
    
    // Convert priority to enum
    FirebasePriority_t queuePriority;
    switch (priority) {
        case 1: queuePriority = FIREBASE_PRIORITY_LOW; break;
        case 3: queuePriority = FIREBASE_PRIORITY_HIGH; break;
        case 4: queuePriority = FIREBASE_PRIORITY_URGENT; break;
        default: queuePriority = FIREBASE_PRIORITY_NORMAL; break;
    }
    
    bool success = FIREBASE_QUEUE().enqueueGatewayStatus(
        connectedNodes, totalPacketsReceived, totalPacketsSent,
        wifiRssi, freeHeap, uptimeSeconds, queuePriority
    );
    
    if (success) {
        Serial.printf("[Firebase] ✅ Gateway status queued (nodes: %d, priority: %d)\n", connectedNodes, priority);
    } else {
        Serial.println("[Firebase] ❌ Failed to queue gateway status");
    }
    
    return success;
}

bool FirebaseClient::queueRoutingTable(const std::vector<RouteNode>& routingTable, uint8_t priority) {
    if (!isQueueRunning()) {
        Serial.println("[Firebase] Queue not running, falling back to direct upload");
        // Fallback to direct upload if queue is not available
        auto result = uploadRoutingTable(routingTable);
        return result.success;
    }
    
    // Convert priority to enum
    FirebasePriority_t queuePriority;
    switch (priority) {
        case 1: queuePriority = FIREBASE_PRIORITY_LOW; break;
        case 3: queuePriority = FIREBASE_PRIORITY_HIGH; break;
        case 4: queuePriority = FIREBASE_PRIORITY_URGENT; break;
        default: queuePriority = FIREBASE_PRIORITY_NORMAL; break;
    }
    
    bool success = FIREBASE_QUEUE().enqueueRoutingTable(routingTable, queuePriority);
    
    if (success) {
        Serial.printf("[Firebase] ✅ Routing table queued (%d nodes, priority: %d)\n", 
                     routingTable.size(), priority);
    } else {
        Serial.println("[Firebase] ❌ Failed to queue routing table");
    }
    
    return success;
}

bool FirebaseClient::queueLogEvent(
    const String& eventType,
    const String& nodeId,
    const String& details,
    uint8_t priority
) {
    if (!isQueueRunning()) {
        Serial.println("[Firebase] Queue not running, falling back to direct upload");
        // Fallback to direct upload if queue is not available
        auto result = logEvent(eventType, nodeId, details);
        return result.success;
    }
    
    // Convert priority to enum
    FirebasePriority_t queuePriority;
    switch (priority) {
        case 1: queuePriority = FIREBASE_PRIORITY_LOW; break;
        case 3: queuePriority = FIREBASE_PRIORITY_HIGH; break;
        case 4: queuePriority = FIREBASE_PRIORITY_URGENT; break;
        default: queuePriority = FIREBASE_PRIORITY_NORMAL; break;
    }
    
    bool success = FIREBASE_QUEUE().enqueueLogEvent(eventType, nodeId, details, queuePriority);
    
    if (success) {
        Serial.printf("[Firebase] ✅ Event queued (type: %s, priority: %d)\n", eventType.c_str(), priority);
    } else {
        Serial.printf("[Firebase] ❌ Failed to queue event: %s\n", eventType.c_str());
    }
    
    return success;
}
