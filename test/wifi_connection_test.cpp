/**
 * @file wifi_connection_test.cpp
 * @brief Test program for WiFiConnectionService
 * 
 * This test demonstrates:
 * 1. Basic WiFi connection
 * 2. Auto-reconnect functionality
 * 3. Event callbacks
 * 4. RSSI monitoring
 * 5. Connection statistics
 * 
 * @note This is a standalone test - not part of the main application
 */

#include <Arduino.h>
#include "wifi_connection_service.h"

// ============================================================================
// CONFIGURATION - UPDATE THESE WITH YOUR WIFI CREDENTIALS
// ============================================================================

// WARNING: Do NOT commit these credentials to git!
// In production, use environment variables or NVS storage

#define WIFI_SSID     "YourWiFiSSID"        // <-- CHANGE THIS
#define WIFI_PASSWORD "YourWiFiPassword"    // <-- CHANGE THIS

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

WiFiConnectionService* wifiService = nullptr;

// ============================================================================
// EVENT CALLBACK
// ============================================================================

void onWiFiEvent(WiFiConnectionService::WiFiEvent event, int8_t rssi) {
    switch (event) {
        case WiFiConnectionService::WiFiEvent::CONNECTED:
            Serial.println("\n========================================");
            Serial.println("✅ WiFi CONNECTED!");
            Serial.printf("   IP Address: %s\n", wifiService->getLocalIP().c_str());
            Serial.printf("   MAC Address: %s\n", wifiService->getMACAddress().c_str());
            Serial.printf("   RSSI: %d dBm\n", rssi);
            Serial.println("========================================\n");
            break;
            
        case WiFiConnectionService::WiFiEvent::DISCONNECTED:
            Serial.println("\n========================================");
            Serial.println("❌ WiFi DISCONNECTED!");
            Serial.println("========================================\n");
            break;
            
        case WiFiConnectionService::WiFiEvent::RECONNECTING:
            Serial.println("\n⏳ WiFi RECONNECTING...");
            break;
            
        case WiFiConnectionService::WiFiEvent::CONNECTION_FAILED:
            Serial.println("\n❌ WiFi CONNECTION FAILED!");
            break;
            
        case WiFiConnectionService::WiFiEvent::RSSI_LOW:
            Serial.printf("\n⚠️  WiFi SIGNAL LOW! RSSI: %d dBm\n", rssi);
            break;
    }
}

// ============================================================================
// STATISTICS PRINTER
// ============================================================================

