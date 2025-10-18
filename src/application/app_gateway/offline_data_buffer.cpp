#include "offline_data_buffer.h"
#include <esp_log.h>

static const char* TAG = "OfflineBuffer";

const char* OfflineDataBuffer::NVS_NAMESPACE = "offline_buf";
const char* OfflineDataBuffer::KEY_HEAD = "head";
const char* OfflineDataBuffer::KEY_TAIL = "tail";
const char* OfflineDataBuffer::KEY_COUNT = "count";

nvs_handle_t OfflineDataBuffer::nvsHandle = 0;
bool OfflineDataBuffer::initialized = false;

bool OfflineDataBuffer::initialize() {
    if (initialized) {
        return true;
    }
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return false;
    }
    
    initialized = true;
    
    // Initialize head, tail, count if not exist
    uint16_t head = readUint16(KEY_HEAD, 0);
    uint16_t tail = readUint16(KEY_TAIL, 0);
    uint16_t count = readUint16(KEY_COUNT, 0);
    
    ESP_LOGI(TAG, "✅ Offline buffer initialized: head=%u, tail=%u, count=%u", head, tail, count);
    
    return true;
}

bool OfflineDataBuffer::addData(const String& nodeId, const sensorData& data) {
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
    
    // Store nodeId
    String nodeIdKey = getDataKey(head, true);
    esp_err_t err = nvs_set_str(nvsHandle, nodeIdKey.c_str(), nodeId.c_str());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store nodeId: %s", esp_err_to_name(err));
        return false;
    }
    
    // Store sensor data as blob
    String dataKey = getDataKey(head, false);
    err = nvs_set_blob(nvsHandle, dataKey.c_str(), &data, sizeof(sensorData));
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
    
    ESP_LOGD(TAG, "📦 Buffered data from %s (count: %u/%u)", nodeId.c_str(), count, MAX_BUFFER_SIZE);
    
    return true;
}

uint16_t OfflineDataBuffer::getBufferedCount() {
    if (!initialized) {
        return 0;
    }
    return readUint16(KEY_COUNT, 0);
}

bool OfflineDataBuffer::getOldestData(String& nodeId, sensorData& data) {
    if (!initialized) {
        return false;
    }
    
    uint16_t count = readUint16(KEY_COUNT, 0);
    if (count == 0) {
        return false; // Buffer empty
    }
    
    uint16_t tail = readUint16(KEY_TAIL, 0);
    
    // Read nodeId
    String nodeIdKey = getDataKey(tail, true);
    size_t required_size = 0;
    esp_err_t err = nvs_get_str(nvsHandle, nodeIdKey.c_str(), NULL, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get nodeId size: %s", esp_err_to_name(err));
        return false;
    }
    
    char* nodeIdBuf = (char*)malloc(required_size);
    if (!nodeIdBuf) {
        ESP_LOGE(TAG, "Failed to allocate memory for nodeId");
        return false;
    }
    
    err = nvs_get_str(nvsHandle, nodeIdKey.c_str(), nodeIdBuf, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read nodeId: %s", esp_err_to_name(err));
        free(nodeIdBuf);
        return false;
    }
    
    nodeId = String(nodeIdBuf);
    free(nodeIdBuf);
    
    // Read sensor data
    String dataKey = getDataKey(tail, false);
    size_t dataSize = sizeof(sensorData);
    err = nvs_get_blob(nvsHandle, dataKey.c_str(), &data, &dataSize);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(err));
        return false;
    }
    
    return true;
}

bool OfflineDataBuffer::removeOldest() {
    if (!initialized) {
        return false;
    }
    
    uint16_t count = readUint16(KEY_COUNT, 0);
    if (count == 0) {
        return false; // Buffer empty
    }
    
    uint16_t tail = readUint16(KEY_TAIL, 0);
    
    // Erase keys (optional - can skip to save flash wear)
    // String nodeIdKey = getDataKey(tail, true);
    // String dataKey = getDataKey(tail, false);
    // nvs_erase_key(nvsHandle, nodeIdKey.c_str());
    // nvs_erase_key(nvsHandle, dataKey.c_str());
    
    // Move tail forward
    tail = (tail + 1) % MAX_BUFFER_SIZE;
    count--;
    
    writeUint16(KEY_TAIL, tail);
    writeUint16(KEY_COUNT, count);
    
    nvs_commit(nvsHandle);
    
    ESP_LOGD(TAG, "🗑️ Removed oldest buffered data (remaining: %u)", count);
    
    return true;
}

bool OfflineDataBuffer::clearAll() {
    if (!initialized) {
        return false;
    }
    
    // Reset head, tail, count
    writeUint16(KEY_HEAD, 0);
    writeUint16(KEY_TAIL, 0);
    writeUint16(KEY_COUNT, 0);
    
    esp_err_t err = nvs_commit(nvsHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit clear: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "🗑️ Cleared all buffered data");
    
    return true;
}

bool OfflineDataBuffer::isFull() {
    return getBufferedCount() >= MAX_BUFFER_SIZE;
}

void OfflineDataBuffer::getStats(uint16_t& count, uint16_t& maxSize, uint8_t& percentFull) {
    count = getBufferedCount();
    maxSize = MAX_BUFFER_SIZE;
    percentFull = (count * 100) / MAX_BUFFER_SIZE;
}

String OfflineDataBuffer::getDataKey(uint16_t index, bool isNodeId) {
    char key[16];
    if (isNodeId) {
        snprintf(key, sizeof(key), "nid_%03u", index);
    } else {
        snprintf(key, sizeof(key), "dat_%03u", index);
    }
    return String(key);
}

uint16_t OfflineDataBuffer::readUint16(const char* key, uint16_t defaultValue) {
    uint16_t value = defaultValue;
    esp_err_t err = nvs_get_u16(nvsHandle, key, &value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Key not found, initialize with default
        nvs_set_u16(nvsHandle, key, defaultValue);
        nvs_commit(nvsHandle);
        return defaultValue;
    }
    return value;
}

bool OfflineDataBuffer::writeUint16(const char* key, uint16_t value) {
    esp_err_t err = nvs_set_u16(nvsHandle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
        return false;
    }
    return true;
}
