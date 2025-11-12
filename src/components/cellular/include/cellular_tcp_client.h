/**
 * @file cellular_tcp_client.h
 * @brief TCP/IP Client for A7682S Cellular Module
 * 
 * This class provides TCP socket operations over cellular network:
 * - DNS resolution (AT+CDNSGIP)
 * - TCP connection (AT+CIPOPEN)
 * - Send data (AT+CIPSEND)
 * - Receive data (AT+CIPRCV)
 * - Close connection (AT+CIPCLOSE)
 * 
 * Features:
 * - Multiple concurrent connections (up to 6 sockets)
 * - Automatic DNS resolution
 * - Connection timeout management
 * - Receive buffer management
 * - URC handling for async events
 * 
 * Socket states:
 * - CLOSED: Socket is closed
 * - RESOLVING_DNS: Resolving hostname to IP
 * - OPENING: Opening TCP connection
 * - CONNECTED: Connection established
 * - CLOSING: Closing connection
 * 
 * URCs handled:
 * - +CIPOPEN: <link_num>,<err>
 * - +CIPCLOSE: <link_num>,<err>
 * - +CIPRCV: <link_num>,<length>
 * - +CDNSGIP: <result>,<hostname>,<ip>
 * 
 * @author Phase 3 - TCP/IP Stack
 * @date 2025-01
 */

#ifndef CELLULAR_TCP_CLIENT_H
#define CELLULAR_TCP_CLIENT_H

#include <Arduino.h>
#include <functional>
#include "at_command_handler.h"

/**
 * @class CellularTCPClient
 * @brief TCP client implementation for cellular module
 */
class CellularTCPClient {
public:
    /**
     * @brief Socket state enumeration
     */
    enum class SocketState {
        CLOSED = 0,          ///< Socket closed
        RESOLVING_DNS,       ///< Resolving DNS
        OPENING,             ///< Opening connection
        CONNECTED,           ///< Connected
        CLOSING              ///< Closing connection
    };

    /**
     * @brief Connection error codes
     */
    enum class ConnectError {
        NONE = 0,                    ///< No error
        DNS_FAILED,                  ///< DNS resolution failed
        CONNECTION_TIMEOUT,          ///< Connection timeout
        CONNECTION_REFUSED,          ///< Connection refused by server
        NETWORK_ERROR,               ///< Network error
        SOCKET_NOT_AVAILABLE,        ///< No available socket
        ALREADY_CONNECTED,           ///< Socket already connected
        INVALID_PARAMETERS,          ///< Invalid host/port
        MODULE_ERROR                 ///< Module returned error
    };

    /**
     * @brief Event callback types
     */
    enum class Event {
        CONNECTED,           ///< Connection established
        DISCONNECTED,        ///< Connection closed
        DATA_AVAILABLE,      ///< Data received
        ERROR                ///< Error occurred
    };

    /**
     * @brief Event callback function
     * @param linkNum Socket link number (0-5)
     * @param event Event type
     * @param errorCode Error code (if EVENT_ERROR)
     */
    using EventCallback = std::function<void(uint8_t linkNum, Event event, int errorCode)>;

    /**
     * @brief Socket information structure
     */
    struct SocketInfo {
        SocketState state;               ///< Current state
        String remoteHost;               ///< Remote hostname
        String remoteIP;                 ///< Remote IP address
        uint16_t remotePort;             ///< Remote port
        uint32_t connectTime;            ///< Connection timestamp
        uint32_t lastActivityTime;       ///< Last activity timestamp
        uint32_t bytesSent;              ///< Total bytes sent
        uint32_t bytesReceived;          ///< Total bytes received
        uint16_t availableData;          ///< Bytes available to read
        
        SocketInfo() 
            : state(SocketState::CLOSED)
            , remotePort(0)
            , connectTime(0)
            , lastActivityTime(0)
            , bytesSent(0)
            , bytesReceived(0)
            , availableData(0)
        {}
    };

    /**
     * @brief TCP client statistics
     */
    struct Stats {
        uint32_t totalConnections;       ///< Total connections made
        uint32_t failedConnections;      ///< Failed connection attempts
        uint32_t totalBytesSent;         ///< Total bytes sent (all sockets)
        uint32_t totalBytesReceived;     ///< Total bytes received (all sockets)
        uint32_t dnsQueries;             ///< DNS queries performed
        uint32_t dnsFailed;              ///< Failed DNS queries
    };

