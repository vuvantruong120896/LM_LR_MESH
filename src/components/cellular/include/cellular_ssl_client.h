/**
 * @file cellular_ssl_client.h
 * @brief SSL/TLS Client for A7682S Cellular Module
 * 
 * Provides HTTPS support using AT+CSSLCFG and AT+CCHOPEN commands
 * Used by CellularHTTPClient for HTTPS connections
 * 
 * AT Commands:
 * - AT+CSSLCFG="sslversion",<ctxindex>,<sslversion> - Set SSL version
 * - AT+CSSLCFG="authmode",<ctxindex>,<authmode> - Set auth mode
 * - AT+CCHSTART - Start HTTP service
 * - AT+CCHOPEN=<pdpidx>,"<url>",<port>,<mode> - Open HTTPS connection
 * - AT+CCHSEND=<sessionid>,<length> - Send HTTPS data
 * - AT+CCHRECV=<sessionid>,<length> - Receive HTTPS data
 * - AT+CCHCLOSE=<sessionid> - Close HTTPS connection
 * - AT+CCHSTOP - Stop HTTP service
 */

#ifndef CELLULAR_SSL_CLIENT_H
#define CELLULAR_SSL_CLIENT_H

#include <Arduino.h>
#include "at_command_handler.h"
#include "cellular_connection_service.h"

class CellularSSLClient {
public:
    enum class State {
        CLOSED,
        OPENING,
        CONNECTED,
        CLOSING,
        ERROR
    };

    /**
     * @brief Constructor
     * @param connectionService Pointer to cellular connection service
     */
    explicit CellularSSLClient(CellularConnectionService* connectionService);

    /**
     * @brief Destructor
     */
    ~CellularSSLClient();

    /**
     * @brief Initialize SSL client (start HTTP service)
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Connect to HTTPS server
     * @param host Server hostname (e.g., "firebase.googleapis.com")
     * @param port Server port (default: 443)
     * @param timeoutMs Connection timeout
     * @return true if connected
     */
    bool connect(const String& host, uint16_t port = 443, uint32_t timeoutMs = 30000);

    /**
     * @brief Send HTTPS request
     * @param request Full HTTP request string
     * @return Number of bytes sent, or -1 on error
     */
    int send(const String& request);

    /**
     * @brief Receive HTTPS response
     * @param buffer Buffer to store response
     * @param maxLength Maximum bytes to receive
     * @param timeoutMs Receive timeout
     * @return Number of bytes received, or -1 on error
     */
    int receive(char* buffer, size_t maxLength, uint32_t timeoutMs = 10000);

    /**
     * @brief Disconnect from server
     * @return true if successful
     */
    bool disconnect();

    /**
     * @brief Check if connected
     * @return true if connected
     */
    bool isConnected() const { return m_state == State::CONNECTED; }

    /**
     * @brief Get current state
     * @return Current state
     */
    State getState() const { return m_state; }

    /**
     * @brief Get AT command handler (for advanced usage)
     * @return Pointer to AT handler
     */
    ATCommandHandler* getATHandler() const;

private:
    CellularConnectionService* m_connectionService;
    ATCommandHandler* m_atHandler;
    State m_state;
    int m_sessionId;
    bool m_httpServiceStarted;

    // URC handling for +CCHOPEN
    volatile bool m_cchOpenReceived;
    volatile int m_cchOpenSessionId;
    volatile int m_cchOpenErrorCode;

    // URC-indicated available bytes for receive
    volatile int m_availableBytes = 0;


    // SSL configuration
    bool configureSSL();
    bool startHTTPService();
    bool stopHTTPService();
    
    // URC handler
    void handleURC(const String& urc);
};

#endif // CELLULAR_SSL_CLIENT_H
