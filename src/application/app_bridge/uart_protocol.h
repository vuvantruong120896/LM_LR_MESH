#ifndef _UART_PROTOCOL_H
#define _UART_PROTOCOL_H

#include <Arduino.h>
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/src/services/ProvisioningProtocol.h"

// UART packet types
enum UartPacketType : uint8_t {
    UART_PACKET_DATA = 0x01,        // Mesh data packet
    UART_PACKET_STATUS = 0x02,      // Bridge status
    UART_PACKET_HEARTBEAT = 0x03,   // Heartbeat/keepalive
    UART_PACKET_COMMAND = 0x04,     // Command from external ESP32
    UART_PACKET_ACK = 0x05,         // Acknowledgment
    UART_PACKET_ERROR = 0x06        // Error response
};

// UART command types
enum UartCommand : uint8_t {
    UART_CMD_GET_STATUS = 0x10,     // Request bridge status
    UART_CMD_GET_NODES = 0x11,      // Request connected nodes list
    UART_CMD_RESET = 0x12,          // Reset bridge
    UART_CMD_SET_CONFIG = 0x13,     // Update configuration
    UART_CMD_SET_NETKEY = 0x14,     // Set network key for mesh
    UART_CMD_START_PROVISIONING = 0x15,  // Start provisioning mode
    UART_CMD_STOP_PROVISIONING = 0x16,   // Stop provisioning mode
    UART_CMD_GET_PROVISIONING_STATUS = 0x17,  // Get provisioning status
    UART_CMD_GET_ROUTING_TABLE = 0x20    // Request full routing table
};

// UART packet structure
#pragma pack(1)
struct UartPacket {
    uint8_t startBytes[2];          // 0x4C, 0x4D
    uint8_t packetType;             // UartPacketType
    uint8_t payloadLength;          // Length of payload (0-200)
    uint8_t sequenceNumber;         // Sequence number for ordering
    uint8_t payload[200];           // Variable payload
    uint8_t checksum;               // Simple checksum
    uint8_t endByte;                // 0x55
};
#pragma pack()

// Bridge status structure for UART (packed for on-wire layout)
#pragma pack(1)
struct UartBridgeStatus {
    uint16_t bridgeId;
    uint32_t uptime;                // Seconds since boot
    uint16_t connectedNodes;        // Number of nodes in routing table
    uint32_t totalPacketsReceived;  // Total packets from mesh
    uint32_t totalPacketsSent;      // Total packets to external ESP32
    uint16_t freeHeap;              // Free heap memory in KB
    int8_t lastRSSI;               // Last received packet RSSI
    int8_t lastSNR;                // Last received packet SNR
    uint8_t meshHealth;            // 0-100% mesh network health
};

// Node information structure (packed for on-wire layout)
struct UartNodeInfo {
    uint16_t nodeId;
    uint8_t hopCount;              // Hops to reach this node
    int8_t rssi;                   // Last RSSI from this node
    uint32_t lastSeen;             // Timestamp of last packet
    uint8_t nodeType;              // Node role/type
};

// Network key structure for UART (packed for on-wire layout)
struct UartNetworkKey {
    uint8_t networkKey[16];        // 128-bit network key
    uint8_t authToken[8];          // Authentication token
    uint16_t networkId;            // Network identifier
    uint8_t keyVersion;            // Key version for rotation
    uint32_t timestamp;            // Key generation timestamp
};

// Provisioning control structure (packed for on-wire layout)
struct UartProvisioningControl {
    uint8_t action;                // 0=stop, 1=start, 2=get_status
    uint32_t durationMs;           // Duration in milliseconds (0=indefinite)
    uint8_t maxSessions;           // Max concurrent sessions
    uint8_t authMethod;            // Authentication method
};


// Provisioning status response (packed for on-wire layout)
struct UartProvisioningStatus {
    bool active;                   // Provisioning mode active
    uint32_t remainingTimeMs;      // Remaining time (0=indefinite)
    uint8_t activeSessions;        // Current active sessions
    uint8_t maxSessions;           // Maximum sessions allowed
    uint16_t totalRequests;        // Total provisioning requests
    uint16_t successfulProvisions; // Successful provisions
    uint16_t rejectedRequests;     // Rejected requests
};

// Routing table entry structure (packed for on-wire layout)
struct UartRoutingEntry {
    uint16_t address;              // Node address
    uint16_t via;                  // Next hop address (via)
    uint8_t metric;                // Hop count
    uint8_t role;                  // Node role
    int8_t receivedSNR;            // Received SNR (signal quality)
    uint32_t timeToLive;           // Time remaining before timeout (seconds)
};

// Routing table response header (packed for on-wire layout)
struct UartRoutingTableHeader {
    uint8_t totalEntries;          // Total number of entries
    uint8_t currentPacket;         // Current packet index (0-based)
    uint8_t totalPackets;          // Total packets needed
    uint8_t entriesInPacket;       // Number of entries in this packet
};

// Maximum routing entries per UART packet (considering 200 byte payload limit)
// UartRoutingTableHeader = 4 bytes
// UartRoutingEntry = 12 bytes each
// Max entries = (200 - 4) / 12 = 16 entries per packet
#define MAX_ROUTING_ENTRIES_PER_PACKET 16

#pragma pack()

// UART communication class
class UartProtocol {
private:
    HardwareSerial* uart;
    uint8_t sequenceNumber;
    uint8_t rxBuffer[300];
    size_t rxBufferIndex;
    
    // Helper functions
    uint8_t calculateChecksum(const UartPacket* packet);
    bool validatePacket(const UartPacket* packet);
    // Calculate checksum from fields without requiring a full UartPacket
    uint8_t calculateChecksumFields(uint8_t packetType, uint8_t payloadLength, uint8_t sequenceNumber, const uint8_t* payload);
    // Send a raw packet by streaming header/payload/checksum/end to UART (avoids 200-byte stack allocation)
    bool sendRawPacket(uint8_t packetType, const uint8_t* payload, uint8_t payloadLength);
    
public:
    UartProtocol(HardwareSerial* serialPort);
    
    // Initialize UART communication
    void begin(uint32_t baudRate);
    
    // Send functions
    bool sendDataPacket(const dataPacket& data, uint16_t sourceNode);
    bool sendStatusPacket(const UartBridgeStatus& status);
    bool sendHeartbeat();
    bool sendAck(uint8_t sequenceNum);
    bool sendError(uint8_t errorCode);
    bool sendNetkeyUpdateConfirm(bool success);
    
    // Receive functions
    bool receivePacket(UartPacket& packet);
    void processReceivedPacket(const UartPacket& packet);
    
    // Network key handling
    void setNetkeyCallback(void (*callback)(const UartNetworkKey& netkey));
    
    // Provisioning control handling
    void setProvisioningCallback(void (*callback)(const UartProvisioningControl& control));
    bool sendProvisioningStatus(const UartProvisioningStatus& status);
    
    // Routing table handling
    bool sendRoutingTable();  // Send entire routing table (may span multiple packets)
    
    // Utility functions
    void update();  // Call in main loop to handle incoming data
    void flush();   // Clear buffers
    bool isConnected(); // Check if external ESP32 is responding
    
private:
    void (*netkeyCallback)(const UartNetworkKey& netkey) = nullptr;
    void (*provisioningCallback)(const UartProvisioningControl& control) = nullptr;
};

#endif // _UART_PROTOCOL_H