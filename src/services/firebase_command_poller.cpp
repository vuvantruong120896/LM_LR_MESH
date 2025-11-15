#include "firebase_command_poller.h"
#include "../application/app_gateway/firebase_coordinator.h"  // COORDINATION FIX (Oct 24, 2025)
#include <esp_log.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

static const char* TAG = "CMD_POLLER";

FirebaseCommandPoller::FirebaseCommandPoller(FirebaseData* fbdo, const String& userUID, const String& gatewayMAC)
    : m_fbdo(fbdo), 
      m_userUID(userUID), 
      m_gatewayMAC(gatewayMAC), 
      m_hasCommand(false), 
      m_lastPoll(0),
      m_pollInterval(10000),  // Poll every 10 seconds
      m_enabled(true),
      m_consecutiveFailures(0),
      m_cooldownUntilMs(0),
      m_minHeapThreshold(25600),  // 25 KB minimum
      m_maxConsecutiveFailures(3),
      m_cooldownBaseMs(30000),    // 30 seconds base cooldown
      m_firebaseHost(""),         // Will be extracted from Firebase config
      m_pollingTaskHandle(nullptr),
      m_taskRunning(false) {
    
    // Build base path: users/{uid}/commands/{mac}
    m_basePath = String("users/") + userUID + "/commands/" + gatewayMAC;
    
    ESP_LOGI(TAG, "Command poller created");
    ESP_LOGI(TAG, "User UID: %s", userUID.c_str());
    ESP_LOGI(TAG, "Gateway MAC: %s", gatewayMAC.c_str());
    ESP_LOGI(TAG, "Base path: %s", m_basePath.c_str());
    
    // Extract Firebase host for DNS checks (assuming standard Firebase host format)
    // This is a simplified approach - in production you might get it from config
    m_firebaseHost = String(userUID).substring(0, userUID.indexOf("-")) + ".firebaseio.com";
}

FirebaseCommandPoller::~FirebaseCommandPoller() {
    // Stop polling task
    if (m_pollingTaskHandle != nullptr) {
        m_taskRunning = false;
        vTaskDelay(pdMS_TO_TICKS(200)); // Give task time to exit
        vTaskDelete(m_pollingTaskHandle);
        m_pollingTaskHandle = nullptr;
        ESP_LOGI(TAG, "Command poller task stopped");
    }
}

void FirebaseCommandPoller::begin(uint32_t stackSize, uint8_t priority, int coreId) {
    ESP_LOGI(TAG, "=== Command Poller Initialized ===");
    ESP_LOGI(TAG, "Poll interval: %u ms", m_pollInterval);
    ESP_LOGI(TAG, "Enabled: %s", m_enabled ? "YES" : "NO");
    ESP_LOGI(TAG, "Circuit breaker: max failures=%u, cooldown=%u ms", 
             m_maxConsecutiveFailures, m_cooldownBaseMs);
    ESP_LOGI(TAG, "Min heap threshold: %u bytes", m_minHeapThreshold);
    
    // Create dedicated polling task
    m_taskRunning = true;
    BaseType_t result = xTaskCreatePinnedToCore(
        pollingTask,
        "CmdPoller",
        stackSize,
        this,           // Pass this instance as parameter
        priority,
        &m_pollingTaskHandle,
        coreId
    );
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "✅ Command polling task started on core %d (stack: %u, priority: %u)",
                 coreId, stackSize, priority);
        ESP_LOGI(TAG, "   Network operations isolated from main loop");
    } else {
        ESP_LOGE(TAG, "❌ Failed to create command polling task!");
        m_taskRunning = false;
    }
}

