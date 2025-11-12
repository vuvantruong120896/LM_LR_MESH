#ifndef CELLULAR_CONNECTION_SERVICE_H
#define CELLULAR_CONNECTION_SERVICE_H

#include <Arduino.h>
#include "cellular_uart.h"
#include "at_command_handler.h"
#include <functional>

/**
 * @brief Cellular Connection Service for A7682S Module
 * 
 * This service provides high-level network connection management:
 * - Network registration and attachment
 * - PDP context activation
 * - APN configuration
 * - Auto-reconnect with exponential backoff
 * - Event callbacks for connection state changes
 * - Signal quality monitoring
 * - Connection statistics
 * 
 * @note This is the cellular equivalent of WiFiConnectionService
 */
class CellularConnectionService {
public:
    /**
     * @brief Connection events
     */
    enum class Event {
        CONNECTED,          ///< Successfully connected to network
        DISCONNECTED,       ///< Disconnected from network
        RECONNECTING,       ///< Attempting to reconnect
        CONNECTION_FAILED,  ///< Connection attempt failed
        SIGNAL_LOW,         ///< Signal quality below threshold
        PDP_ACTIVATED,      ///< PDP context activated
        PDP_DEACTIVATED     ///< PDP context deactivated
    };

    /**
     * @brief Connection status
     */
    enum class Status {
        DISCONNECTED,       ///< Not connected
        INITIALIZING,       ///< Initialization in progress
        REGISTERING,        ///< Network registration in progress
        REGISTERED,         ///< Registered on network
        ACTIVATING_PDP,     ///< PDP context activation in progress
        CONNECTED,          ///< Fully connected (PDP active)
        RECONNECTING,       ///< Reconnection in progress
        FAILED              ///< Connection failed
    };

    /**
     * @brief Network registration state
     */
    enum class RegState {
        NOT_REGISTERED = 0,         ///< Not registered, not searching
        REGISTERED_HOME = 1,        ///< Registered, home network
        SEARCHING = 2,              ///< Not registered, searching
        DENIED = 3,                 ///< Registration denied
        UNKNOWN = 4,                ///< Unknown
        REGISTERED_ROAMING = 5      ///< Registered, roaming
    };

    /**
     * @brief Connection statistics
     */
    struct Stats {
        uint32_t connectionAttempts;        ///< Total connection attempts
        uint32_t successfulConnections;     ///< Successful connections
        uint32_t failedConnections;         ///< Failed connections
        uint32_t reconnectAttempts;         ///< Reconnection attempts
        uint32_t lastConnectTime;           ///< Last successful connection time (millis)
        uint32_t totalConnectedTime;        ///< Total time connected (ms)
        uint32_t lastDisconnectTime;        ///< Last disconnect time (millis)
        int8_t currentRSSI;                 ///< Current signal quality (0-31)
        RegState registrationState;         ///< Current registration state
    };

    /**
     * @brief APN Configuration
     */
    struct APNConfig {
        String apn;         ///< APN name (e.g., "internet", "v-internet")
        String username;    ///< Username (empty if not required)
        String password;    ///< Password (empty if not required)
        
        APNConfig(const char* apn = "", const char* user = "", const char* pass = "")
            : apn(apn), username(user), password(pass) {}
    };

    /**
     * @brief Event callback function type
     * @param event Event type
     * @param rssi Current RSSI (meaningful for CONNECTED and SIGNAL_LOW events)
     */
    using EventCallback = std::function<void(Event event, int8_t rssi)>;
    
    /**
     * @brief Secondary URC callback (for SSL/TCP clients)
     * @param urc URC string
     */
    using SecondaryURCCallback = std::function<void(const String& urc)>;

    /**
     * @brief Constructor
     * @param apnConfig APN configuration
     * @param autoReconnect Enable auto-reconnect (default: true)
     * @param reconnectIntervalMs Initial reconnect interval (default: 5000ms)
     */
    explicit CellularConnectionService(
        const APNConfig& apnConfig,
        bool autoReconnect = true,
        uint32_t reconnectIntervalMs = 5000
    );

    /**
     * @brief Destructor
     */
    ~CellularConnectionService();

    /**
     * @brief Initialize cellular module
     * 
     * Steps:
     * 1. Power on module
     * 2. Test AT communication
     * 3. Configure module (echo off, error format)
     * 4. Check SIM card
     * 5. Get IMEI
     * 
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Connect to cellular network
     * 
     * Steps:
     * 1. Wait for network registration
     * 2. Configure APN
     * 3. Attach to GPRS/LTE
     * 4. Activate PDP context
     * 5. Get IP address
     * 
     * @param timeoutMs Connection timeout (default: 60000ms)
     * @return true if connection successful
     */
    bool connect(uint32_t timeoutMs = 60000);

    /**
     * @brief Disconnect from network
     * 
     * Steps:
     * 1. Deactivate PDP context
     * 2. Detach from GPRS
     * 
     * @return true if disconnect successful
     */
    bool disconnect();

    /**
     * @brief Check if connected to network
     * @return true if connected (PDP context active)
     */
    bool isConnected() const;

    /**
     * @brief Get current connection status
     * @return Status
     */
    Status getStatus() const;

    /**
     * @brief Update connection status (call in main loop)
     * 
     * This handles:
     * - Auto-reconnect if connection lost
     * - Signal quality monitoring
     * - URC processing
     */
    void update();

