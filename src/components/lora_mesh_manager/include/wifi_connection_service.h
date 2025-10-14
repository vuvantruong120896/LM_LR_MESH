#ifndef WIFI_CONNECTION_SERVICE_H
#define WIFI_CONNECTION_SERVICE_H

#include <WiFi.h>
#include <functional>
#include <vector>

/**
 * @brief Generic WiFi Connection Service with auto-reconnect and event handling
 * 
 * This service provides robust WiFi connection management with:
 * - Automatic reconnection with exponential backoff
 * - Event callbacks for connection state changes
 * - RSSI monitoring and signal quality tracking
 * - Connection uptime and statistics
 * - Thread-safe state management
 * 
 * @note This is a generic service that can be reused across different applications
 *       (gateway, node OTA updates, etc.)
 */
class WiFiConnectionService {
public:
    /**
     * @brief WiFi connection events
     */
    enum class WiFiEvent {
        CONNECTED,          ///< Successfully connected to WiFi
        DISCONNECTED,       ///< Disconnected from WiFi
        RECONNECTING,       ///< Attempting to reconnect
        CONNECTION_FAILED,  ///< Connection attempt failed
        RSSI_LOW           ///< RSSI dropped below threshold
    };

    /**
     * @brief WiFi connection status
     */
    enum class WiFiStatus {
        DISCONNECTED,   ///< Not connected
        CONNECTING,     ///< Connection in progress
        CONNECTED,      ///< Successfully connected
        RECONNECTING,   ///< Reconnection in progress
        FAILED         ///< Connection failed (max retries exceeded)
    };

    /**
     * @brief WiFi connection statistics
     */
    struct WiFiStats {
        uint32_t connectionAttempts;    ///< Total connection attempts
        uint32_t successfulConnections; ///< Successful connections count
        uint32_t disconnections;        ///< Disconnection count
        uint32_t reconnectAttempts;     ///< Reconnection attempts count
        uint32_t uptimeSeconds;         ///< Total uptime in seconds
        int8_t currentRSSI;             ///< Current RSSI value
        int8_t averageRSSI;             ///< Average RSSI over time
        uint32_t lastConnectTime;       ///< Last successful connection timestamp (millis)
        uint32_t lastDisconnectTime;    ///< Last disconnection timestamp (millis)
    };

    /**
     * @brief Event callback function type
     * @param event The WiFi event that occurred
     * @param rssi Current RSSI value (meaningful for CONNECTED and RSSI_LOW events)
     */
    using EventCallback = std::function<void(WiFiEvent event, int8_t rssi)>;

    /**
     * @brief Constructor
     * @param ssid WiFi network SSID
     * @param password WiFi network password
     * @param autoReconnect Enable automatic reconnection (default: true)
     * @param reconnectIntervalMs Initial reconnect interval in milliseconds (default: 1000)
     */
    WiFiConnectionService(
        const char* ssid,
        const char* password,
        bool autoReconnect = true,
        uint32_t reconnectIntervalMs = 1000
    );

    /**
     * @brief Destructor
     */
    ~WiFiConnectionService();

    /**
     * @brief Initialize WiFi service
     * @return true if initialization successful, false otherwise
     */
    bool initialize();

    /**
     * @brief Connect to WiFi network
     * @param timeoutMs Connection timeout in milliseconds (default: 10000)
     * @return true if connection successful, false otherwise
     */
    bool connect(uint32_t timeoutMs = 10000);

    /**
     * @brief Disconnect from WiFi network
     */
    void disconnect();

    /**
     * @brief Check if currently connected to WiFi
     * @return true if connected, false otherwise
     */
    bool isConnected() const;

    /**
     * @brief Get current WiFi status
     * @return Current WiFi status
     */
    WiFiStatus getStatus() const;

    /**
     * @brief Update service (call this in main loop)
     * Should be called regularly to handle reconnection logic
     */
    void update();

    /**
     * @brief Register event callback
     * @param callback Callback function to register
     */
    void onEvent(EventCallback callback);

    /**
     * @brief Get current RSSI value
     * @return RSSI in dBm, or 0 if not connected
     */
    int8_t getRSSI() const;

    /**
     * @brief Get WiFi statistics
     * @return WiFi statistics structure
     */
    WiFiStats getStats() const;

    /**
     * @brief Set auto-reconnect enabled/disabled
     * @param enabled true to enable auto-reconnect, false to disable
     */
    void setAutoReconnect(bool enabled);

    /**
     * @brief Get auto-reconnect status
     * @return true if auto-reconnect is enabled, false otherwise
     */
    bool getAutoReconnect() const;

    /**
     * @brief Set RSSI threshold for low signal warning
     * @param threshold RSSI threshold in dBm (default: -80)
     */
    void setRSSIThreshold(int8_t threshold);

    /**
     * @brief Get local IP address
     * @return Local IP address as string, or empty string if not connected
     */
    String getLocalIP() const;

    /**
     * @brief Get MAC address
     * @return MAC address as string
     */
    String getMACAddress() const;

    /**
     * @brief Reset WiFi statistics
     */
    void resetStats();

private:
    // Configuration
    const char* m_ssid;
    const char* m_password;
    bool m_autoReconnect;
    uint32_t m_reconnectIntervalMs;
    int8_t m_rssiThreshold;

    // State
    WiFiStatus m_status;
    uint32_t m_lastReconnectAttempt;
    uint32_t m_currentReconnectInterval;
    uint32_t m_reconnectBackoffMultiplier;
    uint32_t m_maxReconnectInterval;
    uint32_t m_connectionStartTime;

    // Statistics
    WiFiStats m_stats;

    // Event callbacks
    std::vector<EventCallback> m_callbacks;

    // Private methods
    void handleDisconnection();
    void attemptReconnect();
    void updateRSSI();
    void notifyEvent(WiFiEvent event, int8_t rssi = 0);
    void resetReconnectBackoff();
    void increaseReconnectBackoff();
};

#endif // WIFI_CONNECTION_SERVICE_H
