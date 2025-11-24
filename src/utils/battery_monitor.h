#ifndef _BATTERY_MONITOR_H
#define _BATTERY_MONITOR_H

#include <Arduino.h>
#include <esp_log.h>

/**
 * @file battery_monitor.h
 * @brief Battery voltage monitoring via ADC for Li-ion battery
 * 
 * Hardware Configuration:
 * - ADC Pin: GPIO8 (ADC1_CHANNEL_7)
 * - Battery: Li-ion 3.7V nominal (3.0V empty, 4.2V full)
 * - Connection: VBAT → GPIO8 (direct or via voltage divider)
 * 
 * Voltage Divider (if needed):
 * - If VBAT can exceed 3.3V (ESP32 ADC max), use voltage divider:
 *   VBAT ---[R1]--- GPIO8 ---[R2]--- GND
 *   
 *   Example: R1=10kΩ, R2=10kΩ → divides by 2 (4.2V → 2.1V at ADC)
 *   Formula: V_ADC = VBAT × (R2 / (R1 + R2))
 *   VBAT = V_ADC × (R1 + R2) / R2
 * 
 * ADC Calibration:
 * - Uses ESP32 ADC calibration (eFuse or default curve fitting)
 * - Attenuation: ADC_ATTEN_DB_11 (0-3.3V range)
 * - Bitwidth: ADC_BITWIDTH_12 (0-4095 raw values)
 * 
 * Usage:
 * ```cpp
 * BatteryMonitor::init();
 * float voltage = BatteryMonitor::readVoltage();
 * uint8_t percent = BatteryMonitor::getPercentage();
 * ```
 */

class BatteryMonitor {
public:
    /**
     * @brief Initialize battery monitor (ADC setup)
     * @return true if initialized successfully
     */
    static bool init();

    /**
     * @brief Read battery voltage
     * @return Battery voltage in volts (e.g., 3.85V)
     */
    static float readVoltage();

    /**
     * @brief Get battery percentage
     * @return Battery percentage 0-100%
     */
    static uint8_t getPercentage();

    /**
     * @brief Check if battery is low
     * @param threshold Low battery threshold in volts (default 3.3V)
     * @return true if battery voltage is below threshold
     */
    static bool isLowBattery(float threshold = 3.3f);

    /**
     * @brief Deinitialize ADC
     */
    static void deinit();

private:
    // Hardware configuration
    static constexpr int BAT_ADC_PIN = 8;                    // GPIO8

    // Voltage divider configuration (from schematic)
    // VBAT → R10 (1MΩ) → BAT_ADC → R14 (1MΩ) → GND
    // Voltage divider ratio: VBAT / 2 = ADC voltage
    static constexpr float VOLTAGE_DIVIDER_R1 = 1000.0f;  // kΩ (R10 = 1MΩ top resistor)
    static constexpr float VOLTAGE_DIVIDER_R2 = 1000.0f;  // kΩ (R14 = 1MΩ bottom resistor)
    static constexpr float VOLTAGE_MULTIPLIER = (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2;  // = 2.0

    // Li-ion battery voltage range
    static constexpr float BATTERY_VOLTAGE_MIN = 3.5f;   // Empty battery (0%)
    static constexpr float BATTERY_VOLTAGE_MAX = 4.2f;   // Full battery (100%)
    static constexpr float BATTERY_VOLTAGE_NOMINAL = 3.7f; // Nominal voltage

    // ADC configuration
    static constexpr int ADC_SAMPLES = 10;  // Number of samples to average
    static constexpr int ADC_MAX_VALUE = 4095;  // 12-bit ADC
    static constexpr float ADC_VREF = 3.35f;     // ESP32 reference voltage (V)

    // ADC Calibration Factor
    // Adjust this if ADC reading differs from multimeter measurement
    // Formula: VBAT_actual = VBAT_measured × ADC_CALIBRATION_FACTOR
    // 
    // How to calibrate:
    // 1. Measure VBAT with multimeter (e.g., 3.85V)
    // 2. Note ADC reading from serial output (e.g., 3.70V)
    // 3. Calculate factor = 3.85 / 3.70 = 1.041
    // 4. Update this value to 1.041f
    // 
    // If ADC consistently reads 0.15V lower, factor ≈ 1.04-1.05
    static constexpr float ADC_CALIBRATION_FACTOR = 1.041f;  // Default: no calibration (adjust as needed)

    // State
    static bool initialized;

    /**
     * @brief Read raw ADC value with averaging
     * @return Average raw ADC value (0-4095)
     */
    static int readRawADC();

    /**
     * @brief Convert battery voltage to percentage (0-100%)
     * @param voltage Battery voltage in volts
     * @return Percentage 0-100
     */
    static uint8_t voltageToPercentage(float voltage);
};

#endif // _BATTERY_MONITOR_H