    // Configuration constants
    static constexpr uint8_t MAX_SOCKETS = 6;           ///< Maximum concurrent sockets (A7682S limit)
    static constexpr uint32_t DEFAULT_CONNECT_TIMEOUT = 60000;  ///< Default connection timeout (60s)
    static constexpr uint32_t DEFAULT_DNS_TIMEOUT = 30000;      ///< Default DNS timeout (30s)
    static constexpr size_t MAX_RECEIVE_BUFFER = 2048;  ///< Max bytes per receive operation

    /**
     * @brief Constructor
     * @param atHandler Pointer to AT command handler
     */
    explicit CellularTCPClient(ATCommandHandler* atHandler);

    /**
     * @brief Destructor
     */
    ~CellularTCPClient();

    // ===== Connection Management =====

    /**
     * @brief Connect to remote server
     * @param host Hostname or IP address
     * @param port Remote port
     * @param timeoutMs Connection timeout in milliseconds
     * @return Socket link number (0-5) on success, -1 on failure
     * 
     * This method:
     * 1. Finds available socket
     * 2. Resolves hostname to IP (if needed)
     * 3. Opens TCP connection (AT+CIPOPEN)
     * 4. Waits for +CIPOPEN URC confirmation
     * 
     * Example:
     * @code
     * int socket = client.connect("httpbin.org", 80);
     * if (socket >= 0) {
     *     // Connection successful
     * }
     * @endcode
     */
    int connect(const String& host, uint16_t port, uint32_t timeoutMs = DEFAULT_CONNECT_TIMEOUT);

    /**
     * @brief Disconnect socket
     * @param linkNum Socket link number (0-5)
     * @return true if close command sent successfully
     * 
     * Sends AT+CIPCLOSE and waits for confirmation
     */
    bool disconnect(uint8_t linkNum);

    /**
     * @brief Disconnect all sockets
     */
    void disconnectAll();

    /**
     * @brief Check if socket is connected
     * @param linkNum Socket link number
     * @return true if socket is in CONNECTED state
     */
    bool connected(uint8_t linkNum) const;

    // ===== Data Transfer =====

    /**
     * @brief Send data to connected socket
     * @param linkNum Socket link number
     * @param data Data buffer to send
     * @param length Data length in bytes
     * @return Number of bytes sent, -1 on error
     * 
     * Uses AT+CIPSEND=<link>,<length> followed by raw data
     * 
     * Example:
     * @code
     * const char* request = "GET / HTTP/1.1\r\nHost: httpbin.org\r\n\r\n";
     * int sent = client.send(socket, request, strlen(request));
     * @endcode
     */
    int send(uint8_t linkNum, const uint8_t* data, size_t length);

    /**
     * @brief Send string data
     * @param linkNum Socket link number
     * @param data String to send
     * @return Number of bytes sent, -1 on error
     */
    int send(uint8_t linkNum, const String& data);

    /**
     * @brief Receive data from socket
     * @param linkNum Socket link number
     * @param buffer Buffer to store received data
     * @param maxLength Maximum bytes to read
     * @return Number of bytes received, -1 on error, 0 if no data
     * 
     * Uses AT+CIPRCV=<link>,<length>
     * Note: Available data is tracked via +CIPRCV URCs
     */
    int receive(uint8_t linkNum, uint8_t* buffer, size_t maxLength);

    /**
     * @brief Get number of bytes available to read
     * @param linkNum Socket link number
     * @return Number of bytes available
     */
    uint16_t available(uint8_t linkNum) const;

    /**
     * @brief Query available data from modem (manual poll)
     * @param linkNum Socket link number
     * @return true if query successful
     * 
     * Uses AT+CIPRXGET=4,<link> to query available data
     * Updates availableData counter in socket info
     * Use this for polling when +CIPRCV URC is not received
     */
    bool queryAvailableData(uint8_t linkNum);

    /**
     * @brief Peek at next byte without removing from buffer
     * @param linkNum Socket link number
     * @return Next byte value, -1 if no data
     */
    int peek(uint8_t linkNum);

    /**
     * @brief Flush receive buffer (discard all pending data)
     * @param linkNum Socket link number
     */
    void flush(uint8_t linkNum);

