#ifndef FIREBASE_CLIENT_H
#define FIREBASE_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include "application/common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Forward declaration to avoid circular dependency
class FirebaseQueueManager;

/**
 * @brief Firebase Client for Gateway Application
 * 
 * This client handles all Firebase Realtime Database operations for the gateway:
 * - Upload sensor data from mesh nodes
 * - Upload gateway status and statistics
 * - Upload routing table
 * - Log system events
 * 
 * @note This is gateway-specific (lives in app_gateway/)
 *       Uses WiFiConnectionService for connectivity
 */
class FirebaseClient {
public:
    /**
     * @brief Firebase connection status
     */
    enum class ConnectionStatus {
        DISCONNECTED,       ///< Not connected to Firebase
        CONNECTING,         ///< Connection in progress
        CONNECTED,          ///< Successfully connected
        AUTHENTICATION_FAILED, ///< Authentication error
        ERROR              ///< General error
    };

    /**
     * @brief Firebase upload result
     */
    struct UploadResult {
        bool success;           ///< Upload succeeded
        String errorMessage;    ///< Error message (if failed)
        uint32_t timestamp;     ///< Upload timestamp
        size_t payloadSize;     ///< Payload size in bytes
    };

    /**
     * @brief Firebase statistics
     */
    struct FirebaseStats {
        uint32_t totalUploads;          ///< Total upload attempts
        uint32_t successfulUploads;     ///< Successful uploads
        uint32_t failedUploads;         ///< Failed uploads
        uint32_t totalBytesUploaded;    ///< Total bytes uploaded
        uint32_t lastUploadTime;        ///< Last upload timestamp (millis)
        float averageUploadTime;        ///< Average upload time (ms)
    };

    /**
     * @brief Constructor
     * @param firebaseHost Firebase host URL (e.g., "your-project.firebaseio.com")
     * @param firebaseAuth Firebase authentication token/secret
     * @param gatewayId Gateway identifier (e.g., "GW_240AC4123456")
     */
    FirebaseClient(
        const char* firebaseHost,
        const char* firebaseAuth,
        const char* gatewayId
    );

    /**
     * @brief Destructor
     */
    ~FirebaseClient();

    /**
     * @brief Initialize Firebase client
     * @return true if initialization successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Connect to Firebase
     * @return true if connection successful, false otherwise
     */
    bool connect();

    /**
     * @brief Disconnect from Firebase
     */
    void disconnect();

    /**
     * @brief Check if connected to Firebase
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Get connection status
     * @return Current connection status
     */
    ConnectionStatus getStatus() const;

    /**
     * @brief Upload sensor data from node
     * @param data Sensor data structure
     * @param rssi Signal strength (dBm)
     * @param snr Signal-to-noise ratio
     * @return Upload result
     */
    UploadResult uploadSensorData(const sensorData& data, int8_t rssi = 0, float snr = 0.0f);

    /**
     * @brief Upload gateway status
     * @param connectedNodes Number of connected nodes
     * @param totalPacketsReceived Total packets received
     * @param totalPacketsSent Total packets sent
     * @param wifiRssi WiFi signal strength
     * @param freeHeap Free heap memory (bytes)
     * @param uptimeSeconds Gateway uptime (seconds)
     * @return Upload result
     */
    UploadResult uploadGatewayStatus(
        uint16_t connectedNodes,
        uint32_t totalPacketsReceived,
        uint32_t totalPacketsSent,
        int8_t wifiRssi,
        uint32_t freeHeap,
        uint32_t uptimeSeconds
    );

    /**
     * @brief Upload routing table
     * @param routingTable Vector of routing table entries
     * @return Upload result
     */
    UploadResult uploadRoutingTable(const std::vector<RouteNode>& routingTable);

    /**
     * @brief Log system event
     * @param eventType Event type (e.g., "node_joined", "wifi_disconnected")
     * @param nodeId Node ID (if applicable)
     * @param details Additional event details (JSON string)
     * @return Upload result
     */
    UploadResult logEvent(
        const String& eventType,
        const String& nodeId = "",
        const String& details = ""
    );

    /**
     * @brief Update gateway info (one-time on boot)
     * @param macAddress Gateway MAC address
     * @param ipAddress Gateway IP address
     * @param firmwareVersion Firmware version
     * @return Upload result
     */
    UploadResult updateGatewayInfo(
        const String& macAddress,
        const String& ipAddress,
        const String& firmwareVersion
    );

    /**
     * @brief Update node info
     * @param nodeId Node ID
     * @param name Node name
     * @param type Node type (e.g., "sensor", "relay")
     * @param firmwareVersion Firmware version
     * @return Upload result
     */
    UploadResult updateNodeInfo(
        uint16_t nodeId,
        const String& name,
        const String& type,
        const String& firmwareVersion
    );

    /**
     * @brief Get Firebase statistics
     * @return Firebase statistics structure
     */
    FirebaseStats getStats() const;

    /**
     * @brief Reset statistics
     */
    void resetStats();

