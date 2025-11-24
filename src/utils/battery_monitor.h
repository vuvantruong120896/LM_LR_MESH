#ifndef _BATTERY_MONITOR_H
#define _BATTERY_MONITOR_H

#include <Arduino.h>
#include <esp_log.h>
#include <esp_adc_cal.h>

/**
 * @file battery_monitor.h
 * @brief Battery voltage monitoring via ADC for Li-ion battery with eFuse calibration
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
 * - ✅ USES eFuse CALIBRATION (ESP32-S3 internal Vref varies 1100-1200mV per chip)
 * - ✅ Reads ADC_CALI_CURVE_FIT_V2 from eFuse for accurate voltage conversion
 * - Attenuation: ADC_ATTEN_DB_11 (0-3.3V range, 12-bit = 0-4095)
 * - Returns voltage with HIGH accuracy (2 decimal places: e.g., 3.85V)
 * 
 * Why eFuse Calibration Matters:
 * - Each ESP32-S3 chip has different internal Vref (1080mV vs 1200mV)
 * - Without calibration: ADC reading can be off by ±5-10% per chip
 * - With eFuse: Corrects for chip-specific Vref variation automatically
 * 
 * Usage:
 * ```cpp
 * BatteryMonitor::init();
 * float voltage = BatteryMonitor::readVoltage();    // e.g., 3.85V (2 decimals)
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
    static constexpr float ADC_VREF_MV = 1200.0f;  // ESP32-S3 typical internal Vref (mV)
                                                    // ⚠️ VARIES per chip (1100-1200mV)
                                                    // ✅ eFuse calibration corrects this automatically

    // ADC Calibration Curve (V2 - for ESP32-S3)
    // Characteristics structure loaded from eFuse during init()
    static esp_adc_cal_characteristics_t* adc_chars;

    // Precision: 2 decimal places
    // ADC raw value → eFuse calibration → voltage in mV → float with 2 decimals
    // Example: raw=1850 → 1850mV/1000 → 1.85V

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