    // ===== DNS Resolution =====

    /**
     * @brief Resolve hostname to IP address
     * @param hostname Hostname to resolve
     * @param resultIP String to store resolved IP
     * @param timeoutMs DNS timeout
     * @return true if resolution successful
     * 
     * Uses AT+CDNSGIP=<hostname>
     * Waits for +CDNSGIP URC with result
     */
    bool resolveHost(const String& hostname, String& resultIP, uint32_t timeoutMs = DEFAULT_DNS_TIMEOUT);

    // ===== Status & Information =====

    /**
     * @brief Get socket information
     * @param linkNum Socket link number
     * @return SocketInfo structure
     */
    SocketInfo getSocketInfo(uint8_t linkNum) const;

    /**
     * @brief Get socket state
     * @param linkNum Socket link number
     * @return Current socket state
     */
    SocketState getSocketState(uint8_t linkNum) const;

    /**
     * @brief Get statistics
     * @return Stats structure
     */
    Stats getStats() const;

    /**
     * @brief Get number of active connections
     * @return Count of sockets in CONNECTED state
     */
    uint8_t getActiveConnectionCount() const;

    /**
     * @brief Find available socket
     * @return Socket link number (0-5), -1 if none available
     */
    int findAvailableSocket() const;

    // ===== Event Handling =====

    /**
     * @brief Register event callback
     * @param callback Callback function
     */
    void onEvent(EventCallback callback);

    /**
     * @brief Process URCs and update socket states
     * 
     * Call this regularly in main loop to:
     * - Process incoming URCs (+CIPOPEN, +CIPCLOSE, +CIPRCV)
     * - Update socket states
     * - Trigger event callbacks
     */
    void update();

    // ===== Utility =====

    /**
     * @brief Get last connection error
     * @return Last error code
     */
    ConnectError getLastError() const;

    /**
     * @brief Convert error code to string
     * @param error Error code
     * @return Error description
     */
    static const char* errorToString(ConnectError error);

    /**
     * @brief Convert socket state to string
     * @param state Socket state
     * @return State name
     */
    static const char* stateToString(SocketState state);

private:
    // AT command handler
    ATCommandHandler* m_atHandler;

    // Socket tracking
    SocketInfo m_sockets[MAX_SOCKETS];

    // Statistics
    Stats m_stats;
    
    // Event callback
    EventCallback m_eventCallback;    // Last error
    ConnectError m_lastError;

    // DNS resolution state
    struct {
        bool pending;
        String hostname;
        String resultIP;
        bool success;
    } m_dnsState;

    // Logging tag
    static const char* TAG;

    // ===== Private Methods =====

    /**
     * @brief Handle URC messages
     * @param urc URC string
     */
    void handleURC(const String& urc);

    /**
     * @brief Handle +CIPOPEN URC
     * @param linkNum Socket number
     * @param errorCode Error code (0=success)
     */
    void handleCIPOPEN(uint8_t linkNum, int errorCode);

    /**
     * @brief Handle +CIPCLOSE URC
     * @param linkNum Socket number
     * @param errorCode Error code
     */
    void handleCIPCLOSE(uint8_t linkNum, int errorCode);

    /**
     * @brief Handle +CIPRCV URC
     * @param linkNum Socket number
     * @param dataLength Available data length
     */
    void handleCIPRCV(uint8_t linkNum, uint16_t dataLength);

    /**
     * @brief Handle +CDNSGIP URC
     * @param result Result code (1=success)
     * @param hostname Hostname queried
     * @param ip Resolved IP address
     */
    void handleCDNSGIP(int result, const String& hostname, const String& ip);

    /**
     * @brief Trigger event callback
     * @param linkNum Socket number
     * @param event Event type
     * @param errorCode Error code (if applicable)
     */
    void triggerEvent(uint8_t linkNum, Event event, int errorCode = 0);

    /**
     * @brief Check if string is valid IP address
     * @param str String to check
     * @return true if valid IP (x.x.x.x format)
     */
    bool isValidIP(const String& str) const;

    /**
     * @brief Reset socket to initial state
     * @param linkNum Socket number
     */
    void resetSocket(uint8_t linkNum);
};

#endif // CELLULAR_TCP_CLIENT_H
