#include "firebase_coordinator.h"
#include <esp_log.h>

static const char* TAG = "FB_COORDINATOR";

// Static member initialization
uint32_t FirebaseOperationCoordinator::s_lastOperationTime = 0;
SemaphoreHandle_t FirebaseOperationCoordinator::s_mutex = nullptr;

void FirebaseOperationCoordinator::initialize() {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "Firebase operation coordinator initialized (min interval: %u ms)", MIN_INTERVAL_MS);
    }
    s_lastOperationTime = 0;
}

uint32_t FirebaseOperationCoordinator::getWaitTime() {
    if (!s_mutex) return 0;
    
    uint32_t waitMs = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100))) {
        uint32_t now = millis();
        uint32_t elapsed = now - s_lastOperationTime;
        
        if (elapsed < MIN_INTERVAL_MS) {
            waitMs = MIN_INTERVAL_MS - elapsed;
        }
        
        xSemaphoreGive(s_mutex);
    }
    return waitMs;
}

void FirebaseOperationCoordinator::markOperationStart() {
    if (!s_mutex) return;
    
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100))) {
        s_lastOperationTime = millis();
        xSemaphoreGive(s_mutex);
    }
}

uint32_t FirebaseOperationCoordinator::getTimeSinceLastOperation() {
    if (!s_mutex) return 0xFFFFFFFF;
    
    uint32_t elapsed = 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100))) {
        elapsed = millis() - s_lastOperationTime;
        xSemaphoreGive(s_mutex);
    }
    return elapsed;
}
