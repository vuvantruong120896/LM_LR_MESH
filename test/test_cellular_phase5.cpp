/**
 * @file test_cellular_phase5.cpp
 * @brief Phase 5 Test: Firebase Integration via Cellular
 * 
 * Tests:
 * 1. Firebase authentication
 * 2. Upload sensor data
 * 3. Upload gateway status
 * 4. Upload routing table
 * 5. Log events
 * 6. GET/DELETE operations
 */

#include <Arduino.h>
#include <esp_log.h>
#include "cellular_connection_service.h"
#include "cellular_tcp_client.h"
#include "cellular_http_client.h"
#include "cellular_firebase_client.h"

// Test configuration
#define TEST_APN "v-internet"
#define TEST_SIGNAL_THRESHOLD 10

// Firebase Configuration
// TODO: Replace with your Firebase details!
#define FIREBASE_HOST "kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"  // Database secret
#define GATEWAY_ID "GW_TEST_001"  // Or use MAC address

// Global objects
CellularConnectionService* cellularConn = nullptr;
CellularTCPClient* tcpClient = nullptr;
CellularHTTPClient* httpClient = nullptr;
CellularFirebaseClient* firebaseClient = nullptr;

// Test state
bool testCompleted = false;
bool testPassed = false;

const char* TAG = "PHASE5_TEST";

// ===== Event Handlers =====

void onCellularEvent(CellularConnectionService::Event event, int8_t rssi) {
    switch (event) {
        case CellularConnectionService::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 Cellular CONNECTED (RSSI: %d)", rssi);
            ESP_LOGI(TAG, "   IP: %s", cellularConn->getIPAddress().c_str());
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
        
        case CellularConnectionService::Event::SIGNAL_LOW:
            ESP_LOGW(TAG, "📶 Signal quality LOW (RSSI: %d)", rssi);
            break;
        
        default:
            break;
    }
}

// ===== Helper Functions =====

void printFirebaseResult(const char* testName, const CellularFirebaseClient::UploadResult& result) {
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ %s", testName);
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Status:        %s", result.success() ? "SUCCESS" : "FAILED");
    ESP_LOGI(TAG, "│ HTTP Code:     %d", result.httpCode);
    ESP_LOGI(TAG, "│ Response Time: %ums", result.responseTime);
    ESP_LOGI(TAG, "│ Message:       %s", result.message.c_str());
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

void printFirebaseStats() {
    auto stats = firebaseClient->getStats();
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ FIREBASE STATISTICS");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Total Uploads:     %u", stats.totalUploads);
    ESP_LOGI(TAG, "│ Successful:        %u", stats.successfulUploads);
    ESP_LOGI(TAG, "│ Failed:            %u", stats.failedUploads);
    ESP_LOGI(TAG, "│ Bytes Uploaded:    %u", stats.totalBytesUpload);
    ESP_LOGI(TAG, "│ Bytes Downloaded:  %u", stats.totalBytesDownload);
    ESP_LOGI(TAG, "│ Avg Response Time: %ums", stats.avgResponseTime);
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

// ===== Test Functions =====

bool testSensorDataUpload() {
    ESP_LOGI(TAG, ">>> TEST: Upload Sensor Data");
    
    // Upload sensor data from simulated node
    auto result = firebaseClient->uploadSensorData(
        "0x1234",      // Node ID
        25.5,          // Temperature
        60.0,          // Humidity
        -45,           // RSSI
        8.5            // SNR
    );
    
    printFirebaseResult("SENSOR DATA UPLOAD", result);
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ PASSED: Sensor data upload");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ FAILED: Sensor data upload");
        return false;
    }
}

bool testGatewayStatusUpload() {
    ESP_LOGI(TAG, ">>> TEST: Upload Gateway Status");
    
    // Upload gateway status
    auto result = firebaseClient->uploadGatewayStatus(
        3,                    // Nodes count
        150,                  // Packets RX
        145,                  // Packets TX
        -55,                  // Cellular RSSI
        ESP.getFreeHeap(),    // Free heap
        millis() / 1000       // Uptime (seconds)
    );
    
    printFirebaseResult("GATEWAY STATUS UPLOAD", result);
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ PASSED: Gateway status upload");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ FAILED: Gateway status upload");
        return false;
    }
}

