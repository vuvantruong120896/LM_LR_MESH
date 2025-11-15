#include "cellular_firebase_command_poller.h"
#include "../components/cellular/include/cellular_firebase_https_client.h"
#include <esp_log.h>
#include <ArduinoJson.h>

static const char* TAG = "CELLULAR_CMD_POLLER";

CellularFirebaseCommandPoller::CellularFirebaseCommandPoller(
    CellularFirebaseHTTPSClient* httpsClient,
    const String& userUID, 
    const String& gatewayMAC)
    : m_httpsClient(httpsClient),
      m_userUID(userUID), 
      m_gatewayMAC(gatewayMAC), 
      m_hasCommand(false), 
      m_lastPoll(0),
      m_lastPollStartTime(0),
      m_pollInterval(30000),  // Fixed: Poll every 30 seconds (cellular bandwidth conservation)
      m_enabled(true),
      m_pollingTaskHandle(nullptr),
      m_taskRunning(false) {
    
    // Build base path: users/{uid}/commands/{mac}
    m_basePath = String("users/") + userUID + "/commands/" + gatewayMAC;
    
    ESP_LOGI(TAG, "Cellular command poller created");
    ESP_LOGI(TAG, "User UID: %s", userUID.c_str());
    ESP_LOGI(TAG, "Gateway MAC: %s", gatewayMAC.c_str());
    ESP_LOGI(TAG, "Base path: %s", m_basePath.c_str());
}

CellularFirebaseCommandPoller::~CellularFirebaseCommandPoller() {
    // Stop polling task
    if (m_pollingTaskHandle != nullptr) {
        m_taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(200)); // Give task time to exit
        vTaskDelete(m_pollingTaskHandle);
        m_pollingTaskHandle = nullptr;
        ESP_LOGI(TAG, "Cellular command poller task stopped");
    }
}

void CellularFirebaseCommandPoller::begin(uint32_t stackSize, uint8_t priority, int coreId) {
    ESP_LOGI(TAG, "=== Cellular Command Poller Initialized ===");
    ESP_LOGI(TAG, "Poll interval: %u ms", m_pollInterval);
    ESP_LOGI(TAG, "Enabled: %s", m_enabled ? "YES" : "NO");
    
    // Create dedicated polling task
    m_taskRunning = true;
    BaseType_t result = xTaskCreatePinnedToCore(
        pollingTask,
        "CellularCmdPoller",
        stackSize,
        this,           // Pass this instance as parameter
        priority,
        &m_pollingTaskHandle,
        coreId
    );
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "✅ Cellular command polling task started on core %d (stack: %u, priority: %u)",
                 coreId, stackSize, priority);
        ESP_LOGI(TAG, "   HTTPS operations isolated from main loop (CPU0)");
    } else {
        ESP_LOGE(TAG, "❌ Failed to create cellular command polling task!");
        m_taskRunning = false;
    }
}

void CellularFirebaseCommandPoller::pollingTask(void* parameter) {
    CellularFirebaseCommandPoller* poller = static_cast<CellularFirebaseCommandPoller*>(parameter);
    ESP_LOGI(TAG, "[CELLULAR-POLLER-TASK] Command polling task started");
    ESP_LOGI(TAG, "[CELLULAR-POLLER-TASK] Poll interval: 30 seconds");
    ESP_LOGI(TAG, "[CELLULAR-POLLER-TASK] Poll stuck timeout: 45 seconds (if poll takes longer, force next poll)");
    
    // Stack monitoring
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[CELLULAR-POLLER-TASK] Initial stack: %u bytes free", stackHighWaterMark);
    
    while (poller->m_taskRunning) {
        
        if (!poller->m_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep 1s if disabled
            continue;
        }
        
        uint32_t now = millis();
        
        // FIX #1: Check if previous poll is stuck (> 45 seconds)
        if (poller->m_lastPollStartTime > 0) {
            uint32_t pollDuration = now - poller->m_lastPollStartTime;
            if (pollDuration > POLL_STUCK_TIMEOUT_MS) {
                ESP_LOGW(TAG, "[CELLULAR-POLLER-TASK] ⚠️ Poll stuck! Duration: %u ms (limit: %u ms)", 
                         pollDuration, POLL_STUCK_TIMEOUT_MS);
                ESP_LOGW(TAG, "[CELLULAR-POLLER-TASK] 🔄 Force resetting poll timer to trigger next poll immediately");
                poller->m_lastPoll = 0;  // Force next poll immediately
                poller->m_lastPollStartTime = 0;
            }
        }
        
        // Check poll interval (30 seconds fixed)
        if (now - poller->m_lastPoll < poller->m_pollInterval) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep 1s and check again
            continue;
        }
        
        // Mark poll start time
        poller->m_lastPollStartTime = now;
        poller->m_lastPoll = now;
        
        ESP_LOGD(TAG, "🔎 [CELLULAR-POLLER-TASK] Polling for pending commands...");
        bool foundCommand = poller->fetchPendingCommands();
        
        if (foundCommand) {
            ESP_LOGI(TAG, "✅ [CELLULAR-POLLER-TASK] Command found and ready for processing");
        }
        
        // Mark poll end (successful completion)
        poller->m_lastPollStartTime = 0;
        
        // Stack monitoring (periodic)
        stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (stackHighWaterMark < 1024) {
            ESP_LOGW(TAG, "⚠️ [CELLULAR-POLLER-TASK] Low stack: %u bytes free", stackHighWaterMark);
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // Small delay before next iteration
    }
    
    ESP_LOGI(TAG, "🎯 [CELLULAR-POLLER-TASK] Task stopping...");
    vTaskDelete(NULL);
}

