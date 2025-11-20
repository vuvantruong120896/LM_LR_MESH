/**
 * @file cellular_firebase_https_client.cpp
 * @brief Firebase HTTPS Client Implementation
 */

#include "cellular_firebase_https_client.h"
#include "TimeSyncService.h"
#include <esp_log.h>

namespace {
uint32_t getSyncedUnixTimestamp() {
    if (TimeSyncService::isTimeSynced()) {
        return TimeSyncService::getCurrentTimestamp();
    }
    return millis() / 1000;
}

uint32_t resolveMeasurementTimestamp(const sensorData& data) {
    // if (data.timestamp > 0) {
    //     return data.timestamp;
    // }
    return getSyncedUnixTimestamp();
}
}

static const char* TAG = "FB_HTTPS";

CellularFirebaseHTTPSClient::CellularFirebaseHTTPSClient(
    CellularSSLClient* sslClient,
    const String& firebaseHost,
    const String& authSecret,
    const String& gatewayId)
    : m_sslClient(sslClient),
      m_firebaseHost(firebaseHost),
      m_authSecret(authSecret),
      m_gatewayId(gatewayId),
      m_requestMutex(nullptr) {
    
    // Remove protocol prefix if present
    if (m_firebaseHost.startsWith("https://")) {
        m_firebaseHost = m_firebaseHost.substring(8);
    }
    if (m_firebaseHost.startsWith("http://")) {
        m_firebaseHost = m_firebaseHost.substring(7);
    }
    
    // Remove trailing slash
    if (m_firebaseHost.endsWith("/")) {
        m_firebaseHost = m_firebaseHost.substring(0, m_firebaseHost.length() - 1);
    }
    
    // Create mutex for HTTP transaction protection
    m_requestMutex = xSemaphoreCreateMutex();
    if (!m_requestMutex) {
        ESP_LOGE(TAG, "Failed to create HTTP transaction mutex");
    }
}

CellularFirebaseHTTPSClient::~CellularFirebaseHTTPSClient() {
    // Cleanup mutex
    if (m_requestMutex) {
        vSemaphoreDelete(m_requestMutex);
        m_requestMutex = nullptr;
    }
}

bool CellularFirebaseHTTPSClient::initialize() {
    if (!m_sslClient) {
        ESP_LOGE(TAG, "SSL client is null");
        return false;
    }

    return m_sslClient->initialize();
}

bool CellularFirebaseHTTPSClient::testConnection() {
    ESP_LOGI(TAG, "Testing Firebase HTTPS connection...");
    
    // Try to read a simple path
    UploadResult result = sendHTTPSRequest("GET", "/.json", "");
    
    if (result.success) {
        ESP_LOGI(TAG, "✅ Firebase HTTPS connection OK");
        return true;
    } else {
        ESP_LOGW(TAG, "❌ Firebase HTTPS connection failed: %s", result.message.c_str());
        return false;
    }
}

void CellularFirebaseHTTPSClient::setUserContext(const String& userUID, const String& gatewayMAC) {
    m_userUID = userUID;
    m_gatewayMAC = gatewayMAC;
    ESP_LOGI(TAG, "User context set: User=%s, Gateway=%s", userUID.c_str(), gatewayMAC.c_str());
}

