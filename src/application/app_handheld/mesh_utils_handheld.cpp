#include "mesh_utils_handheld.h"
#include <Arduino.h>
#include <esp_log.h>
#include <esp_system.h>

// Simplified mesh utils for handheld (no mesh functionality needed)
// This is a stub implementation to satisfy compilation requirements

const char* MESH_UTILS_TAG = "MeshUtils";

void logMemoryUsage(const char* context) {
    // Simple memory logging for handheld
    size_t freeHeap = esp_get_free_heap_size();
    ESP_LOGI(MESH_UTILS_TAG, "[%s] Free heap: %u bytes", context, freeHeap);
}

void logSystemInfo() {
    ESP_LOGI(MESH_UTILS_TAG, "=== Handheld System Info ===");
    ESP_LOGI(MESH_UTILS_TAG, "Free heap: %u bytes", esp_get_free_heap_size());
    ESP_LOGI(MESH_UTILS_TAG, "Uptime: %u ms", millis());
    ESP_LOGI(MESH_UTILS_TAG, "CPU frequency: %u MHz", ESP.getCpuFreqMHz());
}