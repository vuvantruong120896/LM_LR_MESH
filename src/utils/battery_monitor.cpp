#include "battery_monitor.h"

static const char* TAG = "BatteryMonitor";

// Static members
bool BatteryMonitor::initialized = false;
esp_adc_cal_characteristics_t* BatteryMonitor::adc_chars = nullptr;

bool BatteryMonitor::init() {
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Configure ADC pin for analog input
    pinMode(BAT_ADC_PIN, INPUT);
    
    // Configure ADC attenuation to 11dB (0-3.3V range)
    analogSetAttenuation(ADC_11db);
    
    // ✅ Initialize ADC calibration from eFuse (CRITICAL for accurate readings)
    adc_chars = (esp_adc_cal_characteristics_t*)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    if (!adc_chars) {
        ESP_LOGE(TAG, "Failed to allocate memory for ADC calibration characteristics");
        return false;
    }

    // Read calibration data from eFuse
    // This corrects for chip-specific internal Vref variation (1100-1200mV)
    esp_adc_cal_value_t cal_status = esp_adc_cal_characterize(
        ADC_UNIT_1,                           // ADC1 (GPIO8 is on ADC1)
        ADC_ATTEN_DB_12,                      // Attenuation 12dB (0-3.3V) for ESP32-S3
        ADC_WIDTH_BIT_12,                     // 12-bit resolution (0-4095)
        ADC_VREF_MV,                          // Use typical Vref as default
        adc_chars
    );

    // Log calibration source
    switch (cal_status) {
        case ESP_ADC_CAL_VAL_EFUSE_VREF:
            ESP_LOGI(TAG, "✅ ADC calibration: Vref loaded from eFuse (HIGHEST accuracy)");
            break;
        case ESP_ADC_CAL_VAL_EFUSE_TP:
            ESP_LOGI(TAG, "✅ ADC calibration: Two-point calibration from eFuse (HIGH accuracy)");
            break;
        case ESP_ADC_CAL_VAL_DEFAULT_VREF:
            ESP_LOGW(TAG, "⚠️  ADC calibration: Using default Vref (medium accuracy)");
            break;
        default:
            ESP_LOGW(TAG, "⚠️  ADC calibration: Unknown status (%d)", cal_status);
    }

    initialized = true;
    ESP_LOGI(TAG, "✅ Battery monitor initialized (GPIO%d)", BAT_ADC_PIN);
    ESP_LOGI(TAG, "   Voltage divider: R1=%0.0fkΩ, R2=%0.0fkΩ (×%.1f)", 
             VOLTAGE_DIVIDER_R1, VOLTAGE_DIVIDER_R2, VOLTAGE_MULTIPLIER);
    ESP_LOGI(TAG, "   Li-ion range: %.1fV (0%%) - %.1fV (100%%)", 
             BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);
    ESP_LOGI(TAG, "   Precision: 2 decimal places (e.g., 3.85V)");

    return true;
}

void BatteryMonitor::deinit() {
    initialized = false;
    if (adc_chars) {
        free(adc_chars);
        adc_chars = nullptr;
    }
    ESP_LOGI(TAG, "Battery monitor deinitialized");
}

int BatteryMonitor::readRawADC() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized! Call init() first");
        return 0;
    }

    int total = 0;
    int valid_samples = 0;

    // Read multiple samples and average
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int raw_value = analogRead(BAT_ADC_PIN);
        
        if (raw_value >= 0 && raw_value <= ADC_MAX_VALUE) {
            total += raw_value;
            valid_samples++;
        }
        
        delay(10);  // Small delay between samples
    }

    if (valid_samples == 0) {
        ESP_LOGE(TAG, "All ADC reads failed!");
        return 0;
    }

    return total / valid_samples;
}

float BatteryMonitor::readVoltage() {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized! Call init() first");
        return 0.0f;
    }

    if (!adc_chars) {
        ESP_LOGE(TAG, "ADC calibration not initialized!");
        return 0.0f;
    }

    // Read raw ADC value (averaged)
    int raw_adc = readRawADC();
    if (raw_adc == 0) {
        return 0.0f;
    }

    // ✅ Convert raw ADC to voltage using eFuse calibration (mV)
    // esp_adc_cal_raw_to_voltage() uses the calibration characteristics to convert
    // raw ADC value → voltage in mV, accounting for chip-specific Vref variation
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(raw_adc, adc_chars);
    
    // Convert to volts
    float adc_voltage = voltage_mv / 1000.0f;

    // Apply voltage divider multiplier
    // VBAT = V_ADC × (R1 + R2) / R2
    // With R1=R2=1MΩ → VBAT = V_ADC × 2
    float battery_voltage = adc_voltage * VOLTAGE_MULTIPLIER;

    // Round to 2 decimal places for consistency
    battery_voltage = roundf(battery_voltage * 100.0f) / 100.0f;

    // Disable verbose logging - too much spam
    // ESP_LOGD(TAG, "Raw ADC: %d → %umV → %.2fV → VBAT: %.2fV (eFuse calibrated)", 
    //          raw_adc, voltage_mv, adc_voltage, battery_voltage);

    return battery_voltage;
}

uint8_t BatteryMonitor::voltageToPercentage(float voltage) {
    // Clamp voltage to valid range
    if (voltage >= BATTERY_VOLTAGE_MAX) {
        return 100;
    }
    if (voltage <= BATTERY_VOLTAGE_MIN) {
        ESP_LOGW(TAG, "⚠️ Battery voltage %.2fV is below minimum %.2fV - returning 0%%", 
                 voltage, BATTERY_VOLTAGE_MIN);
        return 0;
    }

    // Linear interpolation between min and max
    // This is simplified - real Li-ion discharge curve is not linear
    // For better accuracy, use a lookup table with voltage-percentage mapping
    float percentage = ((voltage - BATTERY_VOLTAGE_MIN) / 
                       (BATTERY_VOLTAGE_MAX - BATTERY_VOLTAGE_MIN)) * 100.0f;

    // Clamp to 0-100 range
    if (percentage < 0.0f) percentage = 0.0f;
    if (percentage > 100.0f) percentage = 100.0f;

    // Disable verbose logging - too much spam
    // ESP_LOGI(TAG, "[BatteryMonitor] %.2fV → %d%% [%.1fV - %.1fV range]", 
    //          voltage, (uint8_t)percentage, BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);

    return (uint8_t)percentage;
}

uint8_t BatteryMonitor::getPercentage() {
    float voltage = readVoltage();
    return voltageToPercentage(voltage);
}

bool BatteryMonitor::isLowBattery(float threshold) {
    float voltage = readVoltage();
    return voltage < threshold && voltage > 0.0f;  // Ignore 0V (error case)
}
