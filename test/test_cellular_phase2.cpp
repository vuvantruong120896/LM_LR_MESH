/**
 * @file test_cellular_phase2.cpp
 * @brief Phase 2 Test: Cellular Connection Service
 * 
 * This test validates:
 * - Service initialization
 * - Network registration
 * - APN configuration
 * - GPRS/LTE attachment
 * - PDP context activation
 * - IP address assignment
 * - Auto-reconnect functionality
 * - Event callbacks
 * - Signal quality monitoring
 * 
 * Hardware Required:
 * - ESP32
 * - A7682S module
 * - Connections: TX->IO40, RX->IO41, PWR->IO39, NET->IO38
 * - SIM card with data plan
 * - Active cellular network
 * 
 * Expected Output:
 * - Module initializes
 * - Network registration successful
 * - PDP context activated
 * - IP address assigned
 * - Periodic status updates
 */

#include <Arduino.h>
#include <esp_log.h>
#include "cellular_connection_service.h"

static const char* TAG = "PHASE2_TEST";

// Global instance
CellularConnectionService* cellularService = nullptr;

// Test configuration
// ⚠️ IMPORTANT: Change this to your carrier's APN!
const char* APN = "v-internet";  // Vietnam: "v-internet" (Viettel), "m-wap" (Mobifone), "e-connect" (Vinaphone)
const char* APN_USER = "";       // Usually empty
const char* APN_PASS = "";       // Usually empty

// Event handler
void onCellularEvent(CellularConnectionService::Event event, int8_t rssi) {
    switch (event) {
        case CellularConnectionService::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 EVENT: CONNECTED (RSSI: %d)", rssi);
            ESP_LOGI(TAG, "   IP: %s", cellularService->getIPAddress().c_str());
            ESP_LOGI(TAG, "   Operator: %s", cellularService->getOperator().c_str());
            break;
            
        case CellularConnectionService::Event::DISCONNECTED:
            ESP_LOGW(TAG, "❌ EVENT: DISCONNECTED (RSSI: %d)", rssi);
            break;
            
        case CellularConnectionService::Event::RECONNECTING:
            ESP_LOGI(TAG, "🔄 EVENT: RECONNECTING (RSSI: %d)", rssi);
            break;
            
        case CellularConnectionService::Event::CONNECTION_FAILED:
            ESP_LOGE(TAG, "⚠️  EVENT: CONNECTION_FAILED (RSSI: %d)", rssi);
            break;
            
        case CellularConnectionService::Event::SIGNAL_LOW:
            ESP_LOGW(TAG, "⚠️  EVENT: SIGNAL_LOW (RSSI: %d)", rssi);
            break;
            
        case CellularConnectionService::Event::PDP_ACTIVATED:
            ESP_LOGI(TAG, "✅ EVENT: PDP_ACTIVATED (RSSI: %d)", rssi);
            break;
            
        case CellularConnectionService::Event::PDP_DEACTIVATED:
            ESP_LOGW(TAG, "⚠️  EVENT: PDP_DEACTIVATED (RSSI: %d)", rssi);
            break;
    }
}