void CellularFirebaseCommandPoller::poll() {
    // Legacy method - now runs in dedicated task
    // Kept for compatibility
}

bool CellularFirebaseCommandPoller::fetchPendingCommands() {
    // Build pending commands path
    String pendingPath = m_basePath + "/pending.json";
    
    ESP_LOGD(TAG, "GET %s", pendingPath.c_str());
    
    // Perform HTTPS GET request
    CellularFirebaseHTTPSClient::UploadResult result = 
        m_httpsClient->httpGet(pendingPath);
    
    if (!result.success) {
        ESP_LOGW(TAG, "Failed to fetch commands: %s", result.message.c_str());
        return false;
    }
    
    // Check if response is empty or null
    if (result.response.isEmpty() || result.response == "null") {
        ESP_LOGD(TAG, "No pending commands");
        return false;
    }
    
    ESP_LOGI(TAG, "Pending commands response: %s", result.response.c_str());
    
    // Extract first command from JSON response
    String cmdKey, cmdValue;
    if (!extractFirstCommand(result.response, cmdKey, cmdValue)) {
        ESP_LOGW(TAG, "No commands found in response");
        return false;
    }
    
    ESP_LOGI(TAG, "Found command: %s", cmdKey.c_str());
    
    // Parse command
    if (parseCommand(cmdKey, cmdValue)) {
        m_hasCommand = true;
        return true;
    }
    
    ESP_LOGW(TAG, "Failed to parse command");
    return false;
}

bool CellularFirebaseCommandPoller::extractFirstCommand(
    const String& jsonResponse, 
    String& outKey, 
    String& outValue) {
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }
    
    // Check if response is an object with command keys
    if (!doc.is<JsonObject>()) {
        ESP_LOGW(TAG, "Response is not a JSON object");
        return false;
    }
    
    JsonObject obj = doc.as<JsonObject>();
    
    // Get first command (Firebase sorts by key)
    for (JsonPair kv : obj) {
        outKey = String(kv.key().c_str());
        
        // Serialize command value back to JSON string
        String cmdJson;
        serializeJson(kv.value(), cmdJson);
        outValue = cmdJson;
        
        ESP_LOGD(TAG, "Extracted command: %s = %s", outKey.c_str(), outValue.c_str());
        return true;
    }
    
    return false;
}

bool CellularFirebaseCommandPoller::parseCommand(const String& key, const String& valueJson) {
    // Parse JSON command
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, valueJson);
    
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }
    
    // Extract fields
    m_currentCommand.id = key;
    m_currentCommand.type = doc["type"] | "";
    m_currentCommand.timestamp = doc["timestamp"] | 0;
    m_currentCommand.priority = doc["priority"] | 1;
    
    // Extract params (keep as JSON string for flexibility)
    if (doc["params"].is<JsonVariant>()) {
        String paramsStr;
        serializeJson(doc["params"], paramsStr);
        m_currentCommand.params = paramsStr;
    } else {
        m_currentCommand.params = "{}";
    }
    
    // Validate required fields
    if (m_currentCommand.id.isEmpty() || m_currentCommand.type.isEmpty()) {
        ESP_LOGE(TAG, "Invalid command: missing required fields");
        return false;
    }
    
    ESP_LOGI(TAG, "Parsed command: ID=%s, Type=%s, Params=%s", 
             m_currentCommand.id.c_str(), 
             m_currentCommand.type.c_str(),
             m_currentCommand.params.c_str());
    
    return true;
}

bool CellularFirebaseCommandPoller::hasCommand() {
    return m_hasCommand;
}

CellularFirebaseCommandPoller::Command CellularFirebaseCommandPoller::getNextCommand() {
    Command cmd = m_currentCommand;
    m_hasCommand = false;
    return cmd;
}

