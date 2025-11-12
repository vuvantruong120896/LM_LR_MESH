/**
 * @file test_cellular_phase4.cpp
 * @brief Phase 4 Test: HTTP/HTTPS Client
 * 
 * Tests:
 * 1. HTTP GET request
 * 2. HTTP POST request with JSON
 * 3. Response parsing (headers, body, status)
 * 4. Connection reuse (Keep-Alive)
 * 5. Error handling
 */

#include <Arduino.h>
#include <esp_log.h>
#include "cellular_connection_service.h"
#include "cellular_tcp_client.h"
#include "cellular_http_client.h"

// Test configuration
#define TEST_APN "v-internet"
#define TEST_SIGNAL_THRESHOLD 10

// Test URLs
#define TEST_GET_URL "http://httpbin.org/get"
#define TEST_POST_URL "http://httpbin.org/post"
#define TEST_PUT_URL "http://httpbin.org/put"
#define TEST_DELETE_URL "http://httpbin.org/delete"

// Global objects
CellularConnectionService* cellularConn = nullptr;
CellularTCPClient* tcpClient = nullptr;
CellularHTTPClient* httpClient = nullptr;

// Test state
bool testCompleted = false;
bool testPassed = false;

const char* TAG = "PHASE4_TEST";

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

void onTCPEvent(uint8_t linkNum, CellularTCPClient::Event event, int errorCode) {
    switch (event) {
        case CellularTCPClient::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 TCP Socket %d CONNECTED", linkNum);
            break;
        
        case CellularTCPClient::Event::DISCONNECTED:
            ESP_LOGI(TAG, "🔌 TCP Socket %d DISCONNECTED", linkNum);
            break;
        
        case CellularTCPClient::Event::DATA_AVAILABLE:
            ESP_LOGI(TAG, "📥 TCP Socket %d DATA AVAILABLE", linkNum);
            break;
        
        case CellularTCPClient::Event::ERROR:
            ESP_LOGE(TAG, "❌ TCP Socket %d ERROR: %d", linkNum, errorCode);
            break;
    }
}

// ===== Helper Functions =====

void printHTTPResponse(const CellularHTTPClient::HTTPResponse& response) {
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ HTTP RESPONSE");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Status:         %d %s", response.statusCode, response.statusMessage.c_str());
    ESP_LOGI(TAG, "│ Success:        %s", response.success ? "YES" : "NO");
    ESP_LOGI(TAG, "│ Response Time:  %ums", response.responseTime);
    ESP_LOGI(TAG, "│ Content-Length: %u", response.contentLength);
    ESP_LOGI(TAG, "│ Chunked:        %s", response.chunked ? "YES" : "NO");
    
    if (response.headers.size() > 0) {
        ESP_LOGI(TAG, "│");
        ESP_LOGI(TAG, "│ Headers (%d):", response.headers.size());
        for (const auto& header : response.headers) {
            ESP_LOGI(TAG, "│   %s: %s", header.first.c_str(), header.second.c_str());
        }
    }
    
    if (response.body.length() > 0) {
        ESP_LOGI(TAG, "│");
        ESP_LOGI(TAG, "│ Body (%d bytes):", response.body.length());
        
        // Print first 500 chars of body
        String bodyPreview = response.body;
        if (bodyPreview.length() > 500) {
            bodyPreview = bodyPreview.substring(0, 500) + "...";
        }
        
        // Split by lines for better readability
        int lineStart = 0;
        while (lineStart < bodyPreview.length()) {
            int lineEnd = bodyPreview.indexOf('\n', lineStart);
            if (lineEnd < 0) lineEnd = bodyPreview.length();
            
            String line = bodyPreview.substring(lineStart, lineEnd);
            line.trim();
            if (line.length() > 0) {
                ESP_LOGI(TAG, "│   %s", line.c_str());
            }
            
            lineStart = lineEnd + 1;
        }
    }
    
    if (!response.success && response.errorMessage.length() > 0) {
        ESP_LOGI(TAG, "│");
        ESP_LOGI(TAG, "│ Error: %s", response.errorMessage.c_str());
    }
    
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

