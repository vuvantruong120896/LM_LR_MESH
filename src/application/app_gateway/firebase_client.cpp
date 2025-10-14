#include "firebase_client.h"
#include <time.h>

// Constants
constexpr uint8_t DEFAULT_MAX_RETRIES = 3;
constexpr uint32_t DEFAULT_RETRY_DELAY_MS = 1000;
constexpr uint32_t UPLOAD_TIMEOUT_MS = 10000;

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
    
    // Debug: Print gateway ID to verify it's stored correctly
    Serial.printf("[Firebase] Gateway ID stored: %s\n", m_gatewayId.c_str());
}

FirebaseClient::~FirebaseClient() {
    disconnect();
}

bool FirebaseClient::initialize() {
    Serial.println("[Firebase] Initializing...");
    
    // Configure Firebase
    m_firebaseConfig.host = m_firebaseHost;
    m_firebaseConfig.signer.tokens.legacy_token = m_firebaseAuth;
    
    // Set timeout
    m_firebaseConfig.timeout.serverResponse = UPLOAD_TIMEOUT_MS;
    
    // Initialize Firebase
    Firebase.begin(&m_firebaseConfig, &m_firebaseAuthData);
    Firebase.reconnectWiFi(true);
    
    Serial.println("[Firebase] Initialized");
    return true;
}

bool FirebaseClient::connect() {
    if (m_status == ConnectionStatus::CONNECTED) {
        Serial.println("[Firebase] Already connected");
        return true;
    }

    Serial.printf("[Firebase] Connecting to %s...\n", m_firebaseHost);
    
    m_status = ConnectionStatus::CONNECTING;
    
    // Test connection with a simple read
    String testPath = String("gateways/") + m_gatewayId + "/info/last_seen";
    
    if (Firebase.setTimestamp(m_firebaseData, testPath.c_str())) {
        m_status = ConnectionStatus::CONNECTED;
        Serial.println("[Firebase] Connected successfully!");
        return true;
    } else {
        m_status = ConnectionStatus::ERROR;
        m_lastError = m_firebaseData.errorReason();
        Serial.printf("[Firebase] Connection failed: %s\n", m_lastError.c_str());
        return false;
    }
}

