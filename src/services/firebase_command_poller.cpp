#include "firebase_command_poller.h"
#include <esp_log.h>
#include <ArduinoJson.h>

static const char* TAG = "CMD_POLLER";

FirebaseCommandPoller::FirebaseCommandPoller(FirebaseData* fbdo, const String& userUID, const String& gatewayMAC)
    : m_fbdo(fbdo), 
      m_userUID(userUID), 
      m_gatewayMAC(gatewayMAC), 
      m_hasCommand(false), 
      m_lastPoll(0),
      m_pollInterval(10000),  // Poll every 10 seconds
      m_enabled(true) {
    
    // Build base path: users/{uid}/commands/{mac}
    m_basePath = String("users/") + userUID + "/commands/" + gatewayMAC;
    
    ESP_LOGI(TAG, "Command poller created");
    ESP_LOGI(TAG, "User UID: %s", userUID.c_str());
    ESP_LOGI(TAG, "Gateway MAC: %s", gatewayMAC.c_str());
    ESP_LOGI(TAG, "Base path: %s", m_basePath.c_str());
}

void FirebaseCommandPoller::begin() {
    ESP_LOGI(TAG, "=== Command Poller Initialized ===");
    ESP_LOGI(TAG, "Poll interval: %u ms", m_pollInterval);
    ESP_LOGI(TAG, "Enabled: %s", m_enabled ? "YES" : "NO");
}

void FirebaseCommandPoller::poll() {
    if (!m_enabled) {
        return;
    }
    
    uint32_t now = millis();
    
    // Poll at configured interval
    if (now - m_lastPoll < m_pollInterval) {
        return;
    }
    
    m_lastPoll = now;
    
    ESP_LOGD(TAG, "Polling for pending commands...");
    
    if (fetchPendingCommands()) {
        ESP_LOGI(TAG, "✅ Found pending command!");
        ESP_LOGI(TAG, "  ID: %s", m_currentCommand.id.c_str());
        ESP_LOGI(TAG, "  Type: %s", m_currentCommand.type.c_str());
        ESP_LOGI(TAG, "  Priority: %d", m_currentCommand.priority);
    }
}

bool FirebaseCommandPoller::fetchPendingCommands() {
    if (!m_fbdo) {
        ESP_LOGE(TAG, "Firebase data object is null!");
        return false;
    }
    
    String pendingPath = m_basePath + "/pending";
    
    ESP_LOGD(TAG, "Fetching from: %s", pendingPath.c_str());
    
    // Get all pending commands
    if (!Firebase.getJSON(*m_fbdo, pendingPath.c_str())) {
        // Not an error - just no pending commands
        ESP_LOGD(TAG, "No pending commands (or error: %s)", m_fbdo->errorReason().c_str());
        return false;
    }
    
    FirebaseJson json = m_fbdo->to<FirebaseJson>();
    size_t len = json.iteratorBegin();
    
    if (len == 0) {
        ESP_LOGD(TAG, "Pending queue is empty");
        json.iteratorEnd();
        return false;
    }
    
    ESP_LOGI(TAG, "Found %d pending command(s)", len);
    
    // Get first command (Firebase sorts by key, we'll get oldest)
    String key, value;
    int type;
    
    json.iteratorGet(0, type, key, value);
    json.iteratorEnd();
    
    ESP_LOGD(TAG, "Command key: %s", key.c_str());
    ESP_LOGD(TAG, "Command value: %s", value.c_str());
    
    // Parse command
    if (parseCommand(key, value)) {
        m_hasCommand = true;
        return true;
    }
    
    ESP_LOGW(TAG, "Failed to parse command");
    return false;
}

bool FirebaseCommandPoller::parseCommand(const String& key, const String& value) {
    // Parse JSON command
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, value);
    
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
    if (doc.containsKey("params")) {
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
    
    return true;
}

bool FirebaseCommandPoller::hasCommand() {
    return m_hasCommand;
}

FirebaseCommandPoller::Command FirebaseCommandPoller::getNextCommand() {
    m_hasCommand = false;
    return m_currentCommand;
}

void FirebaseCommandPoller::moveToProcessing(const Command& cmd) {
    ESP_LOGI(TAG, "Moving command %s to processing...", cmd.id.c_str());
    
    String pendingPath = m_basePath + "/pending/" + cmd.id;
    String processingPath = m_basePath + "/processing/" + cmd.id;
    
    // Create processing entry
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "processing");
    json.set("startedAt", (unsigned long)millis());
    json.set("timestamp", cmd.timestamp);
    
    // Add params if not empty
    if (!cmd.params.isEmpty() && cmd.params != "{}") {
        json.set("params", cmd.params);
    }
    
    // Write to processing
    if (Firebase.setJSON(*m_fbdo, processingPath.c_str(), json)) {
        ESP_LOGI(TAG, "✅ Command moved to processing");
        
        // Delete from pending
        if (Firebase.deleteNode(*m_fbdo, pendingPath.c_str())) {
            ESP_LOGI(TAG, "✅ Removed from pending queue");
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to remove from pending: %s", m_fbdo->errorReason().c_str());
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to move to processing: %s", m_fbdo->errorReason().c_str());
    }
    
    // Update command_results for real-time UI updates
    updateCommandResult(cmd.id, "processing", "Command is being executed");
}