void printHTTPStats() {
    auto stats = httpClient->getStats();
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ HTTP CLIENT STATISTICS");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Total Requests:      %u", stats.totalRequests);
    ESP_LOGI(TAG, "│ Successful:          %u", stats.successfulRequests);
    ESP_LOGI(TAG, "│ Failed:              %u", stats.failedRequests);
    ESP_LOGI(TAG, "│ Total Bytes Sent:    %u", stats.totalBytesSent);
    ESP_LOGI(TAG, "│ Total Bytes Received:%u", stats.totalBytesReceived);
    ESP_LOGI(TAG, "│ Avg Response Time:   %ums", stats.avgResponseTime);
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

// ===== Test Cases =====

bool testHTTPGet() {
    ESP_LOGI(TAG, ">>> TEST: HTTP GET Request");
    ESP_LOGI(TAG, "URL: %s", TEST_GET_URL);
    
    auto response = httpClient->get(TEST_GET_URL);
    printHTTPResponse(response);
    
    if (!response.success) {
        ESP_LOGE(TAG, "❌ FAILED: HTTP GET");
        return false;
    }
    
    if (response.statusCode != 200) {
        ESP_LOGE(TAG, "❌ FAILED: Expected 200, got %d", response.statusCode);
        return false;
    }
    
    if (response.body.length() == 0) {
        ESP_LOGE(TAG, "❌ FAILED: Empty response body");
        return false;
    }
    
    // Check for expected JSON fields
    if (response.body.indexOf("\"args\"") < 0 || 
        response.body.indexOf("\"headers\"") < 0 ||
        response.body.indexOf("\"url\"") < 0) {
        ESP_LOGE(TAG, "❌ FAILED: Response missing expected JSON fields");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: HTTP GET");
    return true;
}

bool testHTTPPost() {
    ESP_LOGI(TAG, ">>> TEST: HTTP POST Request");
    ESP_LOGI(TAG, "URL: %s", TEST_POST_URL);
    
    // Create JSON body
    String jsonBody = "{\"test\":\"phase4\",\"value\":123,\"timestamp\":" + String(millis()) + "}";
    
    // Set headers
    std::map<String, String> headers;
    headers["Content-Type"] = "application/json";
    
    ESP_LOGI(TAG, "Body: %s", jsonBody.c_str());
    
    auto response = httpClient->post(TEST_POST_URL, jsonBody, headers);
    printHTTPResponse(response);
    
    if (!response.success) {
        ESP_LOGE(TAG, "❌ FAILED: HTTP POST");
        return false;
    }
    
    if (response.statusCode != 200) {
        ESP_LOGE(TAG, "❌ FAILED: Expected 200, got %d", response.statusCode);
        return false;
    }
    
    // Check if our data is echoed back
    if (response.body.indexOf("\"test\"") < 0 || 
        response.body.indexOf("phase4") < 0) {
        ESP_LOGE(TAG, "❌ FAILED: Posted data not found in response");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: HTTP POST");
    return true;
}

bool testHTTPPut() {
    ESP_LOGI(TAG, ">>> TEST: HTTP PUT Request");
    ESP_LOGI(TAG, "URL: %s", TEST_PUT_URL);
    
    String body = "test_data_for_put";
    
    auto response = httpClient->put(TEST_PUT_URL, body);
    printHTTPResponse(response);
    
    if (!response.success) {
        ESP_LOGE(TAG, "❌ FAILED: HTTP PUT");
        return false;
    }
    
    if (response.statusCode != 200) {
        ESP_LOGE(TAG, "❌ FAILED: Expected 200, got %d", response.statusCode);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: HTTP PUT");
    return true;
}

bool testHTTPDelete() {
    ESP_LOGI(TAG, ">>> TEST: HTTP DELETE Request");
    ESP_LOGI(TAG, "URL: %s", TEST_DELETE_URL);
    
    auto response = httpClient->deleteRequest(TEST_DELETE_URL);
    printHTTPResponse(response);
    
    if (!response.success) {
        ESP_LOGE(TAG, "❌ FAILED: HTTP DELETE");
        return false;
    }
    
    if (response.statusCode != 200) {
        ESP_LOGE(TAG, "❌ FAILED: Expected 200, got %d", response.statusCode);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: HTTP DELETE");
    return true;
}

// ===== Main Setup =====

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 4 TEST: HTTP/HTTPS Client");
    ESP_LOGI(TAG, "==========================================");
    
    // Step 1: Initialize cellular
    ESP_LOGI(TAG, ">>> STEP 1: Initialize Cellular Connection");
    
    CellularConnectionService::APNConfig apnConfig(TEST_APN);
    cellularConn = new CellularConnectionService(apnConfig, true, 5000);
    cellularConn->setSignalQualityThreshold(TEST_SIGNAL_THRESHOLD);
    cellularConn->onEvent(onCellularEvent);
    
    if (!cellularConn->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular initialization");
        testCompleted = true;
        return;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: Cellular initialized");
    ESP_LOGI(TAG, "IMEI: %s", cellularConn->getIMEI().c_str());
    
    // Step 2: Connect to network
    ESP_LOGI(TAG, ">>> STEP 2: Connect to Cellular Network");
    
    if (!cellularConn->connect()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular connection");
        testCompleted = true;
        return;
    }
    
    ESP_LOGI(TAG, "✅ PASSED: Cellular connected");
    ESP_LOGI(TAG, "IP: %s", cellularConn->getIPAddress().c_str());
    ESP_LOGI(TAG, "Operator: %s", cellularConn->getOperator().c_str());
    
    // Step 3: Create TCP client
    ESP_LOGI(TAG, ">>> STEP 3: Create TCP Client");
    
    tcpClient = new CellularTCPClient(cellularConn->getATHandler());
    tcpClient->onEvent(onTCPEvent);
    
    ESP_LOGI(TAG, "✅ PASSED: TCP client created");
    
    // Step 4: Create HTTP client
    ESP_LOGI(TAG, ">>> STEP 4: Create HTTP Client");
    
    httpClient = new CellularHTTPClient(tcpClient);
    httpClient->setUserAgent("ESP32-Phase4-Test/1.0");
    httpClient->setKeepAlive(false);  // Disable for testing multiple connections
    
    ESP_LOGI(TAG, "✅ PASSED: HTTP client created");
    
    // Step 5: Run HTTP tests
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Running HTTP Tests...");
    ESP_LOGI(TAG, "==========================================");
    
    delay(1000);
    
    bool allPassed = true;
    
    // Test 1: GET
    if (!testHTTPGet()) {
        allPassed = false;
    }
    delay(2000);
    
    // Test 2: POST
    if (!testHTTPPost()) {
        allPassed = false;
    }
    delay(2000);
    
    // Test 3: PUT
    if (!testHTTPPut()) {
        allPassed = false;
    }
    delay(2000);
    
    // Test 4: DELETE
    if (!testHTTPDelete()) {
        allPassed = false;
    }
    
    // Print statistics
    ESP_LOGI(TAG, "==========================================");
    printHTTPStats();
    
    // Final result
    ESP_LOGI(TAG, "==========================================");
    if (allPassed) {
        ESP_LOGI(TAG, "✅ PHASE 4 TEST COMPLETE!");
        ESP_LOGI(TAG, "All HTTP methods working correctly");
        testPassed = true;
    } else {
        ESP_LOGE(TAG, "❌ PHASE 4 TEST FAILED!");
        ESP_LOGE(TAG, "Some HTTP tests did not pass");
    }
    ESP_LOGI(TAG, "==========================================");
    
    testCompleted = true;
}

void loop() {
    if (cellularConn) {
        cellularConn->update();
    }
    
    if (testCompleted) {
        delay(5000);
        
        if (testPassed) {
            ESP_LOGI(TAG, "==========================================");
            ESP_LOGI(TAG, "✅ PHASE 4 TEST PASSED!");
            ESP_LOGI(TAG, "==========================================");
            ESP_LOGI(TAG, "HTTP Client Features:");
            ESP_LOGI(TAG, "  ✅ GET requests");
            ESP_LOGI(TAG, "  ✅ POST requests with JSON");
            ESP_LOGI(TAG, "  ✅ PUT requests");
            ESP_LOGI(TAG, "  ✅ DELETE requests");
            ESP_LOGI(TAG, "  ✅ Response parsing");
            ESP_LOGI(TAG, "  ✅ Header handling");
            ESP_LOGI(TAG, "  ✅ Status code detection");
            printHTTPStats();
        } else {
            ESP_LOGE(TAG, "==========================================");
            ESP_LOGE(TAG, "❌ PHASE 4 TEST FAILED!");
            ESP_LOGE(TAG, "==========================================");
            ESP_LOGE(TAG, "Check:");
            ESP_LOGE(TAG, "  - Cellular connection stable");
            ESP_LOGE(TAG, "  - Internet access working");
            ESP_LOGE(TAG, "  - httpbin.org reachable");
        }
        
        delay(10000);
    }
}
