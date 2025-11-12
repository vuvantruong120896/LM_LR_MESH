/**
 * @file test_cellular_phase6.cpp
 * @brief Phase 6 Test: Firebase Command Queue
 * 
 * Tests:
 * 1. Command queue initialization
 * 2. Command polling and parsing
 * 3. Command execution (REBOOT, GET_STATUS, CLEAR_ROUTES)
 * 4. Status updates (processing → completed/failed)
 * 5. Firebase structure validation
 */

#include <Arduino.h>
#include <esp_log.h>
#include "cellular_connection_service.h"
#include "cellular_tcp_client.h"
#include "cellular_http_client.h"
#include "cellular_firebase_client.h"
#include "firebase_command_queue.h"

// Test configuration
#define TEST_APN "v-internet"
#define TEST_SIGNAL_THRESHOLD 10

// Firebase Configuration
#define FIREBASE_HOST "kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"
#define GATEWAY_ID "GW_TEST_001"

// Command queue configuration
#define POLL_INTERVAL_MS 10000  // Poll every 10 seconds for testing

// Global objects
CellularConnectionService* cellularConn = nullptr;
CellularTCPClient* tcpClient = nullptr;
CellularHTTPClient* httpClient = nullptr;
CellularFirebaseClient* firebaseClient = nullptr;
FirebaseCommandQueue* commandQueue = nullptr;

// Test state
bool setupCompleted = false;
uint32_t lastStatusUpload = 0;

const char* TAG = "PHASE6_TEST";

// ===== Event Handlers =====

void onCellularEvent(CellularConnectionService::Event event, int8_t rssi) {
    switch (event) {
        case CellularConnectionService::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 Cellular CONNECTED (RSSI: %d)", rssi);
            break;
        
        case CellularConnectionService::Event::DISCONNECTED:
            ESP_LOGW(TAG, "⚠️  Cellular DISCONNECTED");
            break;
        
        case CellularConnectionService::Event::RECONNECTING:
            ESP_LOGI(TAG, "🔄 Cellular RECONNECTING...");
            break;
        
        case CellularConnectionService::Event::CONNECTION_FAILED:
            ESP_LOGE(TAG, "❌ Cellular CONNECTION FAILED");
            break;
        
        default:
            break;
    }
}

// ===== Command Handlers =====

bool handleRebootCommand(const FirebaseCommandQueue::Command& cmd) {
    ESP_LOGI(TAG, ">>> REBOOT COMMAND RECEIVED");
    ESP_LOGI(TAG, "   Command ID: %s", cmd.id.c_str());
    ESP_LOGI(TAG, "   Timestamp: %u", cmd.timestamp);
    
    // In real implementation, would reboot after delay
    ESP_LOGI(TAG, "   Scheduling reboot in 5 seconds...");
    
    // For test, we'll just simulate success
    ESP_LOGI(TAG, "✅ Reboot command acknowledged (simulated)");
    
    // In production: esp_restart();
    
    return true;
}

bool handleGetStatusCommand(const FirebaseCommandQueue::Command& cmd) {
    ESP_LOGI(TAG, ">>> GET_STATUS COMMAND RECEIVED");
    ESP_LOGI(TAG, "   Command ID: %s", cmd.id.c_str());
    
    // Upload current gateway status immediately
    auto result = firebaseClient->uploadGatewayStatus(
        3,                          // Nodes count
        200,                        // Packets RX
        195,                        // Packets TX
        -52,                        // Cellular RSSI
        ESP.getFreeHeap(),          // Free heap
        millis() / 1000             // Uptime
    );
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ Status uploaded successfully");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ Status upload failed: %s", result.message.c_str());
        return false;
    }
}

bool handleClearRoutesCommand(const FirebaseCommandQueue::Command& cmd) {
    ESP_LOGI(TAG, ">>> CLEAR_ROUTES COMMAND RECEIVED");
    ESP_LOGI(TAG, "   Command ID: %s", cmd.id.c_str());
    
    // Upload empty routing table
    String emptyRoutes = "{}";
    auto result = firebaseClient->uploadRoutingTable(emptyRoutes);
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ Routing table cleared");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ Failed to clear routes: %s", result.message.c_str());
        return false;
    }
}

