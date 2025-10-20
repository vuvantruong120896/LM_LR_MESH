#ifndef NODE_OFFLINE_BUFFER_H
#define NODE_OFFLINE_BUFFER_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "../common/mesh_utils.h"

/**
 * @brief Service for Node to buffer sensor data when Gateway unavailable
 * 
 * Stores up to MAX_BUFFER_SIZE sensor readings in NVS Flash when no gateway
 * is found in the routing table. When gateway becomes available, buffered
 * data is retrieved and sent to gateway, then cleared.
 * 
 * Features:
 * - Persistent storage across reboots
 * - Circular buffer (FIFO - oldest data replaced when full)
 * - Efficient NVS usage with dedicated namespace
 * 
 * Capacity: 50 samples × 44 bytes = ~2.2 KB data + ~1 KB metadata = ~3.2 KB total
 * Typical offline window: 50 samples × 10-15s interval = 8-12 minutes
 * 
 * NOTE: Uses separate NVS namespace "node_buf" (different from Gateway's "offline_buf")
 */
class NodeOfflineBuffer {
public:
    static const uint16_t MAX_BUFFER_SIZE = 50;  // Maximum buffered samples
    static const uint8_t CURRENT_SCHEMA_VERSION = 1;  // NVS schema version
    
    /**
     * @brief Initialize NVS for node offline buffer with schema version checking
     * 
     * Performs automatic migration if NVS schema version doesn't match current version.
     * On version mismatch, old data is cleared to prevent memory corruption.
     * 
     * @return true if initialization successful
     */
    static bool initialize();
    
    /**
     * @brief Add sensor data to buffer (when no gateway found)
     * @param data Sensor data to buffer
     * @return true if successfully buffered
     */
    static bool addData(const sensorData& data);
    
    /**
     * @brief Get count of buffered samples
     * @return Number of samples in buffer
     */
    static uint16_t getBufferedCount();
    
    /**
     * @brief Get oldest buffered data (for sending to gateway)
     * @param data Output - Sensor data
     * @return true if data retrieved successfully
     */
    static bool getOldestData(sensorData& data);
    
    /**
     * @brief Remove oldest buffered data (after successful send to gateway)
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
    static const char* NVS_NAMESPACE;  // "node_buf"
    static const char* KEY_HEAD;      // Index of next write position
    static const char* KEY_TAIL;      // Index of next read position
    static const char* KEY_COUNT;     // Current count of buffered items
    static const char* KEY_VERSION;   // NVS schema version
    
    static nvs_handle_t nvsHandle;
    static bool initialized;
    
    /**
     * @brief Get NVS key for data entry
     * @param index Buffer index (0 to MAX_BUFFER_SIZE-1)
     * @return Key string (e.g., "d_0", "d_1", ...)
     */
    static String getDataKey(uint16_t index);
    
    /**
     * @brief Read uint16 value from NVS with default fallback
     * @param key NVS key
     * @param defaultValue Default value if key not found
     * @return Value read from NVS or default
     */
    static uint16_t readUint16(const char* key, uint16_t defaultValue);
    
    /**
     * @brief Write uint16 value to NVS
     * @param key NVS key
     * @param value Value to write
     */
    static void writeUint16(const char* key, uint16_t value);
};

#endif // NODE_OFFLINE_BUFFER_H
