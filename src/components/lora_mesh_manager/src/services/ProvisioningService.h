#ifndef _PROVISIONING_SERVICE_H
#define _PROVISIONING_SERVICE_H

#include <Arduino.h>
#include "ProvisioningProtocol.h"
#include "NVSStorageService.h"
#include "AddressManagementService.h"

/**
 * @brief Provisioning Service
 * Centralized service to manage device provisioning on the bridge and forward
 * provisioning packets to node applications when running on nodes.
 */
class ProvisioningService {
public:
    // Callback for node applications to receive provisioning packets (response/reject)
    typedef void (*NodePacketCallback)(uint8_t packetType, const uint8_t* packet, size_t packetSize, uint16_t senderAddress);

    // Statistics structure
    struct ProvisioningStats {
        uint32_t totalRequests;         // Total provisioning requests received
        uint32_t successfulProvisions;  // Successfully provisioned devices
        uint32_t rejectedRequests;      // Rejected requests
        uint32_t failedAuthentications; // Authentication failures
        uint32_t timeoutSessions;       // Timed out sessions
        uint32_t activeSessions;        // Currently active sessions
        uint32_t lastProvisionTime;     // Last successful provision timestamp
    };

    // Lifecycle
    static bool initialize();
    static bool isInitialized();
    static void shutdown();

    // Core Provisioning Operations (bridge side)
    static bool processProvisionRequest(const ProvisionRequestPacket& request, uint16_t senderAddress);
    static bool processProvisionComplete(const ProvisionCompletePacket& complete, uint16_t senderAddress);
    static void handleProvisioningTimeouts();

    // Session Management
    static uint8_t getActiveSessionCount();
    static ProvisioningSession* findSessionByUUID(const uint8_t* deviceUUID);
    static ProvisioningSession* findSessionByAddress(uint16_t nodeAddress);
    static ProvisioningSession* createSession(const uint8_t* deviceUUID, uint16_t tempAddress);
    static bool removeSession(ProvisioningSession* session);
    static void clearAllSessions();

    // Authentication and Authorization
    static bool authenticateRequest(const ProvisionRequestPacket& request, ProvisioningSession* session);
    static bool authorizeDevice(const ProvisionRequestPacket& request, const uint8_t* deviceUUID);
    static bool generateAuthChallenge(const uint8_t* deviceUUID, uint8_t* challenge);

    // Network Credential Management
    static bool getNetworkCredentials(uint8_t* networkKey, uint8_t* authToken,
                                      uint16_t* networkId, uint8_t* keyVersion);
    static bool isProvisioningReady();

    // Statistics and Monitoring
    static bool getProvisioningStats(ProvisioningStats& stats);
    static void resetStats();
    static bool getProvisioningStatus(char* statusBuffer, size_t maxSize);
    static bool exportSessionsJSON(char* jsonBuffer, size_t maxSize);

    // Packet Generation and Sending
    static bool sendProvisionResponse(uint16_t targetAddress, ProvisioningStatus status,
                                      uint16_t assignedAddress, ProvisioningSession* session);
    static bool sendProvisionReject(uint16_t targetAddress, ProvisioningStatus rejectReason,
                                    uint32_t retryAfter);

    // Configuration Management
    static void setMaxConcurrentSessions(uint8_t maxSessions);
    static void setProvisioningTimeout(uint32_t timeoutSeconds);
    static void setAuthenticationMethod(AuthMethod authMethod);
    static void setAutoAuthorization(bool enabled);

    // Packet classification/processing
    static bool isProvisioningPacket(uint8_t packetType);
    static bool processProvisioningPacket(const uint8_t* packet, size_t packetSize, uint16_t senderAddress);

    // Node callback management
    static void setNodePacketCallback(NodePacketCallback callback);
    static void clearNodePacketCallback();

private:
    static bool initialized;
    static const char* TAG;

    // Node packet callback for forwarding packets to application
    static NodePacketCallback nodePacketCallback;

    // Session management
    static ProvisioningSession sessions[MAX_PROVISIONING_SESSIONS];
    static uint8_t sessionCount;

    // Statistics
    static ProvisioningStats stats;

    // Configuration
    static uint8_t maxConcurrentSessions;
    static uint32_t provisioningTimeoutSeconds;
    static AuthMethod authenticationMethod;
    static bool autoAuthorizationEnabled;

    // Helpers
    static ProvisioningSession* findAvailableSession();
    static bool validateProvisionRequest(const ProvisionRequestPacket& request);
    static bool isDeviceAlreadyProvisioned(const uint8_t* deviceUUID);
    static uint32_t generateSessionId(const uint8_t* deviceUUID, uint32_t timestamp);
    static void logProvisioningEvent(const char* event, const uint8_t* deviceUUID = nullptr,
                                     const char* details = nullptr);
    static void updateStats(const char* event);
    static bool sendPacket(uint16_t targetAddress, const uint8_t* packet, size_t packetSize);
};

#endif // _PROVISIONING_SERVICE_H