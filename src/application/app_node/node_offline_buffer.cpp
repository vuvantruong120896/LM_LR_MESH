#include "node_offline_buffer.h"
#include <esp_log.h>

static const char* TAG = "NodeBuffer";

const char* NodeOfflineBuffer::NVS_NAMESPACE = "node_buf";
const char* NodeOfflineBuffer::KEY_HEAD = "head";
const char* NodeOfflineBuffer::KEY_TAIL = "tail";
const char* NodeOfflineBuffer::KEY_COUNT = "count";
const char* NodeOfflineBuffer::KEY_VERSION = "version";

nvs_handle_t NodeOfflineBuffer::nvsHandle = 0;
bool NodeOfflineBuffer::initialized = false;

bool NodeOfflineBuffer::initialize() {
    if (initialized) {
        return true;
    }
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    // Check NVS schema version for backward compatibility
    uint8_t storedVersion = 0;
    esp_err_t versionErr = nvs_get_u8(nvsHandle, KEY_VERSION, &storedVersion);
    
    if (versionErr == ESP_ERR_NVS_NOT_FOUND) {
        // First time setup
        ESP_LOGI(TAG, "📝 First-time NVS buffer initialization");
        ESP_LOGI(TAG, "   Setting schema version: %d", CURRENT_SCHEMA_VERSION);
        nvs_set_u8(nvsHandle, KEY_VERSION, CURRENT_SCHEMA_VERSION);
    } else if (versionErr == ESP_OK && storedVersion != CURRENT_SCHEMA_VERSION) {
        // Schema version mismatch - migration needed
        ESP_LOGW(TAG, "⚠️ NVS schema version mismatch!");
        ESP_LOGW(TAG, "   Stored: %d, Current: %d", storedVersion, CURRENT_SCHEMA_VERSION);
        ESP_LOGW(TAG, "   Clearing node buffer to prevent data corruption");
        
        // Clear all buffered data
        writeUint16(KEY_HEAD, 0);
        writeUint16(KEY_TAIL, 0);
        writeUint16(KEY_COUNT, 0);
        nvs_set_u8(nvsHandle, KEY_VERSION, CURRENT_SCHEMA_VERSION);
        nvs_commit(nvsHandle);
        
        ESP_LOGI(TAG, "✅ Migration complete - buffer cleared, schema updated to v%d", CURRENT_SCHEMA_VERSION);
    } else if (versionErr == ESP_OK) {
        ESP_LOGI(TAG, "✅ NVS schema version matches: %d", storedVersion);
    } else {
        ESP_LOGE(TAG, "❌ Failed to read NVS version: %s", esp_err_to_name(versionErr));
    }
    
    initialized = true;
    
    // Initialize head, tail, count if not exist
    uint16_t head = readUint16(KEY_HEAD, 0);
    uint16_t tail = readUint16(KEY_TAIL, 0);
    uint16_t count = readUint16(KEY_COUNT, 0);
    
    ESP_LOGI(TAG, "✅ Node buffer initialized: head=%u, tail=%u, count=%u", head, tail, count);
    
    return true;
}

bool NodeOfflineBuffer::addData(const sensorData& data) {
    if (!initialized) {
        ESP_LOGE(TAG, "Buffer not initialized");
        return false;
    }
    
    uint16_t head = readUint16(KEY_HEAD, 0);
    uint16_t tail = readUint16(KEY_TAIL, 0);
    uint16_t count = readUint16(KEY_COUNT, 0);
    
    // If buffer full, overwrite oldest (move tail forward)
    if (count >= MAX_BUFFER_SIZE) {
        tail = (tail + 1) % MAX_BUFFER_SIZE;
        writeUint16(KEY_TAIL, tail);
        count = MAX_BUFFER_SIZE - 1; // Will be incremented back to MAX_BUFFER_SIZE
        ESP_LOGW(TAG, "Buffer full, overwriting oldest data");
    }
    
    // Store sensor data as blob
    String dataKey = getDataKey(head);
    esp_err_t err = nvs_set_blob(nvsHandle, dataKey.c_str(), &data, sizeof(sensorData));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store data: %s", esp_err_to_name(err));
        return false;
    }
    
    // Commit to flash
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
        return false;
    }
    
    // Update head and count
    head = (head + 1) % MAX_BUFFER_SIZE;
    count++;
    
    writeUint16(KEY_HEAD, head);
    writeUint16(KEY_COUNT, count);
    
    ESP_LOGD(TAG, "📦 Buffered sensor data (count: %u/%u)", count, MAX_BUFFER_SIZE);
    
    return true;
}