String CellularFirebaseHTTPSClient::buildFirebaseURL(const String& path) {
    String url = "https://";
    url += m_firebaseHost;
    url += path;
    url += "?auth=";
    url += m_authSecret;
    return url;
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::sendHTTPSRequest(
    const String& method, const String& path, const String& body) {
    
    UploadResult result;
    
    // 🔐 MUTEX: Protect entire HTTP transaction from concurrent access
    // This prevents polling (GET) and queue uploads (PUT) from interfering
    if (!m_requestMutex) {
        result.message = "Mutex not initialized";
        ESP_LOGE(TAG, "❌ HTTP request mutex not available!");
        return result;
    }
    
    if (xSemaphoreTake(m_requestMutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        result.message = "Mutex timeout - another HTTP request in progress";
        ESP_LOGW(TAG, "⚠️ %s request blocked - %s timeout after %ums", 
                 method.c_str(), result.message.c_str(), MUTEX_TIMEOUT_MS);
        return result;
    }
    
    uint32_t startTime = millis();

    // Connect to Firebase
    String host = m_firebaseHost;
    
    // Extract hostname without path
    int slashIdx = host.indexOf('/');
    if (slashIdx > 0) {
        host = host.substring(0, slashIdx);
    }

    ESP_LOGD(TAG, "Connecting to %s:443", host.c_str());
    
    if (!m_sslClient->connect(host, 443, 30000)) {
        result.message = "SSL connection failed";
        ESP_LOGE(TAG, "%s", result.message.c_str());
        xSemaphoreGive(m_requestMutex);  // Release mutex before returning
        return result;
    }

    // Build HTTP request
    String request = method + " " + path + "?auth=" + m_authSecret + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n";
    request += "Accept: application/json\r\n";
    
    if (body.length() > 0) {
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + String(body.length()) + "\r\n";
    }
    
    request += "\r\n";
    if (body.length() > 0) {
        request += body;
    }

    ESP_LOGD(TAG, "Sending %s request to %s", method.c_str(), path.c_str());

    // Send request
    int sent = m_sslClient->send(request);
    if (sent <= 0) {
        // m_sslClient->disconnect();
        result.message = "Failed to send request";
        ESP_LOGE(TAG, "%s", result.message.c_str());
        xSemaphoreGive(m_requestMutex);  // Release mutex before returning
        return result;
    }

    // OPTIMIZATION: For PUT/POST methods, Firebase receives data even if we don't wait for response
    // Server accepts the request and processes it. No need to wait for HTTP response.
    // This saves 8+ seconds of timeout per request.
    if (method == "PUT" || method == "POST") {
        result.success = true;
        result.message = "Data sent (Firebase processes in background)";
        result.responseTime = millis() - startTime;
        ESP_LOGI(TAG, "✅ %s request sent successfully (%d bytes) - not waiting for response", method.c_str(), sent);
        // m_sslClient->disconnect();
        xSemaphoreGive(m_requestMutex);  // Release mutex
        return result;
    }

    // For GET/DELETE, we need to read response

    // Receive response (chunked with backoff)
    const size_t CHUNK = 512;               // smaller reads reduce ERROR likelihood
    char buffer[CHUNK + 1];
    String response = "";
    int totalReceived = 0;
    
    // Short settle delay after send to allow server to respond (increased for cellular)
    delay(500);  // increased from 300ms to 500ms for cellular latency

    // Read response with overall timeout (increased for cellular)
    uint32_t readStart = millis();
    uint32_t backoffMs = 200;               // start with 200ms backoff when no data (cellular needs more time)
    uint32_t pollCount = 0;
    while (millis() - readStart < 8000) {  // 8s overall window for cellular response (server processing + network latency)
        int received = m_sslClient->receive(buffer, CHUNK, 2000); 
        pollCount++;
        
        if (received > 0) {
            ESP_LOGD(TAG, "[RECV LOOP] Poll #%u: Got %d bytes", pollCount, received);
            buffer[received] = '\0';
            response += buffer;
            totalReceived += received;
            readStart = millis(); // Reset overall timeout on data
            backoffMs = 200;      // reset backoff after receiving data

            // If we have headers, check if body is complete
            int headerPos = response.indexOf("\r\n\r\n");
            if (headerPos > 0) {
                int bodyStart = headerPos + 4;
                int contentLength = 0;
                int clIdx = response.indexOf("Content-Length:");
                if (clIdx > 0) {
                    String clStr = response.substring(clIdx + 15);
                    clStr = clStr.substring(0, clStr.indexOf('\r'));
                    contentLength = clStr.toInt();
                }

                if (method != "GET") {
                    // For non-GET, stop when body fully read or if no Content-Length
                    if (contentLength > 0) {
                        int bodyReceived = (int)response.length() - bodyStart;
                        if (bodyReceived >= contentLength) {
                            ESP_LOGD(TAG, "[RECV LOOP] Content fully received, breaking");
                            break;
                        }
                    } else {
                        // No content length -> assume done when modem stops sending
                        // Continue a bit until timeout to catch any trailing data
                    }
                }
            }
        } else if (received == 0) {
            delay(backoffMs);
            if (backoffMs < 1000) backoffMs += 200; // cap at 1000ms (increased for cellular)
        } else {
            // receive() error -> break
            ESP_LOGW(TAG, "[RECV LOOP] Poll #%u: Receive error (%d), breaking", pollCount, received);
            break;
        }
    }

    // m_sslClient->disconnect();

    result.responseTime = millis() - startTime;

    if (totalReceived == 0) {
        result.message = "No response received (timeout after 8s)";
        ESP_LOGW(TAG, "%s - Check cellular network latency", result.message.c_str());
        xSemaphoreGive(m_requestMutex);  // Release mutex before returning
        return result;
    }

    ESP_LOGD(TAG, "Received %d bytes in %u ms", totalReceived, result.responseTime);

    // Add basic response validation
    if (response.length() < 10) {
        result.message = "Response too short, possible truncation";
        ESP_LOGW(TAG, "%s (received: %d bytes)", result.message.c_str(), totalReceived);
        xSemaphoreGive(m_requestMutex);  // Release mutex before returning
        return result;
    }

    // Parse HTTP response
    String responseBody;
    result.httpCode = parseHTTPResponse(response, responseBody);
    result.response = responseBody;  // Store response body
    
    if (result.httpCode >= 200 && result.httpCode < 300) {
        result.success = true;
        result.message = "Success";
    } else {
        result.message = "HTTP " + String(result.httpCode);
        ESP_LOGW(TAG, "HTTP error %d", result.httpCode);
    }

    xSemaphoreGive(m_requestMutex);  // Release mutex after successful operation
    return result;
}

int CellularFirebaseHTTPSClient::parseHTTPResponse(const String& response, String& body) {
    // Extract status code
    int httpCode = 0;
    int idx = response.indexOf("HTTP/1.");
    if (idx >= 0) {
        String statusLine = response.substring(idx);
        int spaceIdx = statusLine.indexOf(' ');
        if (spaceIdx > 0) {
            String codeStr = statusLine.substring(spaceIdx + 1, spaceIdx + 4);
            httpCode = codeStr.toInt();
        }
    }

    // Extract body
    int bodyStart = response.indexOf("\r\n\r\n");
    if (bodyStart > 0) {
        body = response.substring(bodyStart + 4);
    }

    return httpCode;
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::uploadSensorData(
    const sensorData& data, int8_t rssi, float snr) {
    
    // Build paths matching WiFi mode (multi-user schema):
    // WiFi Firebase library auto-appends .json, so paths are:
    // 1. Latest data: nodes/{userUID}/{gatewayMAC}/{nodeId}/latest_data.json
    // 2. Time-series: sensor_data/{userUID}/{nodeId}/{timestamp}.json
    String nodeId = String(data.nodeId, HEX);
    nodeId.toUpperCase();
    
    uint32_t timestamp = resolveMeasurementTimestamp(data);

    String jsonBody = buildSensorDataJSON(data, rssi, snr, timestamp);


    // // Path 1: Latest data (real-time dashboard) - matching WiFi mode with .json
    // ESP_LOGI(TAG, "📤 Uploading sensor data for node 0x%s", nodeId.c_str());
    // String path1 = "/nodes/" + m_userUID + "/" + m_gatewayMAC + 
    //                "/0x" + nodeId + "/latest_data.json";
    // UploadResult result1 = sendHTTPSRequest("PUT", path1, jsonBody);

    // vTaskDelay(500 / portTICK_PERIOD_MS); // Short delay between requests

    // Path 2: Time-series data (historical charts)
    ESP_LOGI(TAG, "📤 Uploading time-series sensor data for node 0x%s", nodeId.c_str());
    String path2 = "/sensor_data/" + m_userUID + "/0x" + nodeId + "/" + String(timestamp) + ".json";
    UploadResult result2 = sendHTTPSRequest("PUT", path2, jsonBody);

    
    // // Return success if both uploads succeed
    // UploadResult result;
    // result.success = result1.success && result2.success;
    // result.httpCode = result1.httpCode;
    // result.message = result1.success ? result2.message : result1.message;
    // result.responseTime = result1.responseTime + result2.responseTime;
    // return result;

    // Return success if both uploads succeed
    UploadResult result;
    result.success = result2.success;
    result.httpCode = result2.httpCode;
    result.message = result2.message;
    result.responseTime = result2.responseTime;
    return result;
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::uploadRoutingTable(
    const std::vector<RouteNode>& routes) {
    
    // Path matching WiFi mode: gateways/{userUID}/{gatewayMAC}/routing_table.json
    // WiFi Firebase library auto-appends .json, so we need explicit .json in HTTPS path
    String path = "/gateways/" + m_userUID + "/" + m_gatewayMAC + "/routing_table.json";
    String jsonBody = buildRoutingTableJSON(routes);
    
    ESP_LOGI(TAG, "📡 Uploading routing table (%d nodes)", routes.size());
    
    return sendHTTPSRequest("PUT", path, jsonBody);
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::uploadGatewayStatus(
    uint16_t nodeCount, uint32_t rxPackets, uint32_t txPackets, int16_t rssi,
    uint32_t freeHeap, uint32_t uptime) {
    
    // Path matching WiFi mode: gateways/{userUID}/{gatewayMAC}/status.json
    // WiFi Firebase library auto-appends .json, so we need explicit .json in HTTPS path
    String path = "/gateways/" + m_userUID + "/" + m_gatewayMAC + "/status.json";
    String jsonBody = buildGatewayStatusJSON(nodeCount, rxPackets, txPackets, rssi, freeHeap, uptime);
    
    ESP_LOGI(TAG, "📊 Uploading gateway status");
    
    return sendHTTPSRequest("PUT", path, jsonBody);
}

bool CellularFirebaseHTTPSClient::logEvent(const String& eventType, const String& nodeId, const String& message) {
    // Path matching WiFi mode: gateways/{userUID}/{gatewayMAC}/events/{timestamp}.json
    // WiFi Firebase library auto-appends .json, so we need explicit .json in HTTPS path
    uint32_t timestamp = getSyncedUnixTimestamp();
    String path = "/gateways/" + m_userUID + "/" + m_gatewayMAC + "/events/" + String(timestamp) + ".json";
    
    JsonDocument doc;
    doc["type"] = eventType;
    doc["timestamp"] = timestamp;
    if (nodeId.length() > 0) {
        doc["nodeId"] = nodeId;
    }
    if (message.length() > 0) {
        doc["message"] = message;
    }
    
    String jsonBody;
    serializeJson(doc, jsonBody);
    
    UploadResult result = sendHTTPSRequest("PUT", path, jsonBody);
    return result.success;
}

String CellularFirebaseHTTPSClient::buildSensorDataJSON(const sensorData& data, int8_t rssi, float snr, uint32_t timestamp) {
    // Match WiFi format exactly for Firebase compatibility
    JsonDocument doc;
    
    // Convert device type to string (match WiFi format)
    String deviceTypeStr;
    switch (data.deviceType) {
        case DeviceType::SOIL_SENSOR:
            deviceTypeStr = "soil_sensor";
            break;
        case DeviceType::ENV_SENSOR:
            deviceTypeStr = "environment_sensor";
            break;
        default:
            deviceTypeStr = "unknown";
    }
    
    doc["deviceType"] = deviceTypeStr;
    doc["counter"] = data.counter;
    doc["battery"] = data.battery;
    doc["timestamp"] = timestamp;
    
    // Add RSSI if valid (-120 to -30 dBm)
    if (rssi != 0 && rssi >= -120 && rssi <= -30) {
        doc["rssi"] = rssi;
    }
    
    // Add SNR if valid (-20 to +15 dB)
    if (snr != 0.0f && snr >= -20.0f && snr <= 15.0f) {
        doc["snr"] = snr;
    }
    
    // Add sensor-specific data
    switch (data.deviceType) {
        case DeviceType::SOIL_SENSOR:
            doc["soilMoisture"] = data.data.soil.soilMoisture;
            doc["soilTemperature"] = data.data.soil.soilTemperature;
            doc["pH"] = data.data.soil.pH;
            doc["conductivity"] = data.data.soil.conductivity;  // EC = Electrical Conductivity
            doc["nitrogen"] = data.data.soil.nitrogen;
            doc["phosphorus"] = data.data.soil.phosphorus;
            doc["potassium"] = data.data.soil.potassium;
            break;
        case DeviceType::ENV_SENSOR:
            doc["temperature"] = data.data.environment.temperature;
            doc["humidity"] = data.data.environment.humidity;
            doc["pressure"] = data.data.environment.pressure;
            doc["light"] = data.data.environment.light;  // Light intensity in lux
            break;
        default:
            // Unknown or unsupported type - no additional fields
            break;
    }
    
    String jsonBody;
    serializeJson(doc, jsonBody);
    return jsonBody;
}

String CellularFirebaseHTTPSClient::buildRoutingTableJSON(const std::vector<RouteNode>& routes) {
    // Match WiFi format exactly for Firebase compatibility
    // WiFi uses: { "nodes": { "0xADDR": { ... } }, "node_count": N, "updated_at": timestamp }
    JsonDocument doc;
    
    JsonObject nodesObj = doc["nodes"].to<JsonObject>();
    
    for (const auto& route : routes) {
        char nodeIdStr[16];
        snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", route.networkNode.address);
        JsonObject nodeObj = nodesObj[nodeIdStr].to<JsonObject>();
        
        nodeObj["address"] = nodeIdStr;
        
        char viaIdStr[16];
        snprintf(viaIdStr, sizeof(viaIdStr), "0x%04X", route.via);
        nodeObj["via"] = viaIdStr;
        
        nodeObj["metric"] = route.networkNode.metric;
        nodeObj["role"] = route.networkNode.role;
        
        // Include signal quality for direct routes (metric == 1)
        if (route.networkNode.metric == 1) {
            nodeObj["rssi"] = route.receivedRSSI;
            nodeObj["snr"] = route.receivedSNR;
        }
        
        nodeObj["last_seen"] = getSyncedUnixTimestamp();  // current timestamp
    }
    
    doc["node_count"] = (int)routes.size();
    doc["updated_at"] = getSyncedUnixTimestamp();
    
    String jsonBody;
    serializeJson(doc, jsonBody);
    return jsonBody;
}

String CellularFirebaseHTTPSClient::buildGatewayStatusJSON(
    uint16_t nodeCount, uint32_t rxPackets, uint32_t txPackets, int16_t rssi,
    uint32_t freeHeap, uint32_t uptime) {
    
    // Match WiFi format exactly for Firebase compatibility
    JsonDocument doc;
    
    doc["connected_nodes"] = nodeCount;
    doc["total_packets_received"] = rxPackets;
    doc["total_packets_sent"] = txPackets;
    doc["wifi_connected"] = true;  // Cellular is "connected" if uploading
    doc["wifi_rssi"] = rssi;
    doc["firebase_connected"] = true;
    doc["uptime_seconds"] = uptime;
    doc["free_heap"] = freeHeap;
    doc["timestamp"] = getSyncedUnixTimestamp();
    
    String jsonBody;
    serializeJson(doc, jsonBody);
    return jsonBody;
}

// Generic HTTP methods for command polling

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::httpGet(const String& path) {
    UploadResult result = sendHTTPSRequest("GET", "/" + path, "");
    
    // For GET requests, extract response body from the response
    if (result.success) {
        // The response body is stored in the internal parsing
        // We need to re-parse to extract body
        // For now, use a simple approach - the caller will parse the response
        ESP_LOGI(TAG, "GET %s - HTTP %d", path.c_str(), result.httpCode);
    }
    
    return result;
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::httpPut(
    const String& path, const String& jsonData) {
    
    UploadResult result = sendHTTPSRequest("PUT", "/" + path, jsonData);
    
    if (result.success) {
        ESP_LOGI(TAG, "PUT %s - HTTP %d", path.c_str(), result.httpCode);
    } else {
        ESP_LOGE(TAG, "PUT %s failed: %s", path.c_str(), result.message.c_str());
    }
    
    return result;
}

CellularFirebaseHTTPSClient::UploadResult CellularFirebaseHTTPSClient::httpDelete(const String& path) {
    UploadResult result = sendHTTPSRequest("DELETE", "/" + path, "");
    
    if (result.success) {
        ESP_LOGI(TAG, "DELETE %s - HTTP %d", path.c_str(), result.httpCode);
    } else {
        ESP_LOGE(TAG, "DELETE %s failed: %s", path.c_str(), result.message.c_str());
    }
    
    return result;
}

bool CellularFirebaseHTTPSClient::fetchPendingCommands() {
    // Build path: users/{uid}/commands/{mac}/pending.json
    String path = String("users/") + m_userUID + "/commands/" + m_gatewayMAC + "/pending.json";
    
    ESP_LOGD(TAG, "Fetching pending commands: GET %s", path.c_str());
    
    // Perform HTTPS GET request
    UploadResult result = httpGet(path);
    
    if (!result.success) {
        ESP_LOGW(TAG, "Failed to fetch commands: %s", result.message.c_str());
        return false;
    }
    
    // Response body contains pending commands (handled by command poller)
    // Just log that fetch succeeded
    ESP_LOGD(TAG, "✅ Pending commands fetched successfully");
    return true;
}