void FirebaseClient::disconnect() {
    if (m_status != ConnectionStatus::DISCONNECTED) {
        Serial.println("[Firebase] Disconnecting...");
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
    
    String nodeIdStr = nodeIdToString(data.nodeId);
    String jsonData = createSensorDataJson(data, rssi, snr);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    // Upload to two locations:
    // 1. Latest data (for dashboard real-time view)
    String latestPath = String("nodes/") + nodeIdStr + "/latest_data";
    bool success1 = uploadToPathWithRetry(latestPath, jsonData);
    
    // MEMORY FIX: Small delay between uploads to prevent TCP connection buildup
    delay(100);
    
    // 2. Time-series data (for historical charts)
    String timeSeriesPath = String("sensor_data/") + nodeIdStr + "/" + String(result.timestamp);
    bool success2 = uploadToPathWithRetry(timeSeriesPath, jsonData);
    
    // MEMORY FIX: Force String cleanup
    latestPath = String();
    timeSeriesPath = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    result.success = success1 && success2;
    
    if (result.success) {
        Serial.printf("[Firebase] Sensor data uploaded for node %s (RSSI: %d dBm, SNR: %.1f)\n", 
                     nodeIdStr.c_str(), rssi, snr);
    } else {
        result.errorMessage = m_lastError;
        Serial.printf("[Firebase] Failed to upload sensor data: %s\n", result.errorMessage.c_str());
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
    
    String path = String("gateways/") + m_gatewayId + "/status";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    path = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
        Serial.printf("[Firebase] Gateway status uploaded (%u nodes, %lu uptime)\n", 
                     connectedNodes, uptimeSeconds);
    } else {
        result.errorMessage = m_lastError;
        Serial.printf("[Firebase] Failed to upload gateway status: %s\n", result.errorMessage.c_str());
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
    
    String jsonData = createRoutingTableJson(routingTable);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    String path = String("gateways/") + m_gatewayId + "/routing_table";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    path = String();
    jsonData = String();
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
        Serial.printf("[Firebase] Routing table uploaded (%d nodes)\n", routingTable.size());
    } else {
        result.errorMessage = m_lastError;
        Serial.printf("[Firebase] Failed to upload routing table: %s\n", result.errorMessage.c_str());
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
    
    String jsonData = createEventJson(eventType, nodeId, details);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    String path = String("events/") + String(result.timestamp);
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
        Serial.printf("[Firebase] Event logged: %s\n", eventType.c_str());
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
    
    // Create JSON
    JsonDocument doc;
    doc["mac"] = macAddress;
    doc["ip"] = ipAddress;
    doc["firmware_version"] = firmwareVersion;
    doc["created_at"] = result.timestamp;
    doc["last_seen"] = result.timestamp;
    
    String jsonData;
    serializeJson(doc, jsonData);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    String path = String("gateways/") + m_gatewayId + "/info";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
        Serial.printf("[Firebase] Gateway info updated (MAC: %s, IP: %s)\n", 
                     macAddress.c_str(), ipAddress.c_str());
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
    
    // Create JSON
    JsonDocument doc;
    doc["address"] = nodeIdStr;
    doc["name"] = name;
    doc["type"] = type;
    doc["firmware_version"] = firmwareVersion;
    doc["last_seen"] = result.timestamp;
    
    String jsonData;
    serializeJson(doc, jsonData);
    result.payloadSize = jsonData.length();
    
    uint32_t startTime = millis();
    
    String path = String("nodes/") + nodeIdStr + "/info";
    result.success = uploadToPathWithRetry(path, jsonData);
    
    uint32_t uploadTime = millis() - startTime;
    
    if (result.success) {
        Serial.printf("[Firebase] Node info updated: %s (%s)\n", 
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
    Serial.println("[Firebase] Statistics reset");
}

void FirebaseClient::setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs) {
    m_maxRetries = maxRetries;
    m_retryDelayMs = retryDelayMs;
    Serial.printf("[Firebase] Retry config: %u retries, %lu ms delay\n", maxRetries, retryDelayMs);
}

void FirebaseClient::setAutoTimestamp(bool enabled) {
    m_autoTimestamp = enabled;
}

String FirebaseClient::getLastError() const {
    return m_lastError;
}

// Private methods

bool FirebaseClient::uploadToPath(const String& path, const String& jsonData) {
    // Try updateNode (PATCH) instead of setJSON (PUT) to avoid "method not allowed" error
    // PATCH is more flexible for nested objects and works better with Firebase Security Rules
    FirebaseJson json;
    json.setJsonData(jsonData);
    
    bool success = Firebase.updateNode(m_firebaseData, path.c_str(), json);
    
    // CRITICAL FIX: Force cleanup TCP connection to prevent memory/stack leak
    // Firebase library doesn't always cleanup properly, causing:
    // - Stack decrease (4544 → 2000 bytes observed)
    // - Heap leak (~64KB lost)
    // Solution: Explicitly close WiFi client after each operation
    m_firebaseData.clear();  // Clear internal buffers
    
    if (success) {
        return true;
    } else {
        m_lastError = m_firebaseData.errorReason();
        Serial.printf("[Firebase] Upload error - Path: %s, Error: %s\n", path.c_str(), m_lastError.c_str());
        return false;
    }
}

bool FirebaseClient::uploadToPathWithRetry(const String& path, const String& jsonData) {
    for (uint8_t attempt = 0; attempt < m_maxRetries; attempt++) {
        if (uploadToPath(path, jsonData)) {
            // CRITICAL FIX: Small delay after successful upload to allow WiFi stack cleanup
            // Prevents TCP connection accumulation and stack/heap leaks
            delay(50);  // 50ms delay for WiFi client cleanup
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

String FirebaseClient::createSensorDataJson(const sensorData& data, int8_t rssi, float snr) {
    JsonDocument doc;
    
    doc["counter"] = data.counter;
    doc["temperature"] = data.temperature;
    doc["humidity"] = data.humidity;
    doc["battery"] = data.battery;
    doc["timestamp"] = m_autoTimestamp ? getCurrentTimestamp() : data.timestamp;
    
    if (rssi != 0) {
        doc["rssi"] = rssi;
    }
    
    if (snr != 0.0f) {
        doc["snr"] = snr;
    }
    
    // Debug: Print JSON payload in pretty format
    Serial.printf("[Firebase] Sensor data JSON (node 0x%04X):\n", data.nodeId);
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
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
    JsonDocument doc;
    
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
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
    String jsonData;
    serializeJson(doc, jsonData);
    return jsonData;
}

String FirebaseClient::createRoutingTableJson(const std::vector<RouteNode>& routingTable) {
    JsonDocument doc;
    JsonObject nodesObj = doc["nodes"].to<JsonObject>();
    
    for (const auto& route : routingTable) {
        String nodeIdStr = nodeIdToString(route.networkNode.address);
        JsonObject nodeObj = nodesObj[nodeIdStr].to<JsonObject>();
        
        nodeObj["address"] = nodeIdStr;
        nodeObj["via"] = nodeIdToString(route.via);
        nodeObj["metric"] = route.networkNode.metric;
        nodeObj["role"] = route.networkNode.role;
        
        // Only include signal quality data for direct routes (metric == 1)
        if (route.networkNode.metric == 1) {
            if (route.receivedRSSI != 0) {
                nodeObj["rssi"] = route.receivedRSSI;
            }
            
            if (route.receivedSNR != 0) {
                nodeObj["snr"] = route.receivedSNR;
            }
        }
        
        nodeObj["last_seen"] = getCurrentTimestamp();
    }
    
    doc["node_count"] = (int)routingTable.size();
    doc["updated_at"] = getCurrentTimestamp();
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Debug: Print JSON payload in pretty format for easy reading
    Serial.println("[Firebase] Routing table JSON (pretty print):");
    serializeJsonPretty(doc, Serial);
    Serial.println();  // Add newline after pretty print
    
    return jsonData;
}

String FirebaseClient::createEventJson(
    const String& type, 
    const String& nodeId, 
    const String& details
) {
    JsonDocument doc;
    
    doc["type"] = type;
    doc["gateway_id"] = m_gatewayId;
    
    if (nodeId.length() > 0) {
        doc["node_id"] = nodeId;
    }
    
    if (details.length() > 0) {
        JsonDocument detailsDoc;
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
    // Get current time (seconds since epoch)
    // Note: ESP32 needs NTP time sync for accurate timestamps
    return (uint32_t)(millis() / 1000);  // Fallback: relative time
    
    // For production, use NTP:
    // time_t now;
    // time(&now);
    // return (uint32_t)now;
}

void FirebaseClient::updateUploadStats(bool success, size_t payloadSize, uint32_t uploadTime) {
    m_stats.totalUploads++;
    
    if (success) {
        m_stats.successfulUploads++;
        m_stats.totalBytesUploaded += payloadSize;
        m_stats.lastUploadTime = millis();
        
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