    /**
     * @brief Set retry configuration
     * @param maxRetries Maximum retry attempts (default: 3)
     * @param retryDelayMs Delay between retries in milliseconds (default: 1000)
     */
    void setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs);

    /**
     * @brief Enable/disable automatic timestamping
     * @param enabled true to enable automatic timestamps, false otherwise
     */
    void setAutoTimestamp(bool enabled);

    /**
     * @brief Get last error message
     * @return Last error message
     */
    String getLastError() const;

    /**
     * @brief Set user context for multi-user Firebase paths
     * @param userUID User UID from Firebase Authentication
     * @param gatewayMAC Gateway MAC address (e.g., "AA:BB:CC:DD:EE:FF")
     */
    void setUserContext(const String& userUID, const String& gatewayMAC);

    /**
     * @brief Get Firebase data object (for command poller to share connection)
     * @return Pointer to FirebaseData object
     */
    FirebaseData* getFirebaseData() { return &m_firebaseData; }

    // ===== NEW: Queue-based Non-blocking Firebase Operations =====
    
    /**
     * @brief Queue sensor data for upload (non-blocking)
     * @param data Sensor data structure
     * @param rssi Signal strength (dBm)
     * @param snr Signal-to-noise ratio
     * @param priority Queue priority (default: normal)
     * @return true if queued successfully, false otherwise
     */
    bool queueSensorData(const sensorData& data, int8_t rssi = 0, float snr = 0.0f, uint8_t priority = 2);

    /**
     * @brief Queue gateway status for upload (non-blocking)
     * @param connectedNodes Number of connected nodes
     * @param totalPacketsReceived Total packets received
     * @param totalPacketsSent Total packets sent
     * @param wifiRssi WiFi signal strength
     * @param freeHeap Free heap memory (bytes)
     * @param uptimeSeconds Gateway uptime (seconds)
     * @param priority Queue priority (default: normal)
     * @return true if queued successfully, false otherwise
     */
    bool queueGatewayStatus(
        uint16_t connectedNodes,
        uint32_t totalPacketsReceived,
        uint32_t totalPacketsSent,
        int8_t wifiRssi,
        uint32_t freeHeap,
        uint32_t uptimeSeconds,
        uint8_t priority = 2
    );

    /**
     * @brief Queue routing table for upload (non-blocking)
     * @param routingTable Vector of routing table entries
     * @param priority Queue priority (default: normal)
     * @return true if queued successfully, false otherwise
     */
    bool queueRoutingTable(const std::vector<RouteNode>& routingTable, uint8_t priority = 2);

    /**
     * @brief Queue system event for logging (non-blocking)
     * @param eventType Event type (e.g., "node_joined", "wifi_disconnected")
     * @param nodeId Node ID (if applicable)
     * @param details Additional event details (JSON string)
     * @param priority Queue priority (default: normal)
     * @return true if queued successfully, false otherwise
     */
    bool queueLogEvent(
        const String& eventType,
        const String& nodeId = "",
        const String& details = "",
        uint8_t priority = 2
    );

    /**
     * @brief Initialize Firebase queue system
     * @return true if initialization successful, false otherwise
     */
    bool initializeQueue();

    /**
     * @brief Shutdown Firebase queue system
     */
    void shutdownQueue();

    /**
     * @brief Check if queue system is running
     * @return true if queue is initialized and running
     */
    bool isQueueRunning() const;

private:
    // RAII helper for mutex
    class LockGuard {
    public:
        explicit LockGuard(SemaphoreHandle_t m) : mtx(m), locked(false) {
            if (mtx) {
                if (xSemaphoreTake(mtx, pdMS_TO_TICKS(5000)) == pdTRUE) {
                    locked = true;
                }
            }
        }
        ~LockGuard() {
            if (locked && mtx) {
                xSemaphoreGive(mtx);
            }
        }
        bool isLocked() const { return locked; }
    private:
        SemaphoreHandle_t mtx;
        bool locked;
    };

    // Firebase configuration
    const char* m_firebaseHost;
    const char* m_firebaseAuth;
    String m_gatewayId;  // Use String instead of const char* to avoid dangling pointer
    String m_userUID;    // User UID for multi-user paths
    String m_gatewayMAC; // Gateway MAC address for multi-user paths

    // Firebase objects
    FirebaseData m_firebaseData;
    FirebaseConfig m_firebaseConfig;
    FirebaseAuth m_firebaseAuthData;

    // State
    ConnectionStatus m_status;
    String m_lastError;
    bool m_autoTimestamp;

    // Concurrency control and fault handling
    SemaphoreHandle_t m_mutex;
    uint8_t m_consecutiveFailures;
    uint32_t m_cooldownUntilMs;
    uint32_t m_baseCooldownMs;      // base backoff (e.g., 2000 ms)
    uint8_t m_failureThreshold;     // activate breaker after N consecutive failures

    // Retry configuration
    uint8_t m_maxRetries;
    uint32_t m_retryDelayMs;

    // Statistics
    FirebaseStats m_stats;

    // Private methods
    bool uploadToPath(const String& path, const String& jsonData);
    bool uploadToPathWithRetry(const String& path, const String& jsonData);
    bool circuitAllowsUpload();
    void recordUploadResult(bool success);
    String createSensorDataJson(const sensorData& data, int8_t rssi, float snr);
    String createGatewayStatusJson(uint16_t nodes, uint32_t pktsRx, uint32_t pktsTx, 
                                    int8_t rssi, uint32_t heap, uint32_t uptime);
    String createRoutingTableJson(const std::vector<RouteNode>& routingTable);
    String createEventJson(const String& type, const String& nodeId, const String& details);
    String nodeIdToString(uint16_t nodeId);
    uint32_t getCurrentTimestamp();
    void updateUploadStats(bool success, size_t payloadSize, uint32_t uploadTime);
};

#endif // FIREBASE_CLIENT_H
