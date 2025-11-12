#ifndef CELLULAR_HTTP_CLIENT_H
#define CELLULAR_HTTP_CLIENT_H

#include <Arduino.h>
#include <map>
#include <vector>
#include "cellular_tcp_client.h"

/**
 * @brief HTTP Client for A7682S Cellular Module
 * 
 * Provides high-level HTTP/HTTPS operations built on CellularTCPClient:
 * - GET, POST, PUT, DELETE, PATCH methods
 * - Custom headers and query parameters
 * - Request/response body handling
 * - Chunked transfer encoding support
 * - JSON payload support (with ArduinoJson)
 * - Connection reuse (Keep-Alive)
 * 
 * @note Phase 4 of cellular integration
 */
class CellularHTTPClient {
public:
    /**
     * @brief HTTP methods
     */
    enum class HTTPMethod {
        GET,
        POST,
        PUT,
        DELETE,
        PATCH,
        HEAD
    };

    /**
     * @brief HTTP request structure
     */
    struct HTTPRequest {
        HTTPMethod method;
        String url;                              // Full URL (http://host:port/path?query)
        std::map<String, String> headers;        // Custom headers
        String body;                             // Request body (for POST/PUT/PATCH)
        uint32_t timeout;                        // Request timeout (ms)
        
        HTTPRequest() : method(HTTPMethod::GET), timeout(30000) {}
    };

    /**
     * @brief HTTP response structure
     */
    struct HTTPResponse {
        int statusCode;                          // HTTP status code (200, 404, etc.)
        String statusMessage;                    // Status message ("OK", "Not Found", etc.)
        std::map<String, String> headers;        // Response headers
        String body;                             // Response body
        size_t contentLength;                    // Content-Length header value
        bool chunked;                            // True if Transfer-Encoding: chunked
        bool success;                            // True if request completed successfully
        String errorMessage;                     // Error description if failed
        uint32_t responseTime;                   // Response time in milliseconds
        
        HTTPResponse() : statusCode(0), contentLength(0), chunked(false), 
                        success(false), responseTime(0) {}
    };

    /**
     * @brief Connection statistics
     */
    struct Stats {
        uint32_t totalRequests;
        uint32_t successfulRequests;
        uint32_t failedRequests;
        uint32_t totalBytesSent;
        uint32_t totalBytesReceived;
        uint32_t avgResponseTime;
    };

    /**
     * @brief Constructor
     * @param tcpClient Pointer to initialized CellularTCPClient
     */
    explicit CellularHTTPClient(CellularTCPClient* tcpClient);

    /**
     * @brief Destructor
     */
    ~CellularHTTPClient();

    /**
     * @brief Perform HTTP GET request
     * @param url Full URL (http://example.com/path)
     * @param headers Optional custom headers
     * @param timeout Request timeout (default: 30s)
     * @return HTTPResponse structure
     */
    HTTPResponse get(const String& url, 
                    const std::map<String, String>& headers = {},
                    uint32_t timeout = 30000);

    /**
     * @brief Perform HTTP POST request
     * @param url Full URL
     * @param body Request body
     * @param headers Optional custom headers
     * @param timeout Request timeout (default: 30s)
     * @return HTTPResponse structure
     */
    HTTPResponse post(const String& url,
                     const String& body,
                     const std::map<String, String>& headers = {},
                     uint32_t timeout = 30000);

    /**
     * @brief Perform HTTP PUT request
     * @param url Full URL
     * @param body Request body
     * @param headers Optional custom headers
     * @param timeout Request timeout (default: 30s)
     * @return HTTPResponse structure
     */
    HTTPResponse put(const String& url,
                    const String& body,
                    const std::map<String, String>& headers = {},
                    uint32_t timeout = 30000);

    /**
     * @brief Perform HTTP DELETE request
     * @param url Full URL
     * @param headers Optional custom headers
     * @param timeout Request timeout (default: 30s)
     * @return HTTPResponse structure
     */
    HTTPResponse deleteRequest(const String& url,
                               const std::map<String, String>& headers = {},
                               uint32_t timeout = 30000);

