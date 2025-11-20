/**
 * @file test_soil_sensor_main.cpp
 * @brief PlatformIO test entry point for RS485 Soil Sensor testing
 * 
 * This is the main test file that PlatformIO uses to run sensor tests.
 * It provides a simple Arduino-style setup/loop interface for testing.
 * 
 * **Test Environment:** esp32-sensor-test
 * **Hardware:** ESP32-S3 with RS485 adapter on GPIO 20, 21, 42
 * 
 * **Build & Run:**
 * ```bash
 * # Build test environment
 * pio run -e esp32-sensor-test
 * 
 * # Upload and monitor
 * pio run -e esp32-sensor-test --target upload
 * pio device monitor
 * 
 * # Or combined
 * pio test -e esp32-sensor-test
 * ```
 * 
 * **Test Options (edit below):**
 * - TEST_MODE_SINGLE: Run one sensor read and stop
 * - TEST_MODE_CONTINUOUS: Run continuous reads forever
 * - TEST_MODE_DIAGNOSTIC: Show sensor diagnostics only
 */

#include <Arduino.h>

// Import test functions from test_soil_sensor.cpp
extern void sensor_test_setup();
extern void sensor_test_read_single();
extern void sensor_test_continuous(uint8_t numReads, uint8_t intervalSeconds);
extern void sensor_test_diagnostics();

// =============================================================================
// TEST CONFIGURATION (EDIT THIS)
// =============================================================================

// Choose test mode (uncomment ONE):
// #define TEST_MODE_SINGLE          // One read, then stop
#define TEST_MODE_CONTINUOUS      // Continuous reads forever
// #define TEST_MODE_DIAGNOSTIC      // Show diagnostics only

// Continuous test parameters:
#define CONTINUOUS_NUM_READS      10    // Number of reads per cycle
#define CONTINUOUS_INTERVAL_SEC   10    // Seconds between reads
#define CONTINUOUS_CYCLE_DELAY_MS 5000  // Delay between cycles (ms)

// =============================================================================
// MAIN TEST CODE
// =============================================================================

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);  // Wait for serial to stabilize
    
    Serial.println("");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║         RS485 SOIL SENSOR TEST ENVIRONMENT                 ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.println("");
    
    // Initialize sensor
    sensor_test_setup();
    
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    
#ifdef TEST_MODE_SINGLE
    Serial.println("TEST MODE: Single Read");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("");
    
    // Run single read test
    sensor_test_read_single();
    
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Test complete. System will halt.");
    Serial.println("Press RESET button to run again.");
    Serial.println("═══════════════════════════════════════════════════════════");
    
#elif defined(TEST_MODE_CONTINUOUS)
    Serial.println("TEST MODE: Continuous Reads");
    Serial.printf("  Number of reads per cycle: %d\n", CONTINUOUS_NUM_READS);
    Serial.printf("  Interval between reads: %d seconds\n", CONTINUOUS_INTERVAL_SEC);
    Serial.printf("  Delay between cycles: %d ms\n", CONTINUOUS_CYCLE_DELAY_MS);
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("");
    Serial.println("Starting continuous test loop...");
    Serial.println("Press RESET button to stop.");
    Serial.println("");
    
#elif defined(TEST_MODE_DIAGNOSTIC)
    Serial.println("TEST MODE: Diagnostics Only");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("");
    
    // Show diagnostics
    sensor_test_diagnostics();
    
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("Diagnostics complete. System will halt.");
    Serial.println("Press RESET button to run again.");
    Serial.println("═══════════════════════════════════════════════════════════");
    
#else
    #error "No test mode defined! Uncomment one of: TEST_MODE_SINGLE, TEST_MODE_CONTINUOUS, TEST_MODE_DIAGNOSTIC"
#endif
}

void loop() {
#ifdef TEST_MODE_CONTINUOUS
    // Run continuous test cycle
    sensor_test_continuous(CONTINUOUS_NUM_READS, CONTINUOUS_INTERVAL_SEC);
    
    // Wait before next cycle
    Serial.println("");
    Serial.printf("Waiting %d ms before next cycle...\n", CONTINUOUS_CYCLE_DELAY_MS);
    Serial.println("═══════════════════════════════════════════════════════════");
    Serial.println("");
    delay(CONTINUOUS_CYCLE_DELAY_MS);
    
#else
    // Single read or diagnostic mode - halt in loop
    delay(10000);
#endif
}