void printStats() {
    auto stats = cellularService->getStats();
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ CONNECTION STATISTICS");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Connection Attempts:    %u", stats.connectionAttempts);
    ESP_LOGI(TAG, "│ Successful:             %u", stats.successfulConnections);
    ESP_LOGI(TAG, "│ Failed:                 %u", stats.failedConnections);
    ESP_LOGI(TAG, "│ Reconnect Attempts:     %u", stats.reconnectAttempts);
    ESP_LOGI(TAG, "│ Total Connected Time:   %.1f min", stats.totalConnectedTime / 60000.0f);
    ESP_LOGI(TAG, "│ Current RSSI:           %d (%s)", 
                  stats.currentRSSI,
                  stats.currentRSSI >= 20 ? "Excellent" :
                  stats.currentRSSI >= 15 ? "Good" :
                  stats.currentRSSI >= 10 ? "Fair" :
                  stats.currentRSSI >= 5 ? "Poor" : "Very Poor");
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 2 TEST: Cellular Connection Service");
    ESP_LOGI(TAG, "==========================================");

    // ===== STEP 1: Create Service =====
    ESP_LOGI(TAG, ">>> STEP 1: Create Cellular Connection Service");
    ESP_LOGI(TAG, "APN: %s", APN);
    
    CellularConnectionService::APNConfig apnConfig(APN, APN_USER, APN_PASS);
    cellularService = new CellularConnectionService(
        apnConfig,
        true,   // Auto-reconnect enabled
        5000    // Initial reconnect interval: 5 seconds
    );
    
    if (!cellularService) {
        ESP_LOGE(TAG, "❌ FAILED: Cannot create service");
        while (1) delay(1000);
    }
    ESP_LOGI(TAG, "✅ PASSED: Service created");

    // ===== STEP 2: Register Event Callback =====
    ESP_LOGI(TAG, ">>> STEP 2: Register Event Callback");
    cellularService->onEvent(onCellularEvent);
    ESP_LOGI(TAG, "✅ PASSED: Event callback registered");

    // ===== STEP 3: Set Signal Quality Threshold =====
    ESP_LOGI(TAG, ">>> STEP 3: Set Signal Quality Threshold");
    cellularService->setSignalQualityThreshold(10);  // Warn if RSSI < 10
    ESP_LOGI(TAG, "✅ PASSED: Threshold set to 10");

    // ===== STEP 4: Initialize Service =====
    ESP_LOGI(TAG, ">>> STEP 4: Initialize Service");
    ESP_LOGI(TAG, "This will:");
    ESP_LOGI(TAG, "  - Power on module");
    ESP_LOGI(TAG, "  - Test AT communication");
    ESP_LOGI(TAG, "  - Check SIM card");
    ESP_LOGI(TAG, "  - Get IMEI");
    
    if (!cellularService->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Service initialization");
        ESP_LOGE(TAG, "Check:");
        ESP_LOGE(TAG, "  - Hardware connections");
        ESP_LOGE(TAG, "  - SIM card inserted");
        ESP_LOGE(TAG, "  - Power supply (2A min)");
        while (1) delay(1000);
    }
    
    ESP_LOGI(TAG, "✅ PASSED: Service initialized");
    ESP_LOGI(TAG, "IMEI: %s", cellularService->getIMEI().c_str());
    ESP_LOGI(TAG, "Signal Quality: %d", cellularService->getSignalQuality());

    // ===== STEP 5: Connect to Network =====
    ESP_LOGI(TAG, ">>> STEP 5: Connect to Cellular Network");
    ESP_LOGI(TAG, "This will:");
    ESP_LOGI(TAG, "  - Wait for network registration (up to 60s)");
    ESP_LOGI(TAG, "  - Configure APN");
    ESP_LOGI(TAG, "  - Attach to GPRS/LTE");
    ESP_LOGI(TAG, "  - Activate PDP context");
    ESP_LOGI(TAG, "  - Get IP address");
    ESP_LOGI(TAG, "⏳ Please wait (this may take 30-90 seconds)...");
    
    if (!cellularService->connect(90000)) {  // 90 second timeout
        ESP_LOGE(TAG, "❌ FAILED: Network connection");
        ESP_LOGE(TAG, "Possible causes:");
        ESP_LOGE(TAG, "  - No cellular coverage");
        ESP_LOGE(TAG, "  - Weak signal (move to better location)");
        ESP_LOGE(TAG, "  - SIM card not activated");
        ESP_LOGE(TAG, "  - Incorrect APN settings");
        ESP_LOGE(TAG, "  - Account balance low");
        ESP_LOGI(TAG, "Service will auto-retry in background...");
    } else {
        ESP_LOGI(TAG, "✅ PASSED: Connected to network");
        
        // Print connection info
        ESP_LOGI(TAG, "┌─────────────────────────────────────");
        ESP_LOGI(TAG, "│ CONNECTION INFO");
        ESP_LOGI(TAG, "├─────────────────────────────────────");
        ESP_LOGI(TAG, "│ IMEI:       %s", cellularService->getIMEI().c_str());
        ESP_LOGI(TAG, "│ Operator:   %s", cellularService->getOperator().c_str());
        ESP_LOGI(TAG, "│ IP Address: %s", cellularService->getIPAddress().c_str());
        ESP_LOGI(TAG, "│ RSSI:       %d", cellularService->getSignalQuality());
        ESP_LOGI(TAG, "└─────────────────────────────────────");
    }

    // ===== TEST COMPLETE =====
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 2 SETUP COMPLETE!");
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "The service is now running in background.");
    ESP_LOGI(TAG, "Features active:");
    ESP_LOGI(TAG, "  ✅ Auto-reconnect on disconnection");
    ESP_LOGI(TAG, "  ✅ Periodic signal quality monitoring");
    ESP_LOGI(TAG, "  ✅ Event callbacks");
    ESP_LOGI(TAG, "  ✅ URC processing");
    ESP_LOGI(TAG, "Monitor serial output for updates...");
}

void loop() {
    // Update service (handles auto-reconnect, URC processing, etc.)
    cellularService->update();
    
    // Periodic status report (every 60 seconds)
    static uint32_t lastStatusReport = 0;
    if (millis() - lastStatusReport > 60000) {
        lastStatusReport = millis();
        
        ESP_LOGI(TAG, "====== PERIODIC STATUS REPORT ======");
        ESP_LOGI(TAG, "Timestamp: %lu ms (%.1f min uptime)", 
                      millis(), millis() / 60000.0f);
        ESP_LOGI(TAG, "Status: %s", 
                      cellularService->isConnected() ? "CONNECTED ✅" : "DISCONNECTED ❌");
        
        if (cellularService->isConnected()) {
            ESP_LOGI(TAG, "IP: %s", cellularService->getIPAddress().c_str());
            ESP_LOGI(TAG, "Operator: %s", cellularService->getOperator().c_str());
        }
        
        ESP_LOGI(TAG, "RSSI: %d", cellularService->getSignalQuality());
        ESP_LOGI(TAG, "Auto-reconnect: %s", 
                      cellularService->getAutoReconnect() ? "ON" : "OFF");
        
        printStats();
        ESP_LOGI(TAG, "====================================");
    }
    
    // Test reconnect (optional - uncomment to test)
    // Disconnect after 2 minutes to test auto-reconnect
    /*
    static bool testReconnect = false;
    if (!testReconnect && millis() > 120000) {
        testReconnect = true;
        ESP_LOGI(TAG, ">>> TESTING AUTO-RECONNECT");
        ESP_LOGI(TAG, "Manually disconnecting to test auto-reconnect...");
        cellularService->disconnect();
    }
    */
    
    delay(100);
}