    /**
     * @brief Perform custom HTTP request
     * @param request HTTPRequest structure with all parameters
     * @return HTTPResponse structure
     */
    HTTPResponse request(const HTTPRequest& request);

    /**
     * @brief Set default User-Agent header
     * @param userAgent User-Agent string
     */
    void setUserAgent(const String& userAgent);

    /**
     * @brief Set default timeout for all requests
     * @param timeout Timeout in milliseconds
     */
    void setDefaultTimeout(uint32_t timeout);

    /**
     * @brief Enable/disable connection reuse (Keep-Alive)
     * @param enable True to enable
     */
    void setKeepAlive(bool enable);

    /**
     * @brief Get connection statistics
     * @return Stats structure
     */
    Stats getStats() const { return m_stats; }

    /**
     * @brief Reset statistics
     */
    void resetStats();

    /**
     * @brief Close any active connections
     */
    void closeConnection();

private:
    // TCP client
    CellularTCPClient* m_tcpClient;

    // Configuration
    String m_userAgent;
    uint32_t m_defaultTimeout;
    bool m_keepAlive;

    // Connection state
    int m_currentSocket;
    String m_currentHost;
    uint16_t m_currentPort;

    // Statistics
    Stats m_stats;

    // Constants
    static const char* TAG;
    static constexpr uint16_t HTTP_DEFAULT_PORT = 80;
    static constexpr uint16_t HTTPS_DEFAULT_PORT = 443;
    static constexpr size_t MAX_RESPONSE_SIZE = 16384;  // 16KB max response

    /**
     * @brief Parse URL into components
     * @param url Full URL string
     * @param protocol Output: "http" or "https"
     * @param host Output: hostname
     * @param port Output: port number
     * @param path Output: path + query
     * @return true if parsing successful
     */
    bool parseURL(const String& url, String& protocol, String& host, 
                  uint16_t& port, String& path);

    /**
     * @brief Build HTTP request string
     * @param method HTTP method
     * @param path Request path
     * @param host Host header value
     * @param headers Custom headers
     * @param body Request body
     * @return Complete HTTP request string
     */
    String buildRequest(HTTPMethod method, const String& path, const String& host,
                       const std::map<String, String>& headers, const String& body);

    /**
     * @brief Parse HTTP response
     * @param data Raw response data
     * @param response Output response structure
     * @return true if parsing successful
     */
    bool parseResponse(const String& data, HTTPResponse& response);

    /**
     * @brief Parse HTTP status line
     * @param line Status line (e.g., "HTTP/1.1 200 OK")
     * @param statusCode Output status code
     * @param statusMessage Output status message
     * @return true if parsing successful
     */
    bool parseStatusLine(const String& line, int& statusCode, String& statusMessage);

    /**
     * @brief Parse HTTP headers
     * @param data Header section
     * @param headers Output headers map
     * @param bodyStart Output: index where body starts
     * @return true if parsing successful
     */
    bool parseHeaders(const String& data, std::map<String, String>& headers, 
                     int& bodyStart);

    /**
     * @brief Decode chunked transfer encoding
     * @param chunkedData Raw chunked data
     * @param decodedData Output decoded data
     * @return true if decoding successful
     */
    bool decodeChunked(const String& chunkedData, String& decodedData);

    /**
     * @brief Get or create connection to host:port
     * @param host Hostname
     * @param port Port number
     * @return Socket number, or -1 on failure
     */
    int getConnection(const String& host, uint16_t port);

public:
    /**
     * @brief Convert HTTPMethod to string
     */
    static const char* methodToString(HTTPMethod method);

    /**
     * @brief Check if status code indicates success (2xx)
     */
    static bool isSuccessStatus(int statusCode) {
        return statusCode >= 200 && statusCode < 300;
    }
};

#endif // CELLULAR_HTTP_CLIENT_H
