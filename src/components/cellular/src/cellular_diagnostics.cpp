/**
 * @file cellular_diagnostics.cpp
 * @brief Diagnostic utilities for A7682S modem HTTPS connectivity
 */

#include "cellular_diagnostics.h"
#include "at_command_handler.h"
#include <esp_log.h>
#include <cstring>
#include <sstream>

static const char* TAG = "CELLULAR_DIAG";

CellularDiagnostics::TestResult CellularDiagnostics::testEchoService() {
    TestResult result;
    
    ESP_LOGI(TAG, "=== PHASE 1: Echo Service Test (HTTP) ===");
    ESP_LOGI(TAG, "Purpose: Verify modem can receive ANY data from HTTP service");
    ESP_LOGI(TAG, "Target: httpbin.org (public echo service)");
    
    HTTPRequest req;
    req.method = "GET";
    req.path = "/get";
    req.host = "httpbin.org";
    req.hostHeader = "httpbin.org";
    
    result = sendRawHTTPRequest("httpbin.org", 80, false, req);
    
    if (result.success) {
        ESP_LOGI(TAG, "✅ Echo service test PASSED");
        ESP_LOGI(TAG, "   - Modem CAN receive data");
        ESP_LOGI(TAG, "   - HTTP status: %d", result.httpStatus);
        ESP_LOGI(TAG, "   - Response time: %u ms", result.responseTime);
        ESP_LOGI(TAG, "   - Receive started at URC round: %d", result.urcRounds);
    } else {
        ESP_LOGE(TAG, "❌ Echo service test FAILED");
        ESP_LOGE(TAG, "   - Modem cannot receive ANY HTTP data");
        ESP_LOGE(TAG, "   - Error: %s", result.errorMessage.c_str());
        ESP_LOGE(TAG, "   - Connection time: %d ms", result.connectionTime);
        ESP_LOGE(TAG, "   - Send time: %d ms", result.sendTime);
        ESP_LOGE(TAG, "   - This indicates fundamental modem issue");
    }
    
    return result;
}

CellularDiagnostics::TestResult CellularDiagnostics::testFirebaseAuth(const std::string& authToken) {
    TestResult result;
    
    ESP_LOGI(TAG, "=== PHASE 2: Firebase Auth Test (HTTPS) ===");
    ESP_LOGI(TAG, "Purpose: Verify Firebase responds to auth token check");
    ESP_LOGI(TAG, "Target: Firebase REST API");
    
    // Build a simple Firebase auth check request
    HTTPRequest req;
    req.method = "GET";
    req.path = "/.json";  // Simple GET to root with token
    req.host = "kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app";
    req.hostHeader = "kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app";
    
    // Note: Auth token should be passed as query parameter in real implementation
    // For now, just test connectivity
    
    result = sendRawHTTPRequest(
        "kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app",
        443,
        true,  // Use SSL/TLS
        req
    );
    
    if (result.success) {
        ESP_LOGI(TAG, "✅ Firebase auth test PASSED");
        ESP_LOGI(TAG, "   - Firebase responds to requests");
        ESP_LOGI(TAG, "   - HTTP status: %d", result.httpStatus);
        ESP_LOGI(TAG, "   - Response time: %u ms", result.responseTime);
    } else {
        ESP_LOGE(TAG, "❌ Firebase auth test FAILED");
        ESP_LOGE(TAG, "   - Firebase not responding or rejecting requests");
        ESP_LOGE(TAG, "   - Error: %s", result.errorMessage.c_str());
        
        // Diagnostic hints
        if (result.connectionTime > 0 && result.sendTime > 0 && result.receiveTime == 0) {
            ESP_LOGE(TAG, "   - Connection OK, send OK, but NO response received");
            ESP_LOGE(TAG, "   - Firebase may be rejecting auth or blocking modem IP");
        }
    }
    
    return result;
}