bool handleUpdateConfigCommand(const FirebaseCommandQueue::Command& cmd) {
    ESP_LOGI(TAG, ">>> UPDATE_CONFIG COMMAND RECEIVED");
    ESP_LOGI(TAG, "   Command ID: %s", cmd.id.c_str());
    
    // Print parameters
    ESP_LOGI(TAG, "   Parameters:");
    for (const auto& param : cmd.params) {
        ESP_LOGI(TAG, "     %s = %s", param.first.c_str(), param.second.c_str());
    }
    
    // In real implementation, would update NVS config
    ESP_LOGI(TAG, "✅ Config updated (simulated)");
    
    return true;
}

bool handleSyncTimeCommand(const FirebaseCommandQueue::Command& cmd) {
    ESP_LOGI(TAG, ">>> SYNC_TIME COMMAND RECEIVED");
    ESP_LOGI(TAG, "   Command ID: %s", cmd.id.c_str());
    
    // Check if timestamp parameter exists
    if (cmd.params.count("timestamp") > 0) {
        uint32_t serverTime = cmd.params.at("timestamp").toInt();
        ESP_LOGI(TAG, "   Server timestamp: %u", serverTime);
        
        // In real implementation, would set system time
        ESP_LOGI(TAG, "✅ Time synchronized (simulated)");
        return true;
    } else {
        ESP_LOGW(TAG, "❌ Missing timestamp parameter");
        return false;
    }
}

// ===== Helper Functions =====

void printCommandQueueStats() {
    auto stats = commandQueue->getStats();
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ COMMAND QUEUE STATISTICS");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Total Polls:      %u", stats.totalPolls);
    ESP_LOGI(TAG, "│ Total Commands:   %u", stats.totalCommands);
    ESP_LOGI(TAG, "│ Completed:        %u", stats.completedCommands);
    ESP_LOGI(TAG, "│ Failed:           %u", stats.failedCommands);
    ESP_LOGI(TAG, "│ Pending:          %u", commandQueue->getPendingCount());
    ESP_LOGI(TAG, "│ Last Poll:        %us ago", (millis() - stats.lastPollTime) / 1000);
    if (stats.lastCommandTime > 0) {
        ESP_LOGI(TAG, "│ Last Command:     %us ago", (millis() - stats.lastCommandTime) / 1000);
    }
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

