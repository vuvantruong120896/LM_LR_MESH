#include "battery_monitor.h"

static const char* TAG = "BatteryMonitor";

// Static members
bool BatteryMonitor::initialized = false;
esp_adc_cal_characteristics_t* BatteryMonitor::adc_chars = nullptr;

// Smoothing and caching for stable readings
static float cachedVoltage = 0.0f;
static uint8_t cachedPercentage = 0;
static uint32_t lastReadTime = 0;
static constexpr uint32_t CACHE_VALID_MS = 5000;  // Cache valid for 5 seconds
static constexpr float EMA_ALPHA = 0.2f;          // Smoothing factor (lower = more stable)

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
    cachedVoltage = 0.0f;
    cachedPercentage = 0;
    lastReadTime = 0;
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

    // Read multiple samples and average (reduced delay for faster response)
    for (int i = 0; i < ADC_SAMPLES; i++) {
        int raw_value = analogRead(BAT_ADC_PIN);
        
        if (raw_value >= 0 && raw_value <= ADC_MAX_VALUE) {
            total += raw_value;
            valid_samples++;
        }
        
        delayMicroseconds(500);  // 0.5ms delay between samples (was 10ms)
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

    // Return cached value if still valid (reduces ADC noise from frequent reads)
    uint32_t now = millis();
    if (cachedVoltage > 0 && (now - lastReadTime) < CACHE_VALID_MS) {
        return cachedVoltage;
    }

    // Read raw ADC value (averaged)
    int raw_adc = readRawADC();
    if (raw_adc == 0) {
        return cachedVoltage > 0 ? cachedVoltage : 0.0f;
    }

    // ✅ USE eFuse calibration for accurate voltage conversion
    // This corrects for chip-specific Vref variation
    uint32_t voltage_mv = 0;
    if (adc_chars) {
        voltage_mv = esp_adc_cal_raw_to_voltage(raw_adc, adc_chars);
    } else {
        // Fallback: direct calculation if calibration not available
        voltage_mv = (uint32_t)((float)raw_adc * 1.0f);  // ~1:1 mapping observed
    }
    
    float adc_voltage = voltage_mv / 1000.0f;

    // Apply voltage divider multiplier
    // VBAT = V_ADC × (R1 + R2) / R2
    // With R1=R2=1MΩ → VBAT = V_ADC × 2
    float battery_voltage = adc_voltage * VOLTAGE_MULTIPLIER;

    // Apply EMA smoothing to reduce noise
    if (cachedVoltage > 0) {
        battery_voltage = (EMA_ALPHA * battery_voltage) + ((1.0f - EMA_ALPHA) * cachedVoltage);
    }

    // Round to 2 decimal places for consistency
    battery_voltage = roundf(battery_voltage * 100.0f) / 100.0f;

    // Update cache
    cachedVoltage = battery_voltage;
    lastReadTime = now;

    ESP_LOGD(TAG, "Raw ADC: %d → %umV → %.2fV → VBAT: %.2fV (eFuse calibrated, EMA smoothed)", 
             raw_adc, voltage_mv, adc_voltage, battery_voltage);

    return battery_voltage;
}

uint8_t BatteryMonitor::voltageToPercentage(float voltage) {
    // Li-ion battery discharge curve lookup table
    // Based on typical Li-ion 3.7V cell discharge profile
    // More accurate than linear interpolation
    static const struct {
        float voltage;
        uint8_t percent;
    } dischargeCurve[] = {
        {4.20f, 100},
        {4.15f, 95},
        {4.10f, 90},
        {4.05f, 85},
        {4.00f, 80},
        {3.95f, 75},
        {3.90f, 70},
        {3.85f, 65},
        {3.80f, 60},
        {3.75f, 55},
        {3.70f, 50},  // Nominal voltage
        {3.65f, 45},
        {3.60f, 40},
        {3.55f, 30},  // Knee of discharge curve
        {3.50f, 20},
        {3.45f, 15},
        {3.40f, 10},
        {3.35f, 5},
        {3.30f, 0},   // Cutoff voltage
    };
    static const int curveSize = sizeof(dischargeCurve) / sizeof(dischargeCurve[0]);

    // Clamp to valid range
    if (voltage >= dischargeCurve[0].voltage) {
        return 100;
    }
    if (voltage <= dischargeCurve[curveSize - 1].voltage) {
        return 0;
    }

    // Find the two points to interpolate between
    for (int i = 0; i < curveSize - 1; i++) {
        if (voltage <= dischargeCurve[i].voltage && voltage > dischargeCurve[i + 1].voltage) {
            // Linear interpolation between two points
            float v1 = dischargeCurve[i].voltage;
            float v2 = dischargeCurve[i + 1].voltage;
            uint8_t p1 = dischargeCurve[i].percent;
            uint8_t p2 = dischargeCurve[i + 1].percent;
            
            float ratio = (voltage - v2) / (v1 - v2);
            return (uint8_t)(p2 + ratio * (p1 - p2));
        }
    }

    return 0;
}

uint8_t BatteryMonitor::getPercentage() {
    float voltage = readVoltage();
    return voltageToPercentage(voltage);
}

bool BatteryMonitor::isLowBattery(float threshold) {
    float voltage = readVoltage();
    return voltage < threshold && voltage > 0.0f;  // Ignore 0V (error case)
}