    /**
     * @brief Register event callback
     * @param callback Callback function
     */
    void onEvent(EventCallback callback);

    /**
     * @brief Get signal quality (RSSI)
     * @return RSSI value (0-31, 99=unknown)
     */
    int8_t getSignalQuality() const;

    /**
     * @brief Get signal strength in dBm
     * @return RSSI in dBm (-113 to -51 dBm, or -999 if unknown)
     * 
     * Conversion formula:
     * - 0-1: -113 dBm or less
     * - 2-30: -111 to -53 dBm
     * - 31: -51 dBm or greater
     * - 99: Unknown
     */
    int16_t getSignalStrength() const {
        int8_t csq = getSignalQuality();
        if (csq == 99 || csq < 0) return -999;  // Unknown
        if (csq == 0) return -113;
        if (csq == 1) return -111;
        if (csq == 31) return -51;
        // Linear interpolation: CSQ 2-30 maps to -111 to -53 dBm
        return -113 + (csq * 2);
    }

    /**
     * @brief Get IMEI (device identifier)
     * @return IMEI string (15 digits)
     */
    String getIMEI() const;

    /**
     * @brief Get current operator name
     * @return Operator name
     */
    String getOperator() const;

    /**
     * @brief Get IP address assigned by network
     * @return IP address string
     */
    String getIPAddress() const;

    /**
     * @brief Get network registration state
     * @return RegState
     */
    RegState getRegistrationState() const;

    /**
     * @brief Get connection statistics
     * @return Stats structure
     */
    Stats getStats() const;

    /**
     * @brief Get AT command handler for advanced usage
     * @return Pointer to AT handler (do not delete!)
     */
    ATCommandHandler* getATHandler() const;

    /**
     * @brief Sync ESP32 system time from modem network time.
     * 
     * Reads time via AT+CCLK? and sets system clock. Returns true on success.
     */
    bool syncTimeFromNetwork();
    
    /**
     * @brief Register secondary URC callback (for SSL/TCP clients)
     * 
     * This allows SSL/TCP clients to receive URCs without interfering
     * with ConnectionService's URC handling.
     * 
     * @param callback Callback function
     */
    void registerSecondaryURCCallback(SecondaryURCCallback callback);

    /**
     * @brief Enable/disable auto-reconnect
     * @param enable true to enable
     */
    void setAutoReconnect(bool enable);

    /**
     * @brief Check if auto-reconnect is enabled
     * @return true if enabled
     */
    bool getAutoReconnect() const;

    /**
     * @brief Set signal quality threshold for SIGNAL_LOW event
     * @param threshold RSSI threshold (0-31, default: 10)
     */
    void setSignalQualityThreshold(int8_t threshold);

    /**
     * @brief Get local IP address (alias for getIPAddress)
     * @return IP address string
     */
    String getLocalIP() const { return getIPAddress(); }

    /**
     * @brief Get MAC address equivalent (IMEI for cellular)
     * @return IMEI string
     */
    String getMACAddress() const { return getIMEI(); }

private:
    // Hardware
    CellularUART* m_uart;
    ATCommandHandler* m_atHandler;

    // Configuration
    APNConfig m_apnConfig;
    bool m_autoReconnect;
    uint32_t m_reconnectIntervalMs;

    // State
    Status m_status;
    RegState m_registrationState;
    String m_imei;
    String m_operator;
    String m_ipAddress;
    int8_t m_currentRSSI;
    int8_t m_signalThreshold;

    // Reconnect management
    uint32_t m_lastReconnectAttempt;
    uint32_t m_currentReconnectInterval;
    float m_reconnectBackoffMultiplier;
    uint32_t m_maxReconnectInterval;

    // PDP activation state
    volatile bool m_netOpenSuccess;
    volatile bool m_netOpenReceived;

    // Statistics
    Stats m_stats;
    uint32_t m_connectionStartTime;

    // Event callback
    EventCallback m_eventCallback;
    
    // Secondary URC callback (for SSL/TCP clients)
    SecondaryURCCallback m_secondaryURCCallback;

    // Constants
    static const char* TAG;
    static constexpr uint32_t MIN_RECONNECT_INTERVAL = 5000;   // 5 seconds
    static constexpr uint32_t MAX_RECONNECT_INTERVAL = 300000; // 5 minutes
    static constexpr float RECONNECT_BACKOFF_MULTIPLIER = 1.5f;
    static constexpr int8_t DEFAULT_SIGNAL_THRESHOLD = 10;     // RSSI < 10 = weak

    // Private methods
    bool powerOnModule();
    bool checkSIMCard();
    bool waitForNetworkRegistration(uint32_t timeoutMs);
    bool configureAPN();
    bool attachGPRS();
    bool activatePDPContext();
    bool deactivatePDPContext();
    bool detachGPRS();
    bool updateSignalQuality();
    bool updateRegistrationState();
    bool updateIPAddress();
    void resetReconnectBackoff();
    void triggerEvent(Event event, int8_t rssi = 0);
    void handleURC(const String& urc);
    
    /**
     * @brief Convert RegState to string for logging
     */
    static const char* regStateToString(RegState state);
    
    /**
     * @brief Convert Status to string for logging
     */
    static const char* statusToString(Status status);
};

#endif // CELLULAR_CONNECTION_SERVICE_H