bool testRoutingTableUpload() {
    ESP_LOGI(TAG, ">>> TEST: Upload Routing Table");
    
    // Simulated routing table JSON
    String routingTable = "{";
    routingTable += "\"0x1234\":{\"nextHop\":\"0x0000\",\"hopCount\":1,\"rssi\":-45},";
    routingTable += "\"0x5678\":{\"nextHop\":\"0x1234\",\"hopCount\":2,\"rssi\":-60},";
    routingTable += "\"0x9ABC\":{\"nextHop\":\"0x0000\",\"hopCount\":1,\"rssi\":-50}";
    routingTable += "}";
    
    auto result = firebaseClient->uploadRoutingTable(routingTable);
    
    printFirebaseResult("ROUTING TABLE UPLOAD", result);
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ PASSED: Routing table upload");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ FAILED: Routing table upload");
        return false;
    }
}

bool testEventLogging() {
    ESP_LOGI(TAG, ">>> TEST: Log Event");
    
    // Log a test event
    auto result = firebaseClient->logEvent(
        "node_joined",
        "0x1234",
        "New node joined the network"
    );
    
    printFirebaseResult("EVENT LOGGING", result);
    
    if (result.success()) {
        ESP_LOGI(TAG, "✅ PASSED: Event logging");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ FAILED: Event logging");
        return false;
    }
}

bool testReadData() {
    ESP_LOGI(TAG, ">>> TEST: Read Data (GET)");
    
    // Read gateway status back
    String result;
    auto uploadResult = firebaseClient->get("/gateways/" + String(GATEWAY_ID) + "/status", result);
    
    printFirebaseResult("READ DATA (GET)", uploadResult);
    
    if (uploadResult.success()) {
        ESP_LOGI(TAG, "Read data: %s", result.c_str());
        ESP_LOGI(TAG, "✅ PASSED: Read data");
        return true;
    } else {
        ESP_LOGE(TAG, "❌ FAILED: Read data");
        return false;
    }
}

// ===== Setup & Loop =====

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 5 TEST: Firebase Integration");
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
    ESP_LOGI(TAG, "IMEI: %s", cellularConn->getIMEI().c_str());
    
    // STEP 2: Connect to Cellular Network
    ESP_LOGI(TAG, ">>> STEP 2: Connect to Cellular Network");
    if (!cellularConn->connect()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular connection");
        return;
    }
    ESP_LOGI(TAG, "✅ PASSED: Cellular connected");
    ESP_LOGI(TAG, "IP: %s", cellularConn->getIPAddress().c_str());
    ESP_LOGI(TAG, "Operator: %s", cellularConn->getOperator().c_str());
    
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
    
    // STEP 6: Run Firebase Tests
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Running Firebase Tests...");
    ESP_LOGI(TAG, "==========================================");
    
    delay(1000);
    
    bool allPassed = true;
    
    allPassed &= testSensorDataUpload();
    delay(2000);
    
    allPassed &= testGatewayStatusUpload();
    delay(2000);
    
    allPassed &= testRoutingTableUpload();
    delay(2000);
    
    allPassed &= testEventLogging();
    delay(2000);
    
    allPassed &= testReadData();
    delay(2000);
    
    // Print statistics
    ESP_LOGI(TAG, "==========================================");
    printFirebaseStats();
    ESP_LOGI(TAG, "==========================================");
    
    // Final result
    if (allPassed) {
        ESP_LOGI(TAG, "🎉 PHASE 5 TEST PASSED!");
        ESP_LOGI(TAG, "==========================================");
        testPassed = true;
    } else {
        ESP_LOGE(TAG, "❌ PHASE 5 TEST FAILED!");
        ESP_LOGE(TAG, "==========================================");
        ESP_LOGE(TAG, "Check:");
        ESP_LOGE(TAG, "  - Cellular connection stable");
        ESP_LOGE(TAG, "  - Firebase host correct");
        ESP_LOGE(TAG, "  - Firebase auth secret valid");
        ESP_LOGE(TAG, "  - Firebase security rules allow write");
    }
    
    testCompleted = true;
}

void loop() {
    if (testCompleted) {
        if (!testPassed) {
            // Print error reminder every 15 seconds
            static uint32_t lastPrint = 0;
            if (millis() - lastPrint > 15000) {
                ESP_LOGE(TAG, "==========================================");
                ESP_LOGE(TAG, "❌ PHASE 5 TEST FAILED!");
                ESP_LOGE(TAG, "==========================================");
                ESP_LOGE(TAG, "Check:");
                ESP_LOGE(TAG, "  - Cellular connection stable");
                ESP_LOGE(TAG, "  - Internet access working");
                ESP_LOGE(TAG, "  - Firebase credentials correct");
                lastPrint = millis();
            }
        }
        
        // Update cellular connection (for auto-reconnect)
        if (cellularConn) {
            cellularConn->update();
        }
    }
    
    delay(100);
}
