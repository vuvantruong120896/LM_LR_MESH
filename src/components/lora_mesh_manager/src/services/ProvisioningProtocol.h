#ifndef _PROVISIONING_PROTOCOL_H
#define _PROVISIONING_PROTOCOL_H

#include <Arduino.h>
#include <cstdint>

// Provisioning packet types
#define PROVISION_REQUEST_PACKET    0xE0
#define PROVISION_RESPONSE_PACKET   0xE1
#define PROVISION_COMPLETE_PACKET   0xE2
#define PROVISION_REJECT_PACKET     0xE3

// Provisioning status codes
enum ProvisioningStatus : uint8_t {
    PROVISION_SUCCESS = 0x00,
    PROVISION_NETWORK_FULL = 0x01,
    PROVISION_INVALID_REQUEST = 0x02,
    PROVISION_AUTH_FAILED = 0x03,
    PROVISION_ADDRESS_CONFLICT = 0x04,
    PROVISION_BRIDGE_BUSY = 0x05,
    PROVISION_TIMEOUT = 0x06,
    PROVISION_INTERNAL_ERROR = 0x07
};

// Authentication method types
enum AuthMethod : uint8_t {
    AUTH_METHOD_OPEN = 0x00,        // No authentication (for testing)
    AUTH_METHOD_CHALLENGE = 0x01,   // Challenge-response
    AUTH_METHOD_PSK = 0x02,         // Pre-shared key
    AUTH_METHOD_CERTIFICATE = 0x03  // Certificate-based (future)
};

// Device capability flags
enum DeviceCapability : uint8_t {
    CAPABILITY_BASIC_NODE = 0x01,
    CAPABILITY_ROUTER_NODE = 0x02,
    CAPABILITY_SENSOR_NODE = 0x04,
    CAPABILITY_ACTUATOR_NODE = 0x08,
    CAPABILITY_BRIDGE_CAPABLE = 0x10
};

/**
 * @brief Provisioning Request Packet
 * Sent by unprovisioned node to bridge to request network membership
 */
struct __attribute__((packed)) ProvisionRequestPacket {
    uint8_t packetType;             // PROVISION_REQUEST_PACKET
    uint8_t deviceUUID[16];         // Unique device identifier
    uint8_t deviceType;             // Device capability flags
    uint8_t authMethod;             // Requested authentication method
    uint16_t requestedAddress;      // Preferred address (0 = any address)
    uint32_t timestamp;             // Request timestamp (anti-replay)
    uint8_t deviceName[32];         // Human-readable device name
    uint8_t challengeResponse[16];  // Response to bridge challenge (if applicable)
    uint8_t deviceInfo[32];         // Additional device information
    uint8_t mac[4];                 // Message Authentication Code
};

/**
 * @brief Provisioning Response Packet  
 * Sent by bridge to node with provisioning decision and network credentials
 */
struct __attribute__((packed)) ProvisionResponsePacket {
    uint8_t packetType;             // PROVISION_RESPONSE_PACKET
    uint8_t status;                 // Provisioning status (ProvisioningStatus)
    uint16_t assignedAddress;       // Assigned unicast address
    uint8_t networkKey[16];         // Network encryption key
    uint8_t authToken[8];           // Authentication token
    uint16_t networkId;             // Network identifier
    uint8_t keyVersion;             // Network key version
    uint32_t provisionTime;         // Provisioning timestamp
    uint16_t bridgeAddress;         // Bridge address for reference
    uint8_t networkConfig[16];      // Additional network configuration
    uint8_t challenge[16];          // Challenge for next authentication
    uint8_t mac[4];                 // Message Authentication Code
};

/**
 * @brief Provisioning Complete Packet
 * Sent by node to confirm successful provisioning and network join
 */
struct __attribute__((packed)) ProvisionCompletePacket {
    uint8_t packetType;             // PROVISION_COMPLETE_PACKET
    uint16_t nodeAddress;           // Confirmed node address
    uint8_t status;                 // Join status confirmation
    uint32_t timestamp;             // Completion timestamp
    uint8_t deviceStatus[16];       // Device status information
    uint8_t mac[4];                 // Message Authentication Code
};

/**
 * @brief Provisioning Reject Packet
 * Sent by bridge to reject provisioning request
 */
struct __attribute__((packed)) ProvisionRejectPacket {
    uint8_t packetType;             // PROVISION_REJECT_PACKET
    uint8_t rejectReason;           // Rejection reason (ProvisioningStatus)
    uint32_t retryAfter;            // Suggested retry time (seconds)
    uint8_t additionalInfo[32];     // Additional rejection information
    uint8_t mac[4];                 // Message Authentication Code
};

/**
 * @brief Provisioning session state
 */
struct ProvisioningSession {
    uint16_t nodeAddress;           // Node address in session
    uint8_t deviceUUID[16];         // Device UUID
    uint8_t sessionState;           // Current session state
    uint32_t sessionStart;          // Session start timestamp
    uint32_t lastActivity;          // Last activity timestamp
    uint8_t challenge[16];          // Current challenge
    uint8_t attemptCount;           // Authentication attempt count
    bool isActive;                  // Session active flag
};

