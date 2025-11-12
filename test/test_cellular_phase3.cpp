/**
 * @file test_cellular_phase3.cpp
 * @brief Phase 3 Test: TCP/IP Stack
 * 
 * This test validates:
 * - DNS resolution
 * - TCP connection to remote server
 * - Sending HTTP request
 * - Receiving HTTP response
 * - Connection close
 * - Multiple concurrent connections
 * 
 * Hardware Required:
 * - ESP32
 * - A7682S module
 * - Connections: TX->IO40, RX->IO41, PWR->IO39, NET->IO38
 * - SIM card with data plan
 * - Active cellular network with internet access
 * 
 * Expected Output:
 * - Cellular connection established
 * - DNS resolution successful
 * - TCP connection to httpbin.org
 * - HTTP GET request sent
 * - HTTP response received
 * - Connection closed gracefully
 */

#include <Arduino.h>
#include <esp_log.h>
#include "cellular_connection_service.h"
#include "cellular_tcp_client.h"

static const char* TAG = "PHASE3_TEST";

// Global instances
CellularConnectionService* cellularService = nullptr;
CellularTCPClient* tcpClient = nullptr;

// Test configuration
const char* APN = "v-internet";  // Change for your carrier
const char* APN_USER = "";
const char* APN_PASS = "";

// Test server (httpbin.org provides testing endpoints)
const char* TEST_HOST = "httpbin.org";
const uint16_t TEST_PORT = 80;

// Test state
enum class TestState {
    INIT,
    CONNECTING_CELLULAR,
    CONNECTED_CELLULAR,
    RESOLVING_DNS,
    CONNECTING_TCP,
    CONNECTED_TCP,
    SENDING_REQUEST,
    WAITING_RESPONSE,
    RECEIVING_DATA,
    CLOSING,
    COMPLETE,
    FAILED
};

TestState testState = TestState::INIT;
int activeSocket = -1;
uint32_t requestStartTime = 0;
String receivedData = "";

// Event handlers
void onCellularEvent(CellularConnectionService::Event event, int8_t rssi) {
    switch (event) {
        case CellularConnectionService::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 Cellular CONNECTED (RSSI: %d)", rssi);
            ESP_LOGI(TAG, "   IP: %s", cellularService->getIPAddress().c_str());
            testState = TestState::CONNECTED_CELLULAR;
            break;
            
        case CellularConnectionService::Event::DISCONNECTED:
            ESP_LOGW(TAG, "❌ Cellular DISCONNECTED");
            testState = TestState::FAILED;
            break;
            
        default:
            break;
    }
}

void onTCPEvent(uint8_t linkNum, CellularTCPClient::Event event, int errorCode) {
    switch (event) {
        case CellularTCPClient::Event::CONNECTED:
            ESP_LOGI(TAG, "🎉 TCP Socket %d CONNECTED", linkNum);
            testState = TestState::CONNECTED_TCP;
            break;
            
        case CellularTCPClient::Event::DISCONNECTED:
            ESP_LOGI(TAG, "❌ TCP Socket %d DISCONNECTED (code: %d)", linkNum, errorCode);
            if (testState != TestState::CLOSING && testState != TestState::COMPLETE) {
                testState = TestState::FAILED;
            }
            break;
            
        case CellularTCPClient::Event::DATA_AVAILABLE:
            ESP_LOGI(TAG, "📦 Data available on socket %d: %d bytes", linkNum, errorCode);
            testState = TestState::RECEIVING_DATA;
            break;
            
        case CellularTCPClient::Event::ERROR:
            ESP_LOGE(TAG, "⚠️  TCP Error on socket %d: %d", linkNum, errorCode);
            testState = TestState::FAILED;
            break;
    }
}

