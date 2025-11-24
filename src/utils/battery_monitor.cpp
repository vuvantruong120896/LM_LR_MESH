#include "battery_monitor.h"

static const char* TAG = "BatteryMonitor";

// Static members
bool BatteryMonitor::initialized = false;

bool BatteryMonitor::init() {
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Configure ADC pin for analog input
    pinMode(BAT_ADC_PIN, INPUT);
    
    // Configure ADC attenuation to 11dB (0-3.3V range)
    analogSetAttenuation(ADC_11db);
    
    initialized = true;
    ESP_LOGI(TAG, "✅ Battery monitor initialized (GPIO%d)", BAT_ADC_PIN);
    ESP_LOGI(TAG, "   Voltage divider: R1=%0.0fkΩ, R2=%0.0fkΩ (×%.1f)", 
             VOLTAGE_DIVIDER_R1, VOLTAGE_DIVIDER_R2, VOLTAGE_MULTIPLIER);
    ESP_LOGI(TAG, "   Li-ion range: %.1fV (0%%) - %.1fV (100%%)", 
             BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);
    
    if (ADC_CALIBRATION_FACTOR != 1.0f) {
        ESP_LOGI(TAG, "   ADC Calibration factor: %.3f", ADC_CALIBRATION_FACTOR);
    }

    return true;
}

void BatteryMonitor::deinit() {
    initialized = false;
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

    // Read raw ADC value (averaged)
    int raw_adc = readRawADC();
    if (raw_adc == 0) {
        return 0.0f;
    }

    // Convert to voltage (0-3.3V range with 12-bit resolution)
    // ADC voltage = (raw_adc / 4095) * 3.3V
    float adc_voltage = (raw_adc / (float)ADC_MAX_VALUE) * ADC_VREF;

    // Apply voltage divider multiplier
    // VBAT = V_ADC × (R1 + R2) / R2
    // With R1=R2=1MΩ → VBAT = V_ADC × 2
    float battery_voltage = adc_voltage * VOLTAGE_MULTIPLIER;

    ESP_LOGD(TAG, "Raw ADC: %d → %.2fV → VBAT: %.2fV", raw_adc, adc_voltage, battery_voltage);

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

    ESP_LOGI(TAG, "[BatteryMonitor] %.2fV → %d%% [%.1fV - %.1fV range]", 
             voltage, (uint8_t)percentage, BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);

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