// Session states
enum ProvisioningSessionState : uint8_t {
    SESSION_IDLE = 0x00,
    SESSION_CHALLENGE_SENT = 0x01,
    SESSION_RESPONSE_RECEIVED = 0x02,
    SESSION_CREDENTIALS_SENT = 0x03,
    SESSION_COMPLETE = 0x04,
    SESSION_FAILED = 0x05
};

// Provisioning configuration constants
#define MAX_PROVISIONING_SESSIONS       8
#define PROVISIONING_TIMEOUT_SECONDS    30
#define MAX_AUTH_ATTEMPTS               3
#define PROVISIONING_CHALLENGE_SIZE     16
#define PROVISIONING_MAC_SIZE           4

/**
 * @brief Provisioning Protocol Helper Functions
 */
class ProvisioningProtocol {
public:
    /**
     * @brief Calculate MAC for provisioning packet
     * @param packet Packet data
     * @param packetSize Packet size
     * @param key Authentication key
     * @param mac Output MAC buffer (4 bytes)
     * @return true if MAC calculated successfully
     */
    static bool calculatePacketMAC(const uint8_t* packet, size_t packetSize,
                                  const uint8_t* key, uint8_t* mac);
    
    /**
     * @brief Verify MAC for provisioning packet
     * @param packet Packet data
     * @param packetSize Packet size  
     * @param key Authentication key
     * @param expectedMAC Expected MAC (4 bytes)
     * @return true if MAC is valid
     */
    static bool verifyPacketMAC(const uint8_t* packet, size_t packetSize,
                               const uint8_t* key, const uint8_t* expectedMAC);
    
    /**
     * @brief Generate random challenge
     * @param challenge Output challenge buffer (16 bytes)
     */
    static void generateChallenge(uint8_t* challenge);
    
    /**
     * @brief Generate challenge response
     * @param challenge Input challenge (16 bytes)
     * @param deviceUUID Device UUID (16 bytes)
     * @param authKey Authentication key (16 bytes)
     * @param response Output response buffer (16 bytes)
     * @return true if response generated successfully
     */
    static bool generateChallengeResponse(const uint8_t* challenge,
                                        const uint8_t* deviceUUID,
                                        const uint8_t* authKey,
                                        uint8_t* response);
    
    /**
     * @brief Verify challenge response
     * @param challenge Original challenge (16 bytes)
     * @param deviceUUID Device UUID (16 bytes)
     * @param authKey Authentication key (16 bytes)
     * @param response Received response (16 bytes)
     * @return true if response is valid
     */
    static bool verifyChallengeResponse(const uint8_t* challenge,
                                      const uint8_t* deviceUUID,
                                      const uint8_t* authKey,
                                      const uint8_t* response);
    
    /**
     * @brief Create provisioning request packet
     * @param deviceUUID Device UUID
     * @param deviceType Device capability flags
     * @param deviceName Device name
     * @param authMethod Authentication method
     * @param packet Output packet buffer
     * @return true if packet created successfully
     */
    static bool createProvisionRequest(const uint8_t* deviceUUID,
                                     uint8_t deviceType,
                                     const char* deviceName,
                                     AuthMethod authMethod,
                                     ProvisionRequestPacket& packet);
    
    /**
     * @brief Create provisioning response packet
     * @param status Provisioning status
     * @param assignedAddress Assigned node address
     * @param networkKey Network key
     * @param authToken Auth token
     * @param networkId Network ID
     * @param packet Output packet buffer
     * @return true if packet created successfully
     */
    static bool createProvisionResponse(ProvisioningStatus status,
                                      uint16_t assignedAddress,
                                      const uint8_t* networkKey,
                                      const uint8_t* authToken,
                                      uint16_t networkId,
                                      ProvisionResponsePacket& packet);
    
    /**
     * @brief Create provisioning complete packet
     * @param nodeAddress Confirmed node address
     * @param status Join status
     * @param packet Output packet buffer
     * @return true if packet created successfully
     */
    static bool createProvisionComplete(uint16_t nodeAddress,
                                      ProvisioningStatus status,
                                      ProvisionCompletePacket& packet);
    
    /**
     * @brief Create provisioning reject packet
     * @param rejectReason Rejection reason
     * @param retryAfter Retry delay suggestion
     * @param packet Output packet buffer
     * @return true if packet created successfully
     */
    static bool createProvisionReject(ProvisioningStatus rejectReason,
                                    uint32_t retryAfter,
                                    ProvisionRejectPacket& packet);
    
    /**
     * @brief Get human-readable status string
     * @param status Provisioning status code
     * @return Status description string
     */
    static const char* getStatusString(ProvisioningStatus status);
    
    /**
     * @brief Get packet type name
     * @param packetType Packet type code
     * @return Packet type name string
     */
    static const char* getPacketTypeName(uint8_t packetType);

private:
    static const char* TAG;
};

#endif // _PROVISIONING_PROTOCOL_H