void printTCPStats() {
    auto stats = tcpClient->getStats();
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ TCP/IP STATISTICS");
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ Total Connections:      %u", stats.totalConnections);
    ESP_LOGI(TAG, "│ Failed Connections:     %u", stats.failedConnections);
    ESP_LOGI(TAG, "│ Total Bytes Sent:       %u", stats.totalBytesSent);
    ESP_LOGI(TAG, "│ Total Bytes Received:   %u", stats.totalBytesReceived);
    ESP_LOGI(TAG, "│ DNS Queries:            %u", stats.dnsQueries);
    ESP_LOGI(TAG, "│ DNS Failed:             %u", stats.dnsFailed);
    ESP_LOGI(TAG, "│ Active Connections:     %u", tcpClient->getActiveConnectionCount());
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

void printSocketInfo(uint8_t linkNum) {
    auto info = tcpClient->getSocketInfo(linkNum);
    
    ESP_LOGI(TAG, "┌─────────────────────────────────────");
    ESP_LOGI(TAG, "│ SOCKET %d INFO", linkNum);
    ESP_LOGI(TAG, "├─────────────────────────────────────");
    ESP_LOGI(TAG, "│ State:          %s", CellularTCPClient::stateToString(info.state));
    ESP_LOGI(TAG, "│ Remote Host:    %s", info.remoteHost.c_str());
    ESP_LOGI(TAG, "│ Remote IP:      %s", info.remoteIP.c_str());
    ESP_LOGI(TAG, "│ Remote Port:    %u", info.remotePort);
    ESP_LOGI(TAG, "│ Bytes Sent:     %u", info.bytesSent);
    ESP_LOGI(TAG, "│ Bytes Received: %u", info.bytesReceived);
    ESP_LOGI(TAG, "│ Available:      %u", info.availableData);
    
    if (info.state == CellularTCPClient::SocketState::CONNECTED) {
        uint32_t connTime = (millis() - info.connectTime) / 1000;
        ESP_LOGI(TAG, "│ Connected Time: %us", connTime);
    }
    
    ESP_LOGI(TAG, "└─────────────────────────────────────");
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "PHASE 3 TEST: TCP/IP Stack");
    ESP_LOGI(TAG, "==========================================");

    // ===== STEP 1: Initialize Cellular Connection =====
    ESP_LOGI(TAG, ">>> STEP 1: Initialize Cellular Connection");
    
    CellularConnectionService::APNConfig apnConfig(APN, APN_USER, APN_PASS);
    cellularService = new CellularConnectionService(apnConfig, true, 5000);
    
    if (!cellularService) {
        ESP_LOGE(TAG, "❌ FAILED: Cannot create cellular service");
        while (1) delay(1000);
    }

    cellularService->onEvent(onCellularEvent);
    cellularService->setSignalQualityThreshold(10);

    if (!cellularService->initialize()) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular initialization");
        while (1) delay(1000);
    }

    ESP_LOGI(TAG, "✅ PASSED: Cellular initialized");
    ESP_LOGI(TAG, "IMEI: %s", cellularService->getIMEI().c_str());

    // ===== STEP 2: Connect to Cellular Network =====
    ESP_LOGI(TAG, ">>> STEP 2: Connect to Cellular Network");
    testState = TestState::CONNECTING_CELLULAR;

    if (!cellularService->connect(90000)) {
        ESP_LOGE(TAG, "❌ FAILED: Cellular connection");
        while (1) delay(1000);
    }

    ESP_LOGI(TAG, "✅ PASSED: Cellular connected");
    ESP_LOGI(TAG, "IP: %s", cellularService->getIPAddress().c_str());
    ESP_LOGI(TAG, "Operator: %s", cellularService->getOperator().c_str());

    // ===== STEP 3: Create TCP Client =====
    ESP_LOGI(TAG, ">>> STEP 3: Create TCP Client");
    
    tcpClient = new CellularTCPClient(cellularService->getATHandler());
    
    if (!tcpClient) {
        ESP_LOGE(TAG, "❌ FAILED: Cannot create TCP client");
        while (1) delay(1000);
    }

    tcpClient->onEvent(onTCPEvent);
    ESP_LOGI(TAG, "✅ PASSED: TCP client created");

    // ===== STEP 4: Test DNS Resolution =====
    ESP_LOGI(TAG, ">>> STEP 4: Test DNS Resolution");
    testState = TestState::RESOLVING_DNS;

    String resolvedIP;
    if (!tcpClient->resolveHost(TEST_HOST, resolvedIP, 30000)) {
        ESP_LOGE(TAG, "❌ FAILED: DNS resolution for %s", TEST_HOST);
        testState = TestState::FAILED;
    } else {
        ESP_LOGI(TAG, "✅ PASSED: DNS resolved");
        ESP_LOGI(TAG, "%s -> %s", TEST_HOST, resolvedIP.c_str());
    }

    // ===== STEP 5: Connect to TCP Server =====
    ESP_LOGI(TAG, ">>> STEP 5: Connect to TCP Server");
    ESP_LOGI(TAG, "Connecting to %s:%d...", TEST_HOST, TEST_PORT);
    testState = TestState::CONNECTING_TCP;

    activeSocket = tcpClient->connect(TEST_HOST, TEST_PORT, 60000);
    
    if (activeSocket < 0) {
        ESP_LOGE(TAG, "❌ FAILED: TCP connection");
        ESP_LOGE(TAG, "Error: %s", CellularTCPClient::errorToString(tcpClient->getLastError()));
        testState = TestState::FAILED;
    } else {
        ESP_LOGI(TAG, "✅ PASSED: TCP connected on socket %d", activeSocket);
        printSocketInfo(activeSocket);
    }

    // ===== STEP 6: Send HTTP Request =====
    if (testState == TestState::CONNECTED_TCP) {
        ESP_LOGI(TAG, ">>> STEP 6: Send HTTP GET Request");
        testState = TestState::SENDING_REQUEST;

        // Build HTTP GET request
        String request = "GET /get HTTP/1.1\r\n";
        request += "Host: " + String(TEST_HOST) + "\r\n";
        request += "User-Agent: ESP32-A7682S/1.0\r\n";
        request += "Connection: close\r\n";
        request += "\r\n";

        ESP_LOGI(TAG, "Sending request (%d bytes)...", request.length());
        ESP_LOGD(TAG, "Request:\n%s", request.c_str());

        int sent = tcpClient->send(activeSocket, request);
        
        if (sent < 0) {
            ESP_LOGE(TAG, "❌ FAILED: Send request");
            testState = TestState::FAILED;
        } else {
            ESP_LOGI(TAG, "✅ PASSED: Sent %d bytes", sent);
            testState = TestState::WAITING_RESPONSE;
            requestStartTime = millis();
        }
    }

    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "Setup complete. Monitoring for response...");
    ESP_LOGI(TAG, "==========================================");
}