void FirebaseCommandPoller::pollingTask(void* parameter) {
    FirebaseCommandPoller* poller = static_cast<FirebaseCommandPoller*>(parameter);
    ESP_LOGI(TAG, "[POLLER-TASK] Command polling task started");
    
    // Stack monitoring
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[POLLER-TASK] Initial stack: %u bytes free", stackHighWaterMark);
    
    while (poller->m_taskRunning) {
        
        if (!poller->m_enabled) {
            vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep 1s if disabled
            continue;
        }
        
        uint32_t now = millis();
        
        // Check poll interval
        if (now - poller->m_lastPoll < poller->m_pollInterval) {
            vTaskDelay(pdMS_TO_TICKS(500)); // Sleep 500ms and check again
            continue;
        }
        
        poller->m_lastPoll = now;
        
        // Check circuit breaker cooldown
        if (poller->isInCooldown()) {
            uint32_t remainingMs = poller->m_cooldownUntilMs - now;
            ESP_LOGW(TAG, "[CIRCUIT-BREAKER] In cooldown for %u more seconds", remainingMs / 1000);
            vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5s during cooldown
            continue;
        }
        
        // Pre-flight checks
        if (!poller->heapCheck()) {
            ESP_LOGW(TAG, "[HEAP-GUARD] Insufficient heap, skipping poll");
            poller->handleFailure();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        if (!poller->dnsPreCheck()) {
            ESP_LOGW(TAG, "[DNS-CHECK] DNS resolution failed, skipping poll");
            poller->handleFailure();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // BINARY SEMAPHORE COORDINATION (Oct 24, 2025 - v2):
        // Step 1: Wait for minimum interval (lightweight, doesn't block other tasks)
        FirebaseOperationCoordinator::waitForMinInterval();
        
        // Step 2: Try to acquire exclusive operation lock (prevents concurrent Firebase HTTP requests)
        // Use 5s timeout - if Worker holds lock, we'll wait or skip
        if (!FirebaseOperationCoordinator::tryAcquireOperationLock(5000)) {
            ESP_LOGW(TAG, "[OP-LOCK] Failed to acquire operation lock (Worker busy?), skipping poll");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // Step 3: We now have exclusive access - perform Firebase operation
        ESP_LOGD(TAG, "Polling for pending commands...");
        bool foundCommand = poller->fetchPendingCommands();
        
        // Step 4: Release operation lock IMMEDIATELY after HTTP completes
        FirebaseOperationCoordinator::releaseOperationLock();
        
        // Step 5: Mark completion for interval tracking
        FirebaseOperationCoordinator::markOperationComplete();
        
        if (foundCommand) {
            ESP_LOGI(TAG, "✅ Found pending command!");
            ESP_LOGI(TAG, "  ID: %s", poller->m_currentCommand.id.c_str());
            ESP_LOGI(TAG, "  Type: %s", poller->m_currentCommand.type.c_str());
            ESP_LOGI(TAG, "  Priority: %d", poller->m_currentCommand.priority);
            poller->handleSuccess();
        }
        
        // Stack monitoring (warn if low)
        stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        if (stackHighWaterMark < 1024) {
            ESP_LOGW(TAG, "⚠️ [POLLER-TASK] Low stack: %u bytes", stackHighWaterMark);
        }
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "[POLLER-TASK] Command polling task exiting");
    vTaskDelete(NULL);
}

void FirebaseCommandPoller::poll() {
    // Legacy method - now handled by dedicated task
    // Keep for backward compatibility but log warning
    ESP_LOGW(TAG, "poll() called directly - polling now runs in dedicated task");
}

bool FirebaseCommandPoller::fetchPendingCommands() {
    if (!m_fbdo) {
        ESP_LOGE(TAG, "Firebase data object is null!");
        handleFailure();
        return false;
    }
    
    String pendingPath = m_basePath + "/pending";
    
    ESP_LOGD(TAG, "Fetching from: %s", pendingPath.c_str());
    
    // Static counter for periodic WiFi reconnect (prevent socket leak in Command Poller)
    static uint32_t pollCount = 0;
    pollCount++;
    
    // Reconnect WiFi every 10 polls to release accumulated sockets
    if (pollCount % 10 == 0) {
        ESP_LOGI(TAG, "🔄 [POLLER] Reconnecting WiFi after %u polls (socket cleanup)", pollCount);
        Firebase.reconnectWiFi(true);
        delay(200);  // Wait for TCP stack cleanup
    }
    
    // Get all pending commands
    if (!Firebase.getJSON(*m_fbdo, pendingPath.c_str())) {
        String errorReason = m_fbdo->errorReason();
        
        // CRITICAL: Clean up FirebaseData buffers after failed operation
        m_fbdo->clear();
        delay(50);  // TCP cleanup time
        
        // Distinguish between "no data" (normal) vs network/TLS errors
        if (errorReason.indexOf("connection") >= 0 || 
            errorReason.indexOf("SSL") >= 0 ||
            errorReason.indexOf("timeout") >= 0 ||
            errorReason.indexOf("refused") >= 0) {
            // Network/TLS error - trigger circuit breaker
            ESP_LOGW(TAG, "Network/TLS error: %s", errorReason.c_str());
            handleFailure();
        } else {
            // Likely just no pending commands - not a failure
            ESP_LOGD(TAG, "No pending commands (or benign error: %s)", errorReason.c_str());
        }
        return false;
    }
    
    FirebaseJson json = m_fbdo->to<FirebaseJson>();
    size_t len = json.iteratorBegin();
    
    if (len == 0) {
        ESP_LOGD(TAG, "Pending queue is empty");
        json.iteratorEnd();
        
        // CRITICAL: Clean up after successful read with no data
        m_fbdo->clear();
        delay(50);
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
    bool parsed = parseCommand(key, value);
    
    // CRITICAL: Clean up FirebaseData buffers after successful operation
    m_fbdo->clear();
    delay(50);  // TCP cleanup time
    
    if (parsed) {
        m_hasCommand = true;
        return true;
    }
    
    ESP_LOGW(TAG, "Failed to parse command");
    return false;
}

bool FirebaseCommandPoller::parseCommand(const String& key, const String& value) {
    // Parse JSON command
    JsonDocument doc;
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
    
    // Update current command with processing start time (for timeout detection)
    m_currentCommand.processingStartTime = millis();
    
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

bool FirebaseCommandPoller::dnsPreCheck() {
    // Simple DNS check using WiFi.hostByName
    // This is a fast check (typically <1s) compared to full TCP/TLS connection
    
    if (m_firebaseHost.isEmpty()) {
        ESP_LOGD(TAG, "[DNS-CHECK] No host configured, skipping");
        return true; // Allow if not configured
    }
    
    IPAddress ip;
    int result = WiFi.hostByName(m_firebaseHost.c_str(), ip);
    
    if (result == 1) {
        ESP_LOGD(TAG, "[DNS-CHECK] ✅ %s -> %s", m_firebaseHost.c_str(), ip.toString().c_str());
        return true;
    } else {
        ESP_LOGW(TAG, "[DNS-CHECK] ❌ Failed to resolve %s", m_firebaseHost.c_str());
        return false;
    }
}

bool FirebaseCommandPoller::heapCheck() {
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    
    ESP_LOGD(TAG, "[HEAP-CHECK] Free: %u bytes, Min: %u bytes, Threshold: %u bytes",
             freeHeap, minFreeHeap, m_minHeapThreshold);
    
    if (freeHeap < m_minHeapThreshold) {
        ESP_LOGW(TAG, "[HEAP-GUARD] ⚠️ Low heap: %u < %u bytes", freeHeap, m_minHeapThreshold);
        return false;
    }
    
    return true;
}

void FirebaseCommandPoller::handleFailure() {
    m_consecutiveFailures++;
    
    ESP_LOGW(TAG, "[CIRCUIT-BREAKER] Failure #%u (max: %u)", 
             m_consecutiveFailures, m_maxConsecutiveFailures);
    
    if (m_consecutiveFailures >= m_maxConsecutiveFailures) {
        // Exponential backoff: 30s, 60s, 120s, 240s, max 300s (5 min)
        uint32_t cooldownMs = m_cooldownBaseMs * (1 << (m_consecutiveFailures - m_maxConsecutiveFailures));
        cooldownMs = min(cooldownMs, 300000U); // Cap at 5 minutes
        
        m_cooldownUntilMs = millis() + cooldownMs;
        
        ESP_LOGE(TAG, "[CIRCUIT-BREAKER] ⚡ TRIPPED! Cooldown for %u seconds", cooldownMs / 1000);
        ESP_LOGE(TAG, "   Consecutive failures: %u", m_consecutiveFailures);
        ESP_LOGE(TAG, "   Will retry at: %u ms", m_cooldownUntilMs);
    }
}

void FirebaseCommandPoller::handleSuccess() {
    if (m_consecutiveFailures > 0) {
        ESP_LOGI(TAG, "[CIRCUIT-BREAKER] ✅ Success! Resetting failure count (was %u)", 
                 m_consecutiveFailures);
    }
    m_consecutiveFailures = 0;
    m_cooldownUntilMs = 0;
}

bool FirebaseCommandPoller::isInCooldown() {
    if (m_cooldownUntilMs == 0) {
        return false;
    }
    
    uint32_t now = millis();
    if (now >= m_cooldownUntilMs) {
        ESP_LOGI(TAG, "[CIRCUIT-BREAKER] Cooldown ended, resuming polling");
        m_cooldownUntilMs = 0;
        return false;
    }
    
    return true;
}
