/**
 * @file cellular_firebase_https_client.h
 * @brief Firebase HTTPS Client using CellularSSLClient
 * 
 * Simplified Firebase client that uses SSL/TLS for secure HTTPS connections
 * This is specifically designed for Firebase Realtime Database REST API over HTTPS
 */

#ifndef CELLULAR_FIREBASE_HTTPS_CLIENT_H
#define CELLULAR_FIREBASE_HTTPS_CLIENT_H

#include <Arduino.h>
#include "cellular_ssl_client.h"
#include "mesh_utils.h"  // For sensorData, RouteNode
#include <ArduinoJson.h>
#include <vector>

/**
 * @brief Firebase HTTPS Client for secure connections
 * 
 * Uses CellularSSLClient for HTTPS (port 443) connections to Firebase
 */
class CellularFirebaseHTTPSClient {
public:
    struct UploadResult {
        bool success;
        int httpCode;
        String message;
        String response;        // Response body (for GET requests)
        uint32_t responseTime;
        
        UploadResult() : success(false), httpCode(0), responseTime(0) {}
    };

    /**
     * @brief Constructor
     * @param sslClient Pointer to initialized CellularSSLClient
     * @param firebaseHost Firebase host (e.g., "project-id.firebaseio.com")
     * @param authSecret Firebase auth secret
     * @param gatewayId Gateway identifier (MAC address)
     */
    CellularFirebaseHTTPSClient(CellularSSLClient* sslClient,
                               const String& firebaseHost,
                               const String& authSecret,
                               const String& gatewayId);

    /**
     * @brief Initialize client
     */
    bool initialize();

    /**
     * @brief Test connection to Firebase
     */
    bool testConnection();

    /**
     * @brief Upload sensor data
     */
    UploadResult uploadSensorData(const sensorData& data, int8_t rssi, float snr);

    /**
     * @brief Upload routing table
     */
    UploadResult uploadRoutingTable(const std::vector<RouteNode>& routes);

    /**
     * @brief Upload gateway status
     */
    UploadResult uploadGatewayStatus(uint16_t nodeCount, uint32_t rxPackets, 
                                     uint32_t txPackets, int16_t rssi,
                                     uint32_t freeHeap, uint32_t uptime);

    /**
     * @brief Log event to Firebase
     */
    bool logEvent(const String& eventType, const String& nodeId, const String& message);

    /**
     * @brief Set user context for multi-user paths
     */
    void setUserContext(const String& userUID, const String& gatewayMAC);

    /**
     * @brief Generic HTTP GET request
     * @param path Firebase path (e.g., "users/uid/commands/mac/pending.json")
     * @return UploadResult with response in message field
     */
    UploadResult httpGet(const String& path);

    /**
     * @brief Generic HTTP PUT request
     * @param path Firebase path
     * @param jsonData JSON data to upload
     * @return UploadResult
     */
    UploadResult httpPut(const String& path, const String& jsonData);

    /**
     * @brief Generic HTTP DELETE request
     * @param path Firebase path
     * @return UploadResult
     */
    UploadResult httpDelete(const String& path);

private:
    CellularSSLClient* m_sslClient;
    String m_firebaseHost;
    String m_authSecret;
    String m_gatewayId;
    String m_userUID;
    String m_gatewayMAC;

    // Helper methods
    String buildFirebaseURL(const String& path);
    UploadResult sendHTTPSRequest(const String& method, const String& path, const String& body);
    String buildSensorDataJSON(const sensorData& data, int8_t rssi, float snr);
    String buildRoutingTableJSON(const std::vector<RouteNode>& routes);
    String buildGatewayStatusJSON(uint16_t nodeCount, uint32_t rxPackets, 
                                   uint32_t txPackets, int16_t rssi,
                                   uint32_t freeHeap, uint32_t uptime);
    int parseHTTPResponse(const String& response, String& body);
};

#endif // CELLULAR_FIREBASE_HTTPS_CLIENT_H
