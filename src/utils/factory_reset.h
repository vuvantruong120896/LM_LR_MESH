#pragma once

#include <Arduino.h>
#include <functional>

/**
 * Factory Reset Manager
 * Handles factory reset via GPIO button press (IO13, 5 seconds)
 * 
 * Features:
 * - Monitor IO13 button press
 * - Trigger factory reset after 5 seconds hold
 * - Optional callback for pre-reset cleanup (Gateway routing table upload)
 * - Clear all NVS data (WiFi, provisioning, network config, etc.)
 * - Reboot device
 * 
 * Usage:
 *   FactoryReset::initialize();  // Call in setup()
 *   FactoryReset::setPreResetCallback(callback);  // Optional: Gateway cleanup
 *   FactoryReset::loop();         // Call in main loop()
 */
class FactoryReset {
public:
    // Pin definitions
    static constexpr int RESET_BUTTON_PIN = 13;    // IO13 - Factory reset button (active LOW)
    static constexpr int LED_INDICATOR_PIN = 10;   // IO10 - LED indicator (active LOW)
    static constexpr uint32_t RESET_HOLD_TIME = 5000; // 5 seconds

    // Callback type for pre-reset cleanup (e.g., upload empty routing table)
    using PreResetCallback = std::function<void()>;

    /**
     * Initialize factory reset button monitoring
     * Configures IO13 as INPUT_PULLUP
     */
    static void initialize();

    /**
     * Set optional callback to execute before factory reset
     * Used by Gateway to clear routing table in Firebase
     */
    static void setPreResetCallback(PreResetCallback callback);

    /**
     * Check button state and handle factory reset
     * Call this in main loop()
     */
    static void loop();

    /**
     * Perform factory reset immediately
     * - Execute pre-reset callback (if set)
     * - Clear all NVS partitions
     * - Reboot device
     */
    static void performFactoryReset();

private:
    static bool buttonPressed;
    static uint32_t pressStartTime;
    static bool resetTriggered;
    static PreResetCallback preResetCallback;
};
