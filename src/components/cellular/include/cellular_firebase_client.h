/**
 * @file cellular_firebase_client.h
 * @brief Firebase Realtime Database Client over Cellular HTTP
 * 
 * Uses CellularHTTPClient to communicate with Firebase REST API
 * Supports: PUT, PATCH, POST, GET, DELETE operations
 */

#ifndef CELLULAR_FIREBASE_CLIENT_H
#define CELLULAR_FIREBASE_CLIENT_H

#include <Arduino.h>
#include <map>
#include <vector>
#include "cellular_http_client.h"

/**
 * @brief Firebase REST API Client for ESP32 Cellular Connection
 * 
 * Firebase REST API Documentation:
 * https://firebase.google.com/docs/reference/rest/database
 */
class CellularFirebaseClient {
public:
    /**
     * @brief Upload result status
     */
    enum class UploadStatus {
        SUCCESS,
        FAILED_CONNECTION,
        FAILED_AUTH,
        FAILED_NETWORK,
        FAILED_TIMEOUT,
        FAILED_PARSE,
        FAILED_UNKNOWN
    };

    /**
     * @brief Upload result structure
     */
    struct UploadResult {
        UploadStatus status;
        int httpCode;
        String message;
        uint32_t responseTime;
        
        UploadResult() : status(UploadStatus::FAILED_UNKNOWN), httpCode(0), responseTime(0) {}
        bool success() const { return status == UploadStatus::SUCCESS; }
    };

    /**
     * @brief Firebase statistics
     */
    struct Stats {
        uint32_t totalUploads;
        uint32_t successfulUploads;
        uint32_t failedUploads;
        uint32_t totalBytesUpload;
        uint32_t totalBytesDownload;
        uint32_t avgResponseTime;
    };

    /**
     * @brief Constructor
     * @param httpClient Pointer to initialized CellularHTTPClient
     * @param firebaseHost Firebase database host (e.g., "project-id.firebaseio.com" or full URL)
     * @param authSecret Firebase database secret for authentication
     * @param gatewayId Gateway identifier (e.g., MAC address)
     */
    CellularFirebaseClient(CellularHTTPClient* httpClient,
                          const String& firebaseHost,
                          const String& authSecret,
                          const String& gatewayId);

    /**
     * @brief Initialize Firebase client
     * @return true if successful
     */
    bool initialize();

    // ===== High-Level Data Upload Methods =====

    /**
     * @brief Upload sensor data to Firebase
     * @param nodeId Node identifier (e.g., "0x1234")
     * @param temperature Temperature value
     * @param humidity Humidity value
     * @param rssi Signal strength
     * @param snr Signal-to-noise ratio
     * @return Upload result
     */
    UploadResult uploadSensorData(const String& nodeId, 
                                  float temperature, 
                                  float humidity,
                                  int8_t rssi, 
                                  float snr);

    /**
     * @brief Upload gateway status to Firebase
     * @param nodesCount Number of nodes in network
     * @param packetsRx Packets received
     * @param packetsTx Packets transmitted
     * @param cellularRssi Cellular signal strength
     * @param freeHeap Free heap memory
     * @param uptime Uptime in seconds
     * @return Upload result
     */
    UploadResult uploadGatewayStatus(uint16_t nodesCount,
                                     uint32_t packetsRx,
                                     uint32_t packetsTx,
                                     int8_t cellularRssi,
                                     uint32_t freeHeap,
                                     uint32_t uptime);

    /**
     * @brief Upload routing table to Firebase
     * @param routes JSON string of routing table
     * @return Upload result
     */
    UploadResult uploadRoutingTable(const String& routesJson);

    /**
     * @brief Log event to Firebase
     * @param eventType Event type (e.g., "node_joined", "connection_lost")
     * @param nodeId Node identifier
     * @param details Event details
     * @return Upload result
     */
    UploadResult logEvent(const String& eventType,
                         const String& nodeId,
                         const String& details);

    // ===== Low-Level Firebase REST API Methods =====

    /**
     * @brief PUT - Write/overwrite data at path
     * @param path Firebase path (e.g., "/users/uid/gateways/mac/status")
     * @param jsonData JSON string to write
     * @return Upload result
     */
    UploadResult put(const String& path, const String& jsonData);

    /**
     * @brief PATCH - Update specific fields at path
     * @param path Firebase path
     * @param jsonData JSON string with fields to update
     * @return Upload result
     */
    UploadResult patch(const String& path, const String& jsonData);

    /**
     * @brief POST - Push new child with auto-generated key
     * @param path Firebase path
     * @param jsonData JSON string to push
     * @return Upload result
     */
    UploadResult post(const String& path, const String& jsonData);

    /**
     * @brief GET - Read data from path
     * @param path Firebase path
     * @param result String to store result
     * @return Upload result
     */
    UploadResult get(const String& path, String& result);

    /**
     * @brief DELETE - Remove data at path
     * @param path Firebase path
     * @return Upload result
     */
    UploadResult deleteData(const String& path);

    // ===== Configuration & Status =====

    /**
     * @brief Set user ID for multi-user support
     * @param userId Firebase Auth UID
     */
    void setUserId(const String& userId);

    /**
     * @brief Enable/disable automatic timestamp
     * @param enabled If true, add "timestamp" field to all uploads
     */
    void setAutoTimestamp(bool enabled);

    /**
     * @brief Set retry configuration
     * @param maxRetries Maximum number of retry attempts
     * @param retryDelayMs Delay between retries (ms)
     */
    void setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs);

    /**
     * @brief Get Firebase statistics
     * @return Statistics structure
     */
    Stats getStats() const { return m_stats; }

    /**
     * @brief Get last error message
     * @return Error message string
     */
    String getLastError() const { return m_lastError; }

    /**
     * @brief Reset statistics
     */
    void resetStats();

private:
    // HTTP Client
    CellularHTTPClient* m_httpClient;

    // Firebase Configuration
    String m_firebaseHost;      // e.g., "project-id.firebaseio.com"
    String m_authSecret;        // Database secret
    String m_gatewayId;         // Gateway MAC or ID
    String m_userId;            // Optional: Firebase Auth UID for multi-user

    // Settings
    bool m_autoTimestamp;       // Auto-add timestamp to uploads
    uint8_t m_maxRetries;       // Max retry attempts
    uint32_t m_retryDelayMs;    // Delay between retries

    // Statistics
    Stats m_stats;
    String m_lastError;

    // Helper Methods

    /**
     * @brief Build Firebase REST API URL
     * @param path Firebase path
     * @param method HTTP method (for query params)
     * @return Full URL with host, path, and auth
     */
    String buildUrl(const String& path, const String& method = "PUT");

    /**
     * @brief Add timestamp to JSON
     * @param json Input JSON string
     * @return JSON with timestamp field added
     */
    String addTimestamp(const String& json);

    /**
     * @brief Execute HTTP request with retry
     * @param method HTTP method
     * @param url Full URL
     * @param body Request body (for PUT/PATCH/POST)
     * @return Upload result
     */
    UploadResult executeRequest(CellularHTTPClient::HTTPMethod method,
                               const String& url,
                               const String& body = "");

    /**
     * @brief Convert HTTP status code to upload status
     * @param httpCode HTTP status code
     * @return Upload status
     */
    UploadStatus httpCodeToStatus(int httpCode);

    static const char* TAG;
};

#endif // CELLULAR_FIREBASE_CLIENT_H