uint16_t NodeOfflineBuffer::getBufferedCount() {
    if (!initialized) {
        return 0;
    }
    return readUint16(KEY_COUNT, 0);
}

bool NodeOfflineBuffer::getOldestData(sensorData& data) {
    if (!initialized) {
        return false;
    }
    
    uint16_t count = readUint16(KEY_COUNT, 0);
    if (count == 0) {
        return false; // Buffer empty
    }
    
    uint16_t tail = readUint16(KEY_TAIL, 0);
    
    // Read sensor data
    String dataKey = getDataKey(tail);
    size_t dataSize = sizeof(sensorData);
    esp_err_t err = nvs_get_blob(nvsHandle, dataKey.c_str(), &data, &dataSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool NodeOfflineBuffer::removeOldest() {
    if (!initialized) {
        return false;
    }
    
    uint16_t count = readUint16(KEY_COUNT, 0);
    if (count == 0) {
        return false; // Buffer empty
    }
    
    uint16_t tail = readUint16(KEY_TAIL, 0);
    
    // Move tail forward
    tail = (tail + 1) % MAX_BUFFER_SIZE;
    count--;
    
    writeUint16(KEY_TAIL, tail);
    writeUint16(KEY_COUNT, count);
    
    ESP_LOGD(TAG, "✅ Removed oldest data (remaining: %u)", count);
    
    return true;
}

bool NodeOfflineBuffer::clearAll() {
    if (!initialized) {
        return false;
    }
    
    writeUint16(KEY_HEAD, 0);
    writeUint16(KEY_TAIL, 0);
    writeUint16(KEY_COUNT, 0);
    
    esp_err_t err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit clear: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Node buffer cleared");
    return true;
}

bool NodeOfflineBuffer::isFull() {
    if (!initialized) {
        return false;
    }
    
    uint16_t count = readUint16(KEY_COUNT, 0);
    return count >= MAX_BUFFER_SIZE;
}

void NodeOfflineBuffer::getStats(uint16_t& count, uint16_t& maxSize, uint8_t& percentFull) {
    if (!initialized) {
        count = 0;
        maxSize = MAX_BUFFER_SIZE;
        percentFull = 0;
        return;
    }
    
    count = readUint16(KEY_COUNT, 0);
    maxSize = MAX_BUFFER_SIZE;
    percentFull = (count * 100) / MAX_BUFFER_SIZE;
}

// Private helper functions

String NodeOfflineBuffer::getDataKey(uint16_t index) {
    return String("d_") + String(index);
}

uint16_t NodeOfflineBuffer::readUint16(const char* key, uint16_t defaultValue) {
    uint16_t value = defaultValue;
    esp_err_t err = nvs_get_u16(nvsHandle, key, &value);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Key doesn't exist yet, write default value
        nvs_set_u16(nvsHandle, key, defaultValue);
        nvs_commit(nvsHandle);
        return defaultValue;
    }
    
    return value;
}

void NodeOfflineBuffer::writeUint16(const char* key, uint16_t value) {
    esp_err_t err = nvs_set_u16(nvsHandle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
        return;
    }
    
    err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit %s: %s", key, esp_err_to_name(err));
    }
}