bool CellularDiagnostics::runFullDiagnostic(const std::string& authToken) {
    ESP_LOGI(TAG, "\n");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║     A7682S Modem HTTPS Connectivity Diagnostic         ║");
    ESP_LOGI(TAG, "║     Date: 2025-11-12                                   ║");
    ESP_LOGI(TAG, "║     Purpose: Isolate Firebase connectivity issue       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "\n");
    
    // Phase 1: Echo service test
    TestResult phase1 = testEchoService();
    delay(2000);  // Wait before next test
    
    if (!phase1.success) {
        ESP_LOGE(TAG, "\n❌ DIAGNOSTIC RESULT: Modem cannot receive ANY data");
        ESP_LOGE(TAG, "   Root cause: Fundamental modem receive issue");
        ESP_LOGE(TAG, "   Actions:");
        ESP_LOGE(TAG, "   1. Check APN settings");
        ESP_LOGE(TAG, "   2. Verify cellular signal strength (currently: RSSI check needed)");
        ESP_LOGE(TAG, "   3. Try different network operator");
        ESP_LOGE(TAG, "   4. Update modem firmware");
        ESP_LOGE(TAG, "   5. Check for known A7682S bugs with bidirectional HTTPS");
        return false;
    }
    
    // Phase 2: Firebase test
    TestResult phase2 = testFirebaseAuth(authToken);
    delay(2000);
    
    if (!phase2.success) {
        ESP_LOGE(TAG, "\n⚠️ DIAGNOSTIC RESULT: Firebase not responding");
        ESP_LOGE(TAG, "   Modem CAN receive data (phase 1 passed)");
        ESP_LOGE(TAG, "   But Firebase is not sending responses");
        ESP_LOGE(TAG, "   Possible causes:");
        ESP_LOGE(TAG, "   1. Firebase auth token invalid or expired");
        ESP_LOGE(TAG, "   2. Firebase security rules blocking this modem IP");
        ESP_LOGE(TAG, "   3. Firebase regional endpoint not accessible");
        ESP_LOGE(TAG, "   4. Network ISP blocking traffic from this modem to Firebase");
        return false;
    }
    
    // All tests passed
    ESP_LOGI(TAG, "\n✅ DIAGNOSTIC RESULT: All tests passed!");
    ESP_LOGI(TAG, "   Modem connectivity is working correctly");
    ESP_LOGI(TAG, "   Firebase is accessible and responding");
    ESP_LOGI(TAG, "   Issue may be in request formatting or other application logic");
    
    return true;
}

std::string CellularDiagnostics::buildHTTPRequest(const HTTPRequest& req) {
    std::stringstream ss;
    
    // Request line
    ss << req.method << " " << req.path << " HTTP/1.1\r\n";
    
    // Headers
    ss << "Host: " << req.hostHeader << "\r\n";
    ss << "Connection: close\r\n";
    ss << "User-Agent: A7682S-Diagnostic/1.0\r\n";
    
    // Add body length if present
    if (!req.body.empty()) {
        ss << "Content-Length: " << req.body.length() << "\r\n";
        ss << "Content-Type: application/x-www-form-urlencoded\r\n";
    }
    
    // End headers
    ss << "\r\n";
    
    // Body if present
    if (!req.body.empty()) {
        ss << req.body;
    }
    
    return ss.str();
}

CellularDiagnostics::TestResult CellularDiagnostics::sendRawHTTPRequest(
    const std::string& hostname, uint16_t port, bool useSSL, const HTTPRequest& request) {
    
    TestResult result;
    uint32_t startTime = millis();
    
    ESP_LOGI(TAG, "Connecting to %s:%d (SSL: %s)", hostname.c_str(), port, useSSL ? "yes" : "no");
    
    // Get AT handler - would need to pass this in real implementation
    // For now, this is a skeleton
    
    // TODO: Implement actual diagnostic using ATCommandHandler
    // For now, return placeholder
    result.success = false;
    result.errorMessage = "Diagnostic not yet fully implemented";
    result.responseTime = millis() - startTime;
    
    return result;
}
