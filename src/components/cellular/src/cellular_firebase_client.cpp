/**
 * @file cellular_firebase_client.cpp
 * @brief Firebase Realtime Database Client Implementation
 */

#include "cellular_firebase_client.h"
#include <esp_log.h>
#include <time.h>

const char* CellularFirebaseClient::TAG = "CELLULAR_FIREBASE";

CellularFirebaseClient::CellularFirebaseClient(CellularHTTPClient* httpClient,
                                             const String& firebaseHost,
                                             const String& authSecret,
                                             const String& gatewayId)
    : m_httpClient(httpClient)
    , m_firebaseHost(firebaseHost)
    , m_authSecret(authSecret)
    , m_gatewayId(gatewayId)
    , m_autoTimestamp(true)
    , m_maxRetries(3)
    , m_retryDelayMs(1000)
{
    memset(&m_stats, 0, sizeof(Stats));
}

bool CellularFirebaseClient::initialize() {
    ESP_LOGI(TAG, "Initializing Firebase client...");
    ESP_LOGI(TAG, "  Host: %s", m_firebaseHost.c_str());
    ESP_LOGI(TAG, "  Gateway: %s", m_gatewayId.c_str());
    
    if (!m_httpClient) {
        ESP_LOGE(TAG, "HTTP client is null");
        m_lastError = "HTTP client not initialized";
        return false;
    }

    // Clean up host URL
    if (m_firebaseHost.startsWith("http://") || m_firebaseHost.startsWith("https://")) {
        // Extract hostname from URL
        int start = m_firebaseHost.indexOf("://") + 3;
        int end = m_firebaseHost.indexOf("/", start);
        if (end < 0) end = m_firebaseHost.length();
        m_firebaseHost = m_firebaseHost.substring(start, end);
    }

    // Remove trailing slash
    if (m_firebaseHost.endsWith("/")) {
        m_firebaseHost = m_firebaseHost.substring(0, m_firebaseHost.length() - 1);
    }

    ESP_LOGI(TAG, "✅ Firebase client initialized (host: %s)", m_firebaseHost.c_str());
    return true;
}

// ===== High-Level Upload Methods =====