// ===== Setup & Loop =====

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 6 TEST: Firebase Command Queue");
    ESP_LOGI(TAG, "==========================================");
    
    // STEP 1: Initialize Cellular Connection
    ESP_LOGI(TAG, ">>> STEP 1: Initialize Cellular Connection");
    
    CellularConnectionService::APNConfig apnConfig;
    apnConfig.apn = TEST_APN;
    apnConfig.username = "";
    apnConfig.password = "";
    
    cellularConn = new CellularConnectionService(apnConfig);
    cellularConn->setSignalQualityThreshold(TEST_SIGNAL_THRESHOLD);
    cellularConn->onEvent(onCellularEvent);
    
    if (!cellularConn->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular initialization");
        return;
    }
    ESP_LOGI(TAG, "✅ PASSED: Cellular initialized");
    
    // STEP 2: Connect to Cellular Network
    ESP_LOGI(TAG, ">>> STEP 2: Connect to Cellular Network");
    if (!cellularConn->connect()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular connection");
        return;
    }
    ESP_LOGI(TAG, "✅ PASSED: Cellular connected");
    ESP_LOGI(TAG, "IP: %s", cellularConn->getIPAddress().c_str());
    
    // STEP 3: Create TCP Client
    ESP_LOGI(TAG, ">>> STEP 3: Create TCP Client");
    tcpClient = new CellularTCPClient(cellularConn->getATHandler());
    ESP_LOGI(TAG, "✅ PASSED: TCP client created");
    
    // STEP 4: Create HTTP Client
    ESP_LOGI(TAG, ">>> STEP 4: Create HTTP Client");
    httpClient = new CellularHTTPClient(tcpClient);
    ESP_LOGI(TAG, "✅ PASSED: HTTP client created");
    
    // STEP 5: Create Firebase Client
    ESP_LOGI(TAG, ">>> STEP 5: Create Firebase Client");
    firebaseClient = new CellularFirebaseClient(
        httpClient,
        FIREBASE_HOST,
        FIREBASE_AUTH,
        GATEWAY_ID
    );
    
    if (!firebaseClient->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Firebase initialization");
        return;
    }
    ESP_LOGI(TAG, "✅ PASSED: Firebase client created");
    
    // STEP 6: Create Command Queue
    ESP_LOGI(TAG, ">>> STEP 6: Create Command Queue");
    commandQueue = new FirebaseCommandQueue(firebaseClient, POLL_INTERVAL_MS);
    
    if (!commandQueue->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Command queue initialization");
        return;
    }
    ESP_LOGI(TAG, "✅ PASSED: Command queue created");
    
    // STEP 7: Register Command Handlers
    ESP_LOGI(TAG, ">>> STEP 7: Register Command Handlers");
    commandQueue->onCommand(FirebaseCommandQueue::CommandType::REBOOT, handleRebootCommand);
    commandQueue->onCommand(FirebaseCommandQueue::CommandType::GET_STATUS, handleGetStatusCommand);
    commandQueue->onCommand(FirebaseCommandQueue::CommandType::CLEAR_ROUTES, handleClearRoutesCommand);
    commandQueue->onCommand(FirebaseCommandQueue::CommandType::UPDATE_CONFIG, handleUpdateConfigCommand);
    commandQueue->onCommand(FirebaseCommandQueue::CommandType::SYNC_TIME, handleSyncTimeCommand);
    ESP_LOGI(TAG, "✅ PASSED: All handlers registered");
    
    // STEP 8: Upload initial status
    ESP_LOGI(TAG, ">>> STEP 8: Upload Initial Status");
    auto result = firebaseClient->uploadGatewayStatus(
        3, 150, 145, -55, ESP.getFreeHeap(), millis() / 1000
    );
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ PASSED: Initial status uploaded");
    } else {
        ESP_LOGW(TAG, "⚠️  Initial status upload failed (continuing anyway)");
    }
    
    // Setup complete
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "✅ SETUP COMPLETE - Waiting for commands");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📝 HOW TO TEST:");
    ESP_LOGI(TAG, "1. Go to Firebase Console");
    ESP_LOGI(TAG, "2. Navigate to: /commands/%s/pending", GATEWAY_ID);
    ESP_LOGI(TAG, "3. Add new command:");
    ESP_LOGI(TAG, "   Key: cmd_test_001");
    ESP_LOGI(TAG, "   Value: {\"type\":\"GET_STATUS\",\"timestamp\":123456}");
    ESP_LOGI(TAG, "4. Watch logs for execution");
    ESP_LOGI(TAG, "5. Check /commands/%s/completed for result", GATEWAY_ID);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⏱️  Polling every %d seconds...", POLL_INTERVAL_MS / 1000);
    ESP_LOGI(TAG, "==========================================");
    
    setupCompleted = true;
    lastStatusUpload = millis();
}

void loop() {
    if (!setupCompleted) {
        delay(100);
        return;
    }

    // Update cellular connection (auto-reconnect)
    if (cellularConn) {
        cellularConn->update();
    }

    // Update command queue (poll and execute commands)
    if (commandQueue) {
        commandQueue->update();
    }

    // Upload status every 60 seconds
    if (millis() - lastStatusUpload > 60000) {
        ESP_LOGI(TAG, "📤 Uploading periodic status...");
        
        auto result = firebaseClient->uploadGatewayStatus(
            3, 200, 195, -52, ESP.getFreeHeap(), millis() / 1000
        );
        
        if (result.success()) {
            ESP_LOGI(TAG, "✅ Status uploaded");
        }
        
        lastStatusUpload = millis();
    }

    // Print statistics every 30 seconds
    static uint32_t lastStatsPrint = 0;
    if (millis() - lastStatsPrint > 30000) {
        printCommandQueueStats();
        lastStatsPrint = millis();
    }

    delay(100);
}