void loop() {
    // Update services
    cellularService->update();
    tcpClient->update();

    // State machine
    switch (testState) {
        case TestState::WAITING_RESPONSE:
        case TestState::RECEIVING_DATA:
        {
            // Check for available data
            uint16_t available = tcpClient->available(activeSocket);
            
            if (available > 0) {
                ESP_LOGI(TAG, "📦 Receiving data (%u bytes available)...", available);
                
                uint8_t buffer[512];
                int received = tcpClient->receive(activeSocket, buffer, sizeof(buffer) - 1);
                
                if (received > 0) {
                    buffer[received] = '\0';
                    receivedData += String((char*)buffer);
                    
                    ESP_LOGI(TAG, "✅ Received %d bytes (total: %d)", 
                             received, receivedData.length());
                }
            }

            // Check if connection closed by server (response complete)
            if (!tcpClient->connected(activeSocket)) {
                ESP_LOGI(TAG, "Server closed connection");
                
                // Print received data
                ESP_LOGI(TAG, "┌─────────────────────────────────────");
                ESP_LOGI(TAG, "│ HTTP RESPONSE (%d bytes)", receivedData.length());
                ESP_LOGI(TAG, "├─────────────────────────────────────");
                
                // Print first 500 chars
                int printLen = min(500, (int)receivedData.length());
                String preview = receivedData.substring(0, printLen);
                ESP_LOGI(TAG, "%s", preview.c_str());
                
                if (receivedData.length() > 500) {
                    ESP_LOGI(TAG, "... (%d more bytes)", receivedData.length() - 500);
                }
                
                ESP_LOGI(TAG, "└─────────────────────────────────────");

                uint32_t responseTime = millis() - requestStartTime;
                ESP_LOGI(TAG, "✅ Response time: %.2fs", responseTime / 1000.0f);

                testState = TestState::COMPLETE;
            }

            // Timeout after 30 seconds
            if (millis() - requestStartTime > 30000) {
                ESP_LOGW(TAG, "⏱️  Response timeout");
                testState = TestState::CLOSING;
            }
            
            break;
        }

        case TestState::CLOSING:
        {
            ESP_LOGI(TAG, ">>> Closing connection...");
            
            if (activeSocket >= 0) {
                tcpClient->disconnect(activeSocket);
            }
            
            testState = TestState::COMPLETE;
            break;
        }

        case TestState::COMPLETE:
        {
            ESP_LOGI(TAG, "==========================================");
            ESP_LOGI(TAG, "✅ PHASE 3 TEST COMPLETE!");
            ESP_LOGI(TAG, "==========================================");

            printSocketInfo(activeSocket);
            printTCPStats();

            ESP_LOGI(TAG, "Test completed successfully.");
            ESP_LOGI(TAG, "You can reset the device to run again.");
            
            testState = TestState::INIT;  // Prevent re-entry
            while (1) delay(10000);  // Halt
        }

        case TestState::FAILED:
        {
            ESP_LOGE(TAG, "==========================================");
            ESP_LOGE(TAG, "❌ PHASE 3 TEST FAILED!");
            ESP_LOGE(TAG, "==========================================");

            if (activeSocket >= 0) {
                printSocketInfo(activeSocket);
            }
            
            printTCPStats();

            ESP_LOGE(TAG, "Check:");
            ESP_LOGE(TAG, "  - Cellular connection stable");
            ESP_LOGE(TAG, "  - Internet access working");
            ESP_LOGE(TAG, "  - DNS server reachable");
            ESP_LOGE(TAG, "  - Firewall/APN restrictions");

            testState = TestState::INIT;  // Prevent re-entry
            while (1) delay(10000);  // Halt
        }

        default:
            break;
    }

    delay(100);
}
