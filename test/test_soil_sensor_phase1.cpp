/**
 * @file test_soil_sensor_phase1.cpp
 * @brief Phase 1 Test: Soil Sensor Startup Initialization Sequence
 * 
 * This test validates:
 * - RS485 UART initialization
 * - Modbus RTU driver setup
 * - Soil Sensor Service initialization
 * - Phase 1 Startup Sequence:
 *   - Read device version from register 0x07D0
 *   - Read sensor status from register 0x0023
 *   - Read sensor ID from register 0x0024
 * 
 * Hardware Required:
 * - ESP32-S3
 * - RS485 adapter (SN65HVD78DR)
 * - Soil sensor connected to RS485 bus
 * - Connections:
 *   - TX: GPIO 21 -> RS485 D pin
 *   - RX: GPIO 20 -> RS485 R pin
 *   - DE: GPIO 42 -> RS485 DE pin
 *   - 120Ω termination resistors on A/B
 * 
 * Expected Output:
 * - RS485 initialized at 9600 bps
 * - Modbus driver ready
 * - Device version: 0x07D0 read successfully
 * - Sensor status: 0x0023 read successfully
 * - Sensor ID: 0x0024 read successfully
 * - All values logged with proper CRC validation
 */

#include <Arduino.h>
#include "rs485_config.h"
#include "soil_sensor_service.h"

#define TEST_TAG "SOIL_PHASE1"

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial monitor

    Serial.println("\n\n==========================================");
    Serial.println("PHASE 1 TEST: Soil Sensor Startup Sequence");
    Serial.println("==========================================\n");

    // ===== STEP 1: Initialize Soil Sensor Service =====
    Serial.println(">>> STEP 1: Initialize Soil Sensor Service");
    Serial.println("Initializing Modbus RTU driver...");
    
    if (!SoilSensorService::initialize()) {
        Serial.println("❌ FAILED: Service initialization");
        Serial.printf("Error: %s\n", SoilSensorService::getLastErrorDescription());
        while (1) delay(1000);
    }
    Serial.println("✅ PASSED: Soil Sensor Service initialized");
    Serial.printf("   Modbus Slave: 0x%04X\n", MODBUS_SLAVE_ADDRESS);
    Serial.printf("   Baud Rate: %u bps\n", MODBUS_BAUD_RATE);
    Serial.printf("   RS485 Pins: TX=%d, RX=%d, DE=%d\n\n", RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);

    // ===== STEP 2: Perform Phase 1 Startup Sequence =====
    Serial.println(">>> STEP 2: Phase 1 Startup Initialization Sequence");
    Serial.println("Sending startup queries to sensor...\n");
    
    if (!SoilSensorService::performStartupSequence()) {
        Serial.println("❌ FAILED: Startup sequence incomplete");
        Serial.printf("Error: %s\n", SoilSensorService::getLastErrorDescription());
        Serial.println("\nPossible issues:");
        Serial.println("  1. Check RS485 connections (DE, TX, RX pins)");
        Serial.println("  2. Verify sensor is connected to RS485 bus");
        Serial.println("  3. Check 120Ω termination resistors");
        Serial.println("  4. Verify soil sensor address is 0x01");
        while (1) delay(1000);
    }

    // ===== STEP 3: Detailed Info =====
    Serial.println("\n>>> STEP 3: Phase 1 Verification Complete");
    Serial.println("✅ All startup queries successful!");
    Serial.println("\nNext step: Phase 2 - Measurement Trigger");
    Serial.println("  Trigger measurement via register 0x0009");
    Serial.println("\nNext step: Phase 3 - Data Readout");
    Serial.println("  Read all 7 soil parameters from 0x0000-0x0006\n");

    SoilSensorService::printStatus();

    Serial.println("\n✅ PHASE 1 TEST COMPLETE - Ready for measurement!\n");
}

void loop() {
    delay(1000);
}
