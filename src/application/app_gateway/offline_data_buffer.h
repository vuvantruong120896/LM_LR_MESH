#ifndef OFFLINE_DATA_BUFFER_H
#define OFFLINE_DATA_BUFFER_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "../common/mesh_utils.h"

/**
 * @brief Service for buffering sensor data to NVS Flash when offline
 * 
 * Stores up to MAX_BUFFER_SIZE sensor readings in NVS Flash.
 * When online, data is retrieved and uploaded to Firebase, then cleared.
 * 
 * Features:
 * - Persistent storage across reboots
 * - Circular buffer (FIFO - oldest data replaced when full)
 * - Efficient NVS usage with single namespace
 */
class OfflineDataBuffer {
public:
    static const uint16_t MAX_BUFFER_SIZE = 500;  // Maximum buffered samples
    static const uint8_t CURRENT_SCHEMA_VERSION = 1;  // NVS schema version (incremented on breaking changes)
    
    /**
     * @brief Initialize NVS for offline buffer with schema version checking
     * 
     * Performs automatic migration if NVS schema version doesn't match current version.
     * On version mismatch, old data is cleared to prevent memory corruption from format differences.
     * 
     * @return true if initialization successful
     */
    static bool initialize();
    
    /**
     * @brief Add sensor data to buffer
     * @param nodeId Node/Gateway ID (hex string like "0xE764")
     * @param data Sensor data to buffer
     * @return true if successfully buffered
     */
    static bool addData(const String& nodeId, const sensorData& data);
    
    /**
     * @brief Get count of buffered samples
     * @return Number of samples in buffer
     */
    static uint16_t getBufferedCount();
    
    /**
     * @brief Get oldest buffered data (for upload)
     * @param nodeId Output - Node ID of the data
     * @param data Output - Sensor data
     * @return true if data retrieved successfully
     */
    static bool getOldestData(String& nodeId, sensorData& data);
    
    /**
     * @brief Remove oldest buffered data (after successful upload)
     * @return true if successfully removed
     */
    static bool removeOldest();
    
    /**
     * @brief Clear all buffered data
     * @return true if successfully cleared
     */
    static bool clearAll();
    
    /**
     * @brief Check if buffer is full
     * @return true if buffer is at MAX_BUFFER_SIZE
     */
    static bool isFull();
    
    /**
     * @brief Get buffer usage statistics
     * @param count Output - Current buffer count
     * @param maxSize Output - Maximum buffer size
     * @param percentFull Output - Percentage full (0-100)
     */
    static void getStats(uint16_t& count, uint16_t& maxSize, uint8_t& percentFull);

private:
    static const char* NVS_NAMESPACE;
    static const char* KEY_HEAD;      // Index of next write position
    static const char* KEY_TAIL;      // Index of next read position
    static const char* KEY_COUNT;     // Current count of buffered items
    static const char* KEY_VERSION;   // NVS schema version (NEW in v1.0)
    
    static nvs_handle_t nvsHandle;
    static bool initialized;
    
    /**
     * @brief Get NVS key for data entry
     * @param index Buffer index (0 to MAX_BUFFER_SIZE-1)
     * @param isNodeId true for nodeId key, false for data key
     * @return Key string
     */
    static String getDataKey(uint16_t index, bool isNodeId);
    
    /**
     * @brief Read uint16_t value from NVS
     */
    static uint16_t readUint16(const char* key, uint16_t defaultValue);
    
    /**
     * @brief Write uint16_t value to NVS
     */
    static bool writeUint16(const char* key, uint16_t value);
};

#endif // OFFLINE_DATA_BUFFER_H
