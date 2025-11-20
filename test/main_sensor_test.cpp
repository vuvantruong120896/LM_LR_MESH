/**
 * @file main_sensor_test.cpp
 * @brief Main file for soil sensor 3-phase test
 * 
 * Tests the 3-phase sensor communication protocol:
 * - Phase 1: Startup initialization (read device version + sensor ID)
 * - Phase 2: Measurement trigger (activate sensor measurement)
 * - Phase 3: Data readout (read all 7 soil parameters)
 * 
 * This file is only compiled in esp32-sensor-test environment.
 */

#ifdef SENSOR_TEST_BUILD

#include <Arduino.h>

// Import test functions
extern void sensor_test_setup();
extern void sensor_test_phase1_startup();
extern void sensor_test_phase2_trigger();
extern void sensor_test_phase3_readout();

// =============================================================================
// TEST CONFIGURATION
// =============================================================================

// Run all 3 phases sequentially (automatically)
#define RUN_ALL_PHASES_SEQUENTIAL

// =============================================================================
// ARDUINO INTERFACE
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║      RS485 SOIL SENSOR - 3 PHASE TEST ENVIRONMENT          ║");
    Serial.println("║         Running All 3 Phases Sequentially                  ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println("");
    
    sensor_test_setup();
    
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("PHASE 1 TEST: Startup Initialization");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Read device version and sensor ID from registers:");
    Serial.println("  - REG 0x07D0: Device version");
    Serial.println("  - REG 0x0023 + 0x0024: Sensor ID (4-digit)");
    Serial.println("");
    
    sensor_test_phase1_startup();
    
    delay(2000);
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("PHASE 2 TEST: Measurement Trigger");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Trigger sensor measurement by writing to register:");
    Serial.println("  - REG 0x0009: Measurement control trigger");
    Serial.println("");
    
    sensor_test_phase2_trigger();
    
    delay(2000);
    Serial.println("PHASE 3 TEST: Data Readout");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Read all 7 soil parameters from registers:");
    Serial.println("  - REG 0x0000-0x0006: Moisture, Temperature, pH, EC, N, P, K");
    Serial.println("");
    
    sensor_test_phase3_readout();
    
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("✅ ALL 3 PHASES COMPLETE!");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Press RESET to run tests again.");
    Serial.println("═══════════════════════════════════════════════════════════");
}

void loop() {
    delay(60000); // Wait 1 minute, then allow reset
}

#endif // SENSOR_TEST_BUILD