void printWiFiStats() {
    if (!wifiService) return;
    
    WiFiConnectionService::WiFiStats stats = wifiService->getStats();
    
    Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("📊 WiFi Connection Statistics");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.printf("  Connection Attempts:    %lu\n", stats.connectionAttempts);
    Serial.printf("  Successful Connections: %lu\n", stats.successfulConnections);
    Serial.printf("  Disconnections:         %lu\n", stats.disconnections);
    Serial.printf("  Reconnect Attempts:     %lu\n", stats.reconnectAttempts);
    Serial.printf("  Total Uptime:           %lu seconds\n", stats.uptimeSeconds);
    Serial.printf("  Current RSSI:           %d dBm\n", stats.currentRSSI);
    Serial.printf("  Average RSSI:           %d dBm\n", stats.averageRSSI);
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║   WiFiConnectionService Test Program   ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    
    // Check if WiFi credentials are set
    if (strcmp(WIFI_SSID, "YourWiFiSSID") == 0) {
        Serial.println("❌ ERROR: Please update WIFI_SSID and WIFI_PASSWORD!");
        Serial.println("   Edit this file and set your WiFi credentials.");
        while (1) {
            delay(1000);
        }
    }
    
    // Create WiFi service with auto-reconnect enabled
    Serial.println("📡 Creating WiFi service...");
    wifiService = new WiFiConnectionService(
        WIFI_SSID,
        WIFI_PASSWORD,
        true,  // auto-reconnect enabled
        1000   // initial reconnect interval: 1 second
    );
    
    // Register event callback
    Serial.println("📝 Registering event callback...");
    wifiService->onEvent(onWiFiEvent);
    
    // Set RSSI threshold for low signal warning
    wifiService->setRSSIThreshold(-75);  // Warn if RSSI < -75 dBm
    
    // Initialize WiFi service
    Serial.println("🔧 Initializing WiFi service...");
    if (!wifiService->initialize()) {
        Serial.println("❌ Failed to initialize WiFi service!");
        return;
    }
    
    // Connect to WiFi
    Serial.printf("🔌 Connecting to WiFi: %s\n", WIFI_SSID);
    Serial.println("   (timeout: 10 seconds)");
    
    if (wifiService->connect(10000)) {
        Serial.println("✅ Initial connection successful!");
    } else {
        Serial.println("⚠️  Initial connection failed, but auto-reconnect is enabled.");
        Serial.println("   The service will keep trying...");
    }
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
    // Update WiFi service (handles auto-reconnect and RSSI monitoring)
    wifiService->update();
    
    // Print statistics every 30 seconds
    static uint32_t lastStatsPrint = 0;
    if (millis() - lastStatsPrint >= 30000) {
        printWiFiStats();
        lastStatsPrint = millis();
    }
    
    // Print connection status every 5 seconds
    static uint32_t lastStatusPrint = 0;
    if (millis() - lastStatusPrint >= 5000) {
        if (wifiService->isConnected()) {
            Serial.printf("[%lu] ✅ Connected | IP: %s | RSSI: %d dBm\n",
                         millis() / 1000,
                         wifiService->getLocalIP().c_str(),
                         wifiService->getRSSI());
        } else {
            Serial.printf("[%lu] ❌ Disconnected | Status: %d\n",
                         millis() / 1000,
                         static_cast<int>(wifiService->getStatus()));
        }
        lastStatusPrint = millis();
    }
    
    // Small delay
    delay(100);
}

// ============================================================================
// TEST SCENARIOS
// ============================================================================

/*
 * TEST SCENARIO 1: Basic Connection
 * ----------------------------------
 * Expected behavior:
 * 1. Service initializes
 * 2. Connects to WiFi within 10 seconds
 * 3. CONNECTED event fires
 * 4. IP address printed
 * 5. RSSI monitored every 5 seconds
 * 
 * 
 * TEST SCENARIO 2: Auto-Reconnect (Exponential Backoff)
 * ------------------------------------------------------
 * Steps to test:
 * 1. Wait for connection
 * 2. Turn off WiFi router OR move ESP32 out of range
 * 3. Observe DISCONNECTED event
 * 4. Observe RECONNECTING events with increasing intervals:
 *    - 1st retry: 1 second
 *    - 2nd retry: 2 seconds
 *    - 3rd retry: 4 seconds
 *    - 4th retry: 8 seconds
 *    - ...up to max 60 seconds
 * 5. Turn router back on
 * 6. Observe successful reconnection
 * 7. Interval resets to 1 second
 * 
 * 
 * TEST SCENARIO 3: RSSI Low Signal Warning
 * -----------------------------------------
 * Steps to test:
 * 1. Connect to WiFi
 * 2. Slowly move ESP32 away from router
 * 3. When RSSI drops below -75 dBm, observe RSSI_LOW event
 * 
 * 
 * TEST SCENARIO 4: Statistics Tracking
 * -------------------------------------
 * Expected behavior:
 * 1. Statistics printed every 30 seconds
 * 2. Connection attempts incremented
 * 3. Uptime tracked correctly
 * 4. Average RSSI calculated (exponential moving average)
 * 
 * 
 * TEST SCENARIO 5: Manual Disconnect/Reconnect
 * ---------------------------------------------
 * Modify loop() to add:
 * 
 *   // Disconnect after 60 seconds
 *   static bool manualDisconnect = false;
 *   if (!manualDisconnect && millis() > 60000) {
 *       Serial.println("🔌 Manual disconnect test...");
 *       wifiService->disconnect();
 *       manualDisconnect = true;
 *   }
 * 
 * Expected behavior:
 * 1. Connects normally
 * 2. After 60 seconds, manually disconnects
 * 3. Auto-reconnect kicks in
 * 4. Reconnects within a few seconds
 */
