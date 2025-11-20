#include "factory_reset.h"
#include <nvs_flash.h>
#include <esp_system.h>
#include "NVSStorageService.h"

#define TAG "FactoryReset"

// Static member initialization
bool FactoryReset::buttonPressed = false;
uint32_t FactoryReset::pressStartTime = 0;
bool FactoryReset::resetTriggered = false;
FactoryReset::PreResetCallback FactoryReset::preResetCallback = nullptr;

void FactoryReset::initialize() {
    // Configure IO13 as input with pull-up (button connects to GND)
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    
    // Configure IO10 as output for LED indicator (active LOW)
    pinMode(LED_INDICATOR_PIN, OUTPUT);
    digitalWrite(LED_INDICATOR_PIN, HIGH);  // LED off initially
    
    ESP_LOGI(TAG, "🔧 Factory Reset initialized - Press IO13 for 5s to reset");
    ESP_LOGI(TAG, "   Button: GPIO%d (active LOW)", RESET_BUTTON_PIN);
    ESP_LOGI(TAG, "   LED: GPIO%d (active LOW)", LED_INDICATOR_PIN);
}

void FactoryReset::setPreResetCallback(PreResetCallback callback) {
    preResetCallback = callback;
    ESP_LOGI(TAG, "✅ Pre-reset callback registered");
}

void FactoryReset::loop() {
    // Don't check if already triggered
    if (resetTriggered) {
        return;
    }

    // Read button state (active LOW - pressed = LOW)
    bool isPressed = (digitalRead(RESET_BUTTON_PIN) == LOW);

    if (isPressed) {
        if (!buttonPressed) {
            // Button just pressed
            buttonPressed = true;
            pressStartTime = millis();
            ESP_LOGI(TAG, "🔘 Factory reset button pressed - hold for 5 seconds...");
        } else {
            // Button still held - check duration
            uint32_t holdDuration = millis() - pressStartTime;
            
            // Log progress every second
            static uint32_t lastLogTime = 0;
            if (millis() - lastLogTime >= 1000) {
                ESP_LOGI(TAG, "⏱️  Holding... %u/%u seconds", holdDuration / 1000, RESET_HOLD_TIME / 1000);
                lastLogTime = millis();
            }

            // Trigger reset after 5 seconds
            if (holdDuration >= RESET_HOLD_TIME) {
                // Turn on LED indicator (active LOW)
                digitalWrite(LED_INDICATOR_PIN, LOW);
                ESP_LOGW(TAG, "🚨 FACTORY RESET TRIGGERED! LED ON");
                resetTriggered = true;
                performFactoryReset();
            }
        }
    } else {
        if (buttonPressed) {
            // Button released before 5 seconds
            uint32_t holdDuration = millis() - pressStartTime;
            ESP_LOGI(TAG, "🔘 Button released after %u ms (need 5000 ms)", holdDuration);
            buttonPressed = false;
        }
    }
}

void FactoryReset::performFactoryReset() {
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║           FACTORY RESET IN PROGRESS                    ║");
    ESP_LOGW(TAG, "╚════════════════════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");

    // Step 0: Execute pre-reset callback (Gateway cleanup)
    if (preResetCallback) {
        ESP_LOGW(TAG, "📡 Step 0: Executing pre-reset cleanup...");
        preResetCallback();
        ESP_LOGI(TAG, "✅ Pre-reset cleanup completed");
        
        // Wait a bit for Firebase upload to complete
        ESP_LOGI(TAG, "⏳ Waiting 3 seconds for cleanup operations...");
        delay(3000);
    }

    // Step 1: Clear all NVS namespaces
    ESP_LOGW(TAG, "📦 Step 1: Erasing all NVS data...");
    
    // Erase entire NVS partition (most thorough method)
    esp_err_t ret = nvs_flash_erase();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ NVS partition erased successfully");
    } else {
        ESP_LOGE(TAG, "❌ Failed to erase NVS partition: %s", esp_err_to_name(ret));
    }

    // Re-initialize NVS after erase
    ret = nvs_flash_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ NVS re-initialized");
    } else {
        ESP_LOGE(TAG, "❌ Failed to re-initialize NVS: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "✅ Factory reset complete!");
    ESP_LOGW(TAG, "   - All WiFi credentials cleared");
    ESP_LOGW(TAG, "   - All provisioning data cleared");
    ESP_LOGW(TAG, "   - All network configuration cleared");
    ESP_LOGW(TAG, "   - All sensor data cleared");
    ESP_LOGW(TAG, "");

    // Step 2: Reboot device
    ESP_LOGW(TAG, "🔄 Rebooting in 2 seconds...");
    delay(2000);
    
    ESP_LOGW(TAG, "🔄 REBOOTING NOW!");
    esp_restart();
}
