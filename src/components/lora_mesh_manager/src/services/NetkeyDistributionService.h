#ifndef _NETKEY_DISTRIBUTION_SERVICE_H
#define _NETKEY_DISTRIBUTION_SERVICE_H

#include "../network/entities/packets/Packet.h"
#include "../network/entities/packets/PacketHeader.h"
#include "../network/entities/routingTable/NetworkNode.h"
#include "../../include/mesh_security.h"
#include "esp_log.h"

// Netkey distribution packet types
enum NetkeyPacketType : uint8_t {
    NETKEY_UPDATE_REQUEST = 0x90,    // Bridge → Node: New network key
    NETKEY_UPDATE_RESPONSE = 0x91,   // Node → Bridge: Confirmation
    NETKEY_UPDATE_BROADCAST = 0x92   // Bridge → All: Broadcast new key
};

// Netkey update status
enum NetkeyUpdateStatus : uint8_t {
    NETKEY_UPDATE_SUCCESS = 0x00,
    NETKEY_UPDATE_INVALID_KEY = 0x01,
    NETKEY_UPDATE_AUTH_FAILED = 0x02,
    NETKEY_UPDATE_INTERNAL_ERROR = 0x03
};

// Netkey update packet structure
struct NetkeyUpdatePacket {
    PacketHeader header;           // Standard packet header
    uint8_t netkeyType;           // NetkeyPacketType
    uint8_t keyVersion;           // Key version for tracking
    uint16_t networkId;           // Network identifier
    uint8_t networkKey[16];       // New network key (128-bit)
    uint8_t authToken[8];         // Authentication token
    uint32_t timestamp;           // Update timestamp
    uint8_t mac[4];               // MAC for integrity (using old key initially)
};

// Netkey response packet structure
struct NetkeyResponsePacket {
    PacketHeader header;           // Standard packet header
    uint8_t netkeyType;           // NetkeyPacketType
    uint8_t keyVersion;           // Key version being confirmed
    uint8_t status;               // NetkeyUpdateStatus
    uint32_t timestamp;           // Response timestamp
    uint8_t mac[4];               // MAC for integrity
};
#pragma pack()

/**
 * @brief Network Key Distribution Service
 * 
 * Handles distribution of network keys from Bridge to all nodes in the mesh.
 * Bridge acts as the key distribution center, receiving keys via UART and
 * securely distributing them to all mesh participants.
 */
class NetkeyDistributionService {
public:
    /**
     * @brief Initialize the netkey distribution service
     */
    static void initialize();
    
    /**
     * @brief Set callback for netkey update notifications
     * @param callback Function to call when new netkey is received
     */
    static void setNetkeyUpdateCallback(void (*callback)(const uint8_t* newKey, uint8_t version));
    
    /**
     * @brief Distribute new network key to all nodes (Bridge only)
     * @param networkKey New 128-bit network key
     * @param authToken Authentication token
     * @param networkId Network identifier
     * @param keyVersion Key version number
     * @return true if distribution started successfully
     */
    static bool distributeNetworkKey(const uint8_t* networkKey, 
                                   const uint8_t* authToken,
                                   uint16_t networkId, 
                                   uint8_t keyVersion);
    
    /**
     * @brief Distribute network key to all nodes in routing table (Simplified approach)
     * @param networkKey New 128-bit network key
     * @param authToken Authentication token
     * @param networkId Network identifier
     * @param keyVersion Key version number
     * @param nodes Array of nodes from routing table
     * @param nodeCount Number of nodes
     * @return true if distribution started successfully
     */
    static bool distributeNetkeyToAllNodes(const uint8_t* networkKey, 
                                         const uint8_t* authToken,
                                         uint16_t networkId, 
                                         uint8_t keyVersion,
                                         const NetworkNode* nodes,
                                         size_t nodeCount);
    
    /**
     * @brief Process received netkey packet (both Bridge and Node)
     * @param packet Raw packet data
     * @param packetSize Size of packet
     * @param senderAddress Address of sender
     * @return true if packet was handled
     */
    static bool processNetkeyPacket(const uint8_t* packet, 
                                  size_t packetSize,
                                  uint16_t senderAddress);
    
    /**
     * @brief Check if a packet is a netkey packet
     * @param packetType Packet type to check
     * @return true if it's a netkey packet type
     */
    static bool isNetkeyPacket(uint8_t packetType);
    
    /**
     * @brief Get current key version
     * @return Current network key version
     */
    static uint8_t getCurrentKeyVersion();
    
    /**
     * @brief Update local network key configuration
     * @param networkKey New network key
     * @param authToken New auth token
     * @param networkId Network ID
     * @param keyVersion Key version
     * @return true if update successful
     */
    static bool updateLocalNetworkKey(const uint8_t* networkKey,
                                    const uint8_t* authToken,
                                    uint16_t networkId,
                                    uint8_t keyVersion);

private:
    static void (*netkeyUpdateCallback)(const uint8_t* newKey, uint8_t version);
    static uint8_t currentKeyVersion;
    static uint32_t lastUpdateTimestamp;
    
    /**
     * @brief Send netkey update to specific node (Bridge only)
     * @param nodeAddress Target node address
     * @param networkKey Network key to send
     * @param authToken Auth token
     * @param networkId Network ID
     * @param keyVersion Key version
     * @return true if sent successfully
     */
    static bool sendNetkeyUpdate(uint16_t nodeAddress,
                               const uint8_t* networkKey,
                               const uint8_t* authToken,
                               uint16_t networkId,
                               uint8_t keyVersion);
    
    /**
     * @brief Send netkey response (Node only)
     * @param bridgeAddress Bridge address
     * @param keyVersion Key version being responded to
     * @param status Update status
     * @return true if sent successfully
     */
    static bool sendNetkeyResponse(uint16_t bridgeAddress,
                                 uint8_t keyVersion,
                                 NetkeyUpdateStatus status);
    
    /**
     * @brief Validate netkey update packet
     * @param packet Packet to validate
     * @param senderAddress Sender address
     * @return true if packet is valid
     */
    static bool validateNetkeyPacket(const NetkeyUpdatePacket* packet, uint16_t senderAddress);
    
    /**
     * @brief Generate MAC for netkey packet
     * @param data Data to generate MAC for
     * @param dataSize Size of data
     * @param mac Output MAC (4 bytes)
     * @param useCurrentKey Whether to use current or old key
     * @return true if MAC generated successfully
     */
    static bool generateNetkeyMAC(const uint8_t* data, size_t dataSize, uint8_t* mac, bool useCurrentKey = true);
    
    /**
     * @brief Generate MAC using Bootstrap Key for secure netkey distribution
     * @param data Data to generate MAC for
     * @param dataSize Size of data
     * @param mac Output MAC (4 bytes)
     * @return true if MAC generated successfully
     */
    static bool generateBootstrapMAC(const uint8_t* data, size_t dataSize, uint8_t* mac);
};

#endif // _NETKEY_DISTRIBUTION_SERVICE_H