CellularFirebaseClient::UploadResult CellularFirebaseClient::uploadSensorData(
    const String& nodeId, 
    float temperature, 
    float humidity,
    int8_t rssi, 
    float snr)
{
    ESP_LOGI(TAG, "Uploading sensor data from %s...", nodeId.c_str());

    // Build JSON: {"nodeId":"0xXXXX","temperature":XX,"humidity":XX,"rssi":XX,"snr":XX,"timestamp":XX}
    String json = "{";
    json += "\"nodeId\":\"" + nodeId + "\",";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"snr\":" + String(snr, 1) + ",";
    json += "\"timestamp\":" + String(millis());
    json += "}";

    // Firebase path: /sensor_data/{gatewayId}/{nodeId} (POST to create timestamp key)
    String path = "/sensor_data/" + m_gatewayId + "/" + nodeId;

    return post(path, json);  // POST creates auto-ID for timestamp
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::uploadGatewayStatus(
    uint16_t nodesCount,
    uint32_t packetsRx,
    uint32_t packetsTx,
    int8_t cellularRssi,
    uint32_t freeHeap,
    uint32_t uptime)
{
    ESP_LOGI(TAG, "Uploading gateway status...");

    // Build JSON
    String json = "{";
    json += "\"gatewayId\":\"" + m_gatewayId + "\",";
    json += "\"nodesCount\":" + String(nodesCount) + ",";
    json += "\"packetsRx\":" + String(packetsRx) + ",";
    json += "\"packetsTx\":" + String(packetsTx) + ",";
    json += "\"cellularRssi\":" + String(cellularRssi) + ",";
    json += "\"freeHeap\":" + String(freeHeap) + ",";
    json += "\"uptime\":" + String(uptime) + ",";
    json += "\"timestamp\":" + String(millis());
    json += "}";

    // Firebase path: /gateways/{gatewayId}/status
    String path = "/gateways/" + m_gatewayId + "/status";

    return put(path, json);
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::uploadRoutingTable(const String& routesJson) {
    ESP_LOGI(TAG, "Uploading routing table...");

    // Firebase path: /gateways/{gatewayId}/routing_table
    String path = "/gateways/" + m_gatewayId + "/routing_table";

    return put(path, routesJson);
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::logEvent(
    const String& eventType,
    const String& nodeId,
    const String& details)
{
    ESP_LOGD(TAG, "Logging event: %s", eventType.c_str());

    // Build JSON - include "type" and "timestamp" for validation
    String json = "{";
    json += "\"type\":\"" + eventType + "\",";
    json += "\"nodeId\":\"" + nodeId + "\",";
    json += "\"details\":\"" + details + "\",";
    json += "\"gateway\":\"" + m_gatewayId + "\",";
    json += "\"timestamp\":" + String(millis());
    json += "}";

    // Firebase path: /events/{gatewayId} (POST to create auto-ID)
    String path = "/events/" + m_gatewayId;

    return post(path, json);
}

// ===== Low-Level Firebase REST API =====

CellularFirebaseClient::UploadResult CellularFirebaseClient::put(const String& path, const String& jsonData) {
    String url = buildUrl(path, "PUT");
    String body = m_autoTimestamp ? addTimestamp(jsonData) : jsonData;
    
    return executeRequest(CellularHTTPClient::HTTPMethod::PUT, url, body);
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::patch(const String& path, const String& jsonData) {
    String url = buildUrl(path, "PATCH");
    String body = m_autoTimestamp ? addTimestamp(jsonData) : jsonData;
    
    return executeRequest(CellularHTTPClient::HTTPMethod::PUT, url, body);  // Firebase uses PUT with PATCH semantics
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::post(const String& path, const String& jsonData) {
    String url = buildUrl(path, "POST");
    String body = m_autoTimestamp ? addTimestamp(jsonData) : jsonData;
    
    return executeRequest(CellularHTTPClient::HTTPMethod::POST, url, body);
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::get(const String& path, String& result) {
    String url = buildUrl(path, "GET");
    
    auto uploadResult = executeRequest(CellularHTTPClient::HTTPMethod::GET, url);
    
    if (uploadResult.success()) {
        result = uploadResult.message;  // Message contains response body
    }
    
    return uploadResult;
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::deleteData(const String& path) {
    String url = buildUrl(path, "DELETE");
    
    return executeRequest(CellularHTTPClient::HTTPMethod::DELETE, url);
}

// ===== Configuration =====

void CellularFirebaseClient::setUserId(const String& userId) {
    m_userId = userId;
    ESP_LOGI(TAG, "User ID set: %s", userId.c_str());
}

void CellularFirebaseClient::setAutoTimestamp(bool enabled) {
    m_autoTimestamp = enabled;
}

void CellularFirebaseClient::setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs) {
    m_maxRetries = maxRetries;
    m_retryDelayMs = retryDelayMs;
}

void CellularFirebaseClient::resetStats() {
    memset(&m_stats, 0, sizeof(Stats));
}

// ===== Private Helper Methods =====

String CellularFirebaseClient::buildUrl(const String& path, const String& method) {
    // Firebase REST API URL format:
    // https://<project-id>.firebaseio.com/<path>.json?auth=<secret>
    
    String url = "https://";  // Firebase REQUIRES HTTPS (port 443)
    url += m_firebaseHost;
    
    // Add path
    if (!path.startsWith("/")) {
        url += "/";
    }
    url += path;
    
    // Add .json extension
    if (!path.endsWith(".json")) {
        url += ".json";
    }
    
    // Add auth parameter
    url += "?auth=" + m_authSecret;
    
    return url;
}

String CellularFirebaseClient::addTimestamp(const String& json) {
    // Add timestamp field to JSON
    // Input:  {"field1":"value1"}
    // Output: {"field1":"value1","timestamp":1234567890}
    
    if (json.length() < 2) {
        return json;
    }
    
    // Get current timestamp (milliseconds since epoch)
    uint64_t timestamp = millis();  // TODO: Use NTP time if available
    
    // Find last }
    int lastBrace = json.lastIndexOf('}');
    if (lastBrace < 0) {
        return json;
    }
    
    String result = json.substring(0, lastBrace);
    
    // Add comma if not empty object
    if (result.length() > 1 && result.charAt(result.length() - 1) != '{') {
        result += ",";
    }
    
    result += "\"timestamp\":" + String((unsigned long)timestamp);
    result += "}";
    
    return result;
}

CellularFirebaseClient::UploadResult CellularFirebaseClient::executeRequest(
    CellularHTTPClient::HTTPMethod method,
    const String& url,
    const String& body)
{
    UploadResult result;
    uint8_t attempt = 0;
    
    while (attempt <= m_maxRetries) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "Retry attempt %d/%d...", attempt, m_maxRetries);
            delay(m_retryDelayMs * attempt);  // Exponential backoff
        }
        
        // Build HTTP request
        CellularHTTPClient::HTTPRequest request;
        request.method = method;
        request.url = url;
        request.headers["Content-Type"] = "application/json";
        
        if (body.length() > 0) {
            request.body = body;
        }
        
        // Execute request
        uint32_t startTime = millis();
        auto response = m_httpClient->request(request);
        result.responseTime = millis() - startTime;
        result.httpCode = response.statusCode;
        
        // Update statistics
        m_stats.totalUploads++;
        m_stats.totalBytesUpload += request.body.length();
        m_stats.totalBytesDownload += response.body.length();
        
        // Check result
        if (response.success) {
            result.status = UploadStatus::SUCCESS;
            result.message = response.body;
            m_stats.successfulUploads++;
            
            // Update average response time
            if (m_stats.successfulUploads > 0) {
                m_stats.avgResponseTime = 
                    (m_stats.avgResponseTime * (m_stats.successfulUploads - 1) + result.responseTime) 
                    / m_stats.successfulUploads;
            }
            
            ESP_LOGI(TAG, "✅ Firebase %s successful (%dms)", 
                     CellularHTTPClient::methodToString(method), result.responseTime);
            return result;
        } else {
            // Failed - determine error type
            result.status = httpCodeToStatus(response.statusCode);
            result.message = response.errorMessage;
            m_lastError = response.errorMessage;
            
            ESP_LOGW(TAG, "Firebase %s failed: %d %s", 
                     CellularHTTPClient::methodToString(method),
                     response.statusCode, 
                     response.statusMessage.c_str());
            
            // Don't retry on auth errors
            if (response.statusCode == 401 || response.statusCode == 403) {
                break;
            }
        }
        
        attempt++;
    }
    
    // All retries failed
    m_stats.failedUploads++;
    ESP_LOGE(TAG, "❌ Firebase request failed after %d attempts", attempt);
    
    return result;
}

CellularFirebaseClient::UploadStatus CellularFirebaseClient::httpCodeToStatus(int httpCode) {
    if (httpCode >= 200 && httpCode < 300) {
        return UploadStatus::SUCCESS;
    } else if (httpCode == 401 || httpCode == 403) {
        return UploadStatus::FAILED_AUTH;
    } else if (httpCode == 408 || httpCode == 504) {
        return UploadStatus::FAILED_TIMEOUT;
    } else if (httpCode >= 400 && httpCode < 500) {
        return UploadStatus::FAILED_PARSE;
    } else if (httpCode >= 500) {
        return UploadStatus::FAILED_NETWORK;
    } else if (httpCode == 0) {
        return UploadStatus::FAILED_CONNECTION;
    }
    
    return UploadStatus::FAILED_UNKNOWN;
}