void FirebaseCommandPoller::moveToCompleted(const Command& cmd, const String& result, 
                                            const String& message, const String& details) {
    ESP_LOGI(TAG, "Marking command %s as completed", cmd.id.c_str());
    
    String processingPath = m_basePath + "/processing/" + cmd.id;
    String completedPath = m_basePath + "/completed/" + cmd.id;
    
    // Create completed entry
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "completed");
    json.set("result", result);
    json.set("message", message);
    json.set("completedAt", (unsigned long)millis());
    json.set("executionTimeMs", (unsigned long)(millis() - cmd.timestamp));
    
    // Add details if provided
    if (!details.isEmpty()) {
        json.set("details", details);
    }
    
    // Write to completed
    if (Firebase.setJSON(*m_fbdo, completedPath.c_str(), json)) {
        ESP_LOGI(TAG, "✅ Command marked as completed");
        
        // Delete from processing
        Firebase.deleteNode(*m_fbdo, processingPath.c_str());
    } else {
        ESP_LOGE(TAG, "❌ Failed to mark as completed: %s", m_fbdo->errorReason().c_str());
    }
    
    // Update command_results
    updateCommandResult(cmd.id, "completed", message);
    
    // Cleanup old commands (keep last 10)
    cleanupOldCommands();
}

void FirebaseCommandPoller::moveToFailed(const Command& cmd, const String& errorCode, const String& message) {
    ESP_LOGE(TAG, "Marking command %s as failed: %s", cmd.id.c_str(), message.c_str());
    
    String processingPath = m_basePath + "/processing/" + cmd.id;
    String failedPath = m_basePath + "/failed/" + cmd.id;
    
    // Create failed entry
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "failed");
    json.set("result", "failed");
    json.set("errorCode", errorCode);
    json.set("message", message);
    json.set("failedAt", (unsigned long)millis());
    json.set("executionTimeMs", (unsigned long)(millis() - cmd.timestamp));
    
    // Write to failed
    if (Firebase.setJSON(*m_fbdo, failedPath.c_str(), json)) {
        ESP_LOGE(TAG, "✅ Command marked as failed");
        
        // Delete from processing
        Firebase.deleteNode(*m_fbdo, processingPath.c_str());
    } else {
        ESP_LOGE(TAG, "❌ Failed to mark as failed: %s", m_fbdo->errorReason().c_str());
    }
    
    // Update command_results
    updateCommandResult(cmd.id, "failed", message);
}

void FirebaseCommandPoller::updateCommandResult(const String& cmdId, const String& status, const String& message) {
    String resultPath = String("users/") + m_userUID + "/command_results/" + m_gatewayMAC;
    
    FirebaseJson json;
    json.set("last_command_id", cmdId);
    json.set("last_command_type", m_currentCommand.type);
    json.set("status", status);
    json.set("message", message);
    json.set("timestamp", (unsigned long)millis());
    json.set("gateway_online", true);
    json.set("last_poll", (unsigned long)millis());
    
    if (Firebase.updateNode(*m_fbdo, resultPath.c_str(), json)) {
        ESP_LOGD(TAG, "✅ Command result updated: %s", status.c_str());
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to update command result: %s", m_fbdo->errorReason().c_str());
    }
}

void FirebaseCommandPoller::updateProgress(const String& cmdId, uint16_t nodesDiscovered, uint32_t timeRemaining) {
    String resultPath = String("users/") + m_userUID + "/command_results/" + m_gatewayMAC + "/progress";
    
    FirebaseJson json;
    json.set("nodes_discovered", nodesDiscovered);
    json.set("time_remaining_ms", timeRemaining);
    
    if (Firebase.updateNode(*m_fbdo, resultPath.c_str(), json)) {
        ESP_LOGD(TAG, "✅ Progress updated: %d nodes, %u ms remaining", nodesDiscovered, timeRemaining);
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to update progress: %s", m_fbdo->errorReason().c_str());
    }
}

void FirebaseCommandPoller::cleanupOldCommands() {
    // TODO: Implement cleanup logic
    // - Keep only last 10 completed commands
    // - Delete failed commands older than 1 hour
    ESP_LOGD(TAG, "Command cleanup not yet implemented");
}
