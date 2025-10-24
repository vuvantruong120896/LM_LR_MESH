#include "firebase_coordinator.h"
#include <esp_log.h>

static const char* TAG = "FB_COORDINATOR";

// Static member initialization
uint32_t FirebaseOperationCoordinator::s_lastOperationTime = 0;
SemaphoreHandle_t FirebaseOperationCoordinator::s_timeMutex = nullptr;
SemaphoreHandle_t FirebaseOperationCoordinator::s_opSemaphore = nullptr;

void FirebaseOperationCoordinator::initialize() {
    if (!s_timeMutex) {
        s_timeMutex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "Time mutex created");
    }
    
    if (!s_opSemaphore) {
        s_opSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(s_opSemaphore); // Initial state: available
        ESP_LOGI(TAG, "Operation semaphore created (binary, initially available)");
    }
    
    s_lastOperationTime = 0;
    ESP_LOGI(TAG, "Firebase operation coordinator initialized (min interval: %u ms)", MIN_INTERVAL_MS);
}

bool FirebaseOperationCoordinator::tryAcquireOperationLock(uint32_t timeoutMs) {
    if (!s_opSemaphore) {
        ESP_LOGE(TAG, "Coordinator not initialized!");
        return false;
    }
    
    // Try to acquire binary semaphore (only ONE task can hold it)
    TickType_t ticks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    
    if (xSemaphoreTake(s_opSemaphore, ticks)) {
        ESP_LOGD(TAG, "[OP-LOCK] Acquired exclusive Firebase operation lock");
        return true;
    }
    
    ESP_LOGW(TAG, "[OP-LOCK] Failed to acquire operation lock (timeout %u ms)", timeoutMs);
    return false;
}

void FirebaseOperationCoordinator::releaseOperationLock() {
    if (s_opSemaphore) {
        xSemaphoreGive(s_opSemaphore);
        ESP_LOGV(TAG, "[OP-LOCK] Released exclusive Firebase operation lock");
    }
}

uint32_t FirebaseOperationCoordinator::waitForMinInterval() {
    if (!s_timeMutex) return 0;
    
    uint32_t waitMs = 0;
    
    if (xSemaphoreTake(s_timeMutex, pdMS_TO_TICKS(100))) {
        uint32_t now = millis();
        uint32_t elapsed = now - s_lastOperationTime;
        
        if (elapsed < MIN_INTERVAL_MS) {
            waitMs = MIN_INTERVAL_MS - elapsed;
        }
        
        xSemaphoreGive(s_timeMutex);
    }
    
    if (waitMs > 0) {
        ESP_LOGD(TAG, "[MIN-INTERVAL] Waiting %u ms for minimum interval", waitMs);
        vTaskDelay(pdMS_TO_TICKS(waitMs));
    }
    
    return waitMs;
}

void FirebaseOperationCoordinator::markOperationComplete() {
    if (!s_timeMutex) return;
    
    if (xSemaphoreTake(s_timeMutex, pdMS_TO_TICKS(100))) {
        s_lastOperationTime = millis();
        xSemaphoreGive(s_timeMutex);
        ESP_LOGV(TAG, "[TIMESTAMP] Operation completed, timestamp updated");
    }
}

uint32_t FirebaseOperationCoordinator::getTimeSinceLastOperation() {
    if (!s_timeMutex) return 0xFFFFFFFF;
    
    uint32_t elapsed = 0;
    if (xSemaphoreTake(s_timeMutex, pdMS_TO_TICKS(100))) {
        elapsed = millis() - s_lastOperationTime;
        xSemaphoreGive(s_timeMutex);
    }
    return elapsed;
}