bool CellularFirebaseCommandPoller::moveToProcessing(const Command& cmd) {
    ESP_LOGI(TAG, "Moving command to processing: %s", cmd.id.c_str());
    
    // Build paths
    String pendingPath = m_basePath + "/pending/" + cmd.id + ".json";
    String processingPath = m_basePath + "/processing/" + cmd.id + ".json";
    
    // Create processing status JSON
    JsonDocument doc;
    doc["type"] = cmd.type;
    doc["timestamp"] = cmd.timestamp;
    doc["priority"] = cmd.priority;
    doc["status"] = "processing";
    doc["startedAt"] = millis();
    
    // Add params if not empty
    if (!cmd.params.isEmpty() && cmd.params != "{}") {
        JsonDocument paramsDoc;
        deserializeJson(paramsDoc, cmd.params);
        doc["params"] = paramsDoc;
    }
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Upload to processing
    CellularFirebaseHTTPSClient::UploadResult result = 
        m_httpsClient->httpPut(processingPath, jsonData);
    
    if (!result.success) {
        ESP_LOGE(TAG, "Failed to move to processing: %s", result.message.c_str());
        return false;
    }
    
    // Delete from pending (use DELETE method)
    result = m_httpsClient->httpDelete(pendingPath);
    
    if (!result.success) {
        ESP_LOGW(TAG, "Failed to delete from pending: %s", result.message.c_str());
        // Not critical - command is in processing anyway
    }
    
    // Update current command with processing start time (for timeout detection)
    m_currentCommand.processingStartTime = millis();
    
    ESP_LOGI(TAG, "Command moved to processing successfully");
    return true;
}

bool CellularFirebaseCommandPoller::moveToCompleted(
    const Command& cmd, 
    const String& result, 
    const String& message,
    const String& details) {
    
    ESP_LOGI(TAG, "Moving command to completed: %s", cmd.id.c_str());
    
    // Build paths
    String processingPath = m_basePath + "/processing/" + cmd.id + ".json";
    String completedPath = m_basePath + "/completed/" + cmd.id + ".json";
    
    // Create completed status JSON
    JsonDocument doc;
    doc["type"] = cmd.type;
    doc["timestamp"] = cmd.timestamp;
    doc["status"] = "completed";
    doc["result"] = result;
    doc["message"] = message;
    doc["completedAt"] = millis();
    
    if (!details.isEmpty() && details != "{}") {
        JsonDocument detailsDoc;
        deserializeJson(detailsDoc, details);
        doc["details"] = detailsDoc;
    }
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Upload to completed
    CellularFirebaseHTTPSClient::UploadResult uploadResult = 
        m_httpsClient->httpPut(completedPath, jsonData);
    
    if (!uploadResult.success) {
        ESP_LOGE(TAG, "Failed to move to completed: %s", uploadResult.message.c_str());
        return false;
    }
    
    // Delete from processing
    uploadResult = m_httpsClient->httpDelete(processingPath);
    
    if (!uploadResult.success) {
        ESP_LOGW(TAG, "Failed to delete from processing: %s", uploadResult.message.c_str());
        // Not critical
    }
    
    ESP_LOGI(TAG, "Command moved to completed successfully");
    return true;
}

bool CellularFirebaseCommandPoller::moveToFailed(
    const Command& cmd, 
    const String& errorCode, 
    const String& message) {
    
    ESP_LOGI(TAG, "Moving command to failed: %s", cmd.id.c_str());
    
    // Build paths
    String processingPath = m_basePath + "/processing/" + cmd.id + ".json";
    String failedPath = m_basePath + "/failed/" + cmd.id + ".json";
    
    // Create failed status JSON
    JsonDocument doc;
    doc["type"] = cmd.type;
    doc["timestamp"] = cmd.timestamp;
    doc["status"] = "failed";
    doc["errorCode"] = errorCode;
    doc["message"] = message;
    doc["failedAt"] = millis();
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Upload to failed
    CellularFirebaseHTTPSClient::UploadResult result = 
        m_httpsClient->httpPut(failedPath, jsonData);
    
    if (!result.success) {
        ESP_LOGE(TAG, "Failed to move to failed: %s", result.message.c_str());
        return false;
    }
    
    // Delete from processing
    result = m_httpsClient->httpDelete(processingPath);
    
    if (!result.success) {
        ESP_LOGW(TAG, "Failed to delete from processing: %s", result.message.c_str());
        // Not critical
    }
    
    ESP_LOGI(TAG, "Command moved to failed successfully");
    return true;
}

bool CellularFirebaseCommandPoller::updateProgress(
    const String& cmdId, 
    uint16_t nodesDiscovered, 
    uint32_t timeRemaining) {
    
    ESP_LOGD(TAG, "Updating progress for command: %s", cmdId.c_str());
    
    // Build progress path
    String progressPath = m_basePath + "/processing/" + cmdId + "/progress.json";
    
    // Create progress JSON
    JsonDocument doc;
    doc["nodesDiscovered"] = nodesDiscovered;
    doc["timeRemaining"] = timeRemaining;
    doc["updatedAt"] = millis();
    
    String jsonData;
    serializeJson(doc, jsonData);
    
    // Upload progress
    CellularFirebaseHTTPSClient::UploadResult result = 
        m_httpsClient->httpPut(progressPath, jsonData);
    
    if (!result.success) {
        ESP_LOGW(TAG, "Failed to update progress: %s", result.message.c_str());
        return false;
    }
    
    ESP_LOGD(TAG, "Progress updated successfully");
    return true;
}
