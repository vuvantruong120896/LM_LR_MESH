/**
 * @file test_cellular_phase1.cpp
 * @brief Phase 1 Test: UART Driver & AT Command Handler
 * 
 * This test validates:
 * - UART initialization (TX=IO40, RX=IO41, PWR=IO39)
 * - Power control
 * - Basic AT commands (AT, ATE0, AT+CPIN?, AT+CSQ, AT+CREG?)
 * - Response parsing
 * 
 * Hardware Required:
 * - ESP32
 * - A7682S module
 * - Connections: TX->IO40, RX->IO41, PWR->IO39, NET->IO38
 * - SIM card inserted
 * 
 * Expected Output:
 * - Module powers on
 * - AT test passes
 * - SIM ready
 * - Signal quality reported
 * - Network registration status
 */

#include <Arduino.h>
#include "cellular_uart.h"
#include "at_command_handler.h"

// Global instances
CellularUART* cellularUart = nullptr;
ATCommandHandler* atHandler = nullptr;

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor

    Serial.println("\n\n=================================");
    Serial.println("PHASE 1 TEST: UART & AT Commands");
    Serial.println("=================================\n");

    // ===== STEP 1: Initialize UART =====
    Serial.println(">>> STEP 1: Initialize UART");
    cellularUart = new CellularUART();  // Use default config
    
    if (!cellularUart->initialize()) {
        Serial.println("❌ FAILED: UART initialization");
        while (1) delay(1000);
    }
    Serial.println("✅ PASSED: UART initialized\n");

    // ===== STEP 2: Power On Module =====
    Serial.println(">>> STEP 2: Power On Module");
    Serial.println("Powering on A7682S (wait 5 seconds)...");
    
    if (!cellularUart->powerOn(5000)) {
        Serial.println("❌ FAILED: Power on");
        while (1) delay(1000);
    }
    Serial.println("✅ PASSED: Module powered on\n");

    // ===== STEP 3: Create AT Handler =====
    Serial.println(">>> STEP 3: Create AT Command Handler");
    atHandler = new ATCommandHandler(cellularUart);
    Serial.println("✅ PASSED: AT handler created\n");

    // ===== STEP 4: Test AT Communication =====
    Serial.println(">>> STEP 4: Test AT Communication");
    Serial.println("Sending AT command (3 retries)...");
    
    if (!atHandler->testAT(3)) {
        Serial.println("❌ FAILED: AT test (module not responding)");
        Serial.println("Check connections:");
        Serial.println("  - TX (ESP32 IO40) -> RX (A7682S)");
        Serial.println("  - RX (ESP32 IO41) -> TX (A7682S)");
        Serial.println("  - PWR (ESP32 IO39) -> PWR_KEY (A7682S)");
        while (1) delay(1000);
    }
    Serial.println("✅ PASSED: Module responds to AT\n");

    // ===== STEP 5: Disable Echo =====
    Serial.println(">>> STEP 5: Disable Echo (ATE0)");
    ATCommandHandler::Response resp = atHandler->sendCommand("E0");
    
    if (!resp.success) {
        Serial.println("⚠️  WARNING: Failed to disable echo");
    } else {
        Serial.println("✅ PASSED: Echo disabled\n");
    }

    // ===== STEP 6: Check SIM Card =====
    Serial.println(">>> STEP 6: Check SIM Card (AT+CPIN?)");
    resp = atHandler->sendCommand("+CPIN?");
    
    if (!resp.success) {
        Serial.println("❌ FAILED: Cannot check SIM status");
        Serial.printf("Error: %s\n", resp.errorMessage.c_str());
    } else {
        Serial.printf("Response: %s\n", resp.data.c_str());
        
        if (resp.data.indexOf("READY") >= 0) {
            Serial.println("✅ PASSED: SIM card ready\n");
        } else if (resp.data.indexOf("SIM PIN") >= 0) {
            Serial.println("⚠️  WARNING: SIM PIN required\n");
        } else {
            Serial.println("❌ FAILED: SIM not ready\n");
        }
    }

    // ===== STEP 7: Check Signal Quality =====
    Serial.println(">>> STEP 7: Check Signal Quality (AT+CSQ)");
    resp = atHandler->sendCommand("+CSQ");
    
    if (!resp.success) {
        Serial.println("❌ FAILED: Cannot check signal quality");
    } else {
        Serial.printf("Response: %s\n", resp.data.c_str());
        
        // Parse CSQ response: "+CSQ: <rssi>,<ber>"
        String value = ATCommandHandler::extractValue(resp.data, "+CSQ:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            int rssi = parts[0].toInt();
            int ber = parts[1].toInt();
            
            Serial.printf("  RSSI: %d (0-31, 99=unknown)\n", rssi);
            Serial.printf("  BER: %d (0-7, 99=unknown)\n", ber);
            
            if (rssi >= 10 && rssi <= 31) {
                Serial.println("✅ PASSED: Good signal quality\n");
            } else if (rssi > 0 && rssi < 10) {
                Serial.println("⚠️  WARNING: Weak signal\n");
            } else {
                Serial.println("❌ FAILED: No signal\n");
            }
        }
    }

    // ===== STEP 8: Check Network Registration =====
    Serial.println(">>> STEP 8: Check Network Registration (AT+CREG?)");
    resp = atHandler->sendCommand("+CREG?");
    
    if (!resp.success) {
        Serial.println("❌ FAILED: Cannot check network registration");
    } else {
        Serial.printf("Response: %s\n", resp.data.c_str());
        
        // Parse CREG response: "+CREG: <n>,<stat>[,<lac>,<ci>]"
        String value = ATCommandHandler::extractValue(resp.data, "+CREG:");
        auto parts = ATCommandHandler::splitValues(value);
        
        if (parts.size() >= 2) {
            int n = parts[0].toInt();
            int stat = parts[1].toInt();
            
            Serial.printf("  Mode: %d\n", n);
            Serial.printf("  Status: %d ", stat);
            
            switch (stat) {
                case 0:
                    Serial.println("(not registered, not searching)");
                    break;
                case 1:
                    Serial.println("(registered, home network)");
                    Serial.println("✅ PASSED: Registered on home network\n");
                    break;
                case 2:
                    Serial.println("(not registered, searching)");
                    break;
                case 3:
                    Serial.println("(registration denied)");
                    break;
                case 5:
                    Serial.println("(registered, roaming)");
                    Serial.println("✅ PASSED: Registered on roaming network\n");
                    break;
                default:
                    Serial.println("(unknown)");
                    break;
            }
        }
    }

    // ===== STEP 9: Get IMEI =====
    Serial.println(">>> STEP 9: Get IMEI (AT+GSN)");
    resp = atHandler->sendCommand("+GSN");
    
    if (!resp.success) {
        Serial.println("❌ FAILED: Cannot get IMEI");
    } else {
        Serial.printf("IMEI: %s\n", resp.data.c_str());
        Serial.println("✅ PASSED: IMEI retrieved\n");
    }

    // ===== STEP 10: Get Operator =====
    Serial.println(">>> STEP 10: Get Operator (AT+COPS?)");
    resp = atHandler->sendCommand("+COPS?");
    
    if (!resp.success) {
        Serial.println("❌ FAILED: Cannot get operator");
    } else {
        Serial.printf("Operator: %s\n", resp.data.c_str());
        Serial.println("✅ PASSED: Operator info retrieved\n");
    }

    // ===== TEST COMPLETE =====
    Serial.println("\n=================================");
    Serial.println("PHASE 1 TEST COMPLETE!");
    Serial.println("=================================");
    Serial.println("\nNext Steps:");
    Serial.println("1. Review test results above");
    Serial.println("2. Ensure all tests PASSED");
    Serial.println("3. Ready for Phase 2: Cellular Connection Service\n");
}

void loop() {
    // Process URCs (unsolicited messages from module)
    atHandler->processURCs();
    
    // Periodic status check (every 30 seconds)
    static uint32_t lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        lastCheck = millis();
        
        Serial.println("\n--- Periodic Status Check ---");
        
        // Check signal quality
        ATCommandHandler::Response resp = atHandler->sendCommand("+CSQ");
        if (resp.success) {
            String value = ATCommandHandler::extractValue(resp.data, "+CSQ:");
            Serial.printf("Signal Quality: %s\n", value.c_str());
        }
        
        // Check network status
        resp = atHandler->sendCommand("+CREG?");
        if (resp.success) {
            String value = ATCommandHandler::extractValue(resp.data, "+CREG:");
            Serial.printf("Network Status: %s\n", value.c_str());
        }
        
        Serial.println("-----------------------------\n");
    }
    
    delay(100);
}
