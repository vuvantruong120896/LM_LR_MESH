#ifndef _UART_PROTOCOL_H
#define _UART_PROTOCOL_H

#include <Arduino.h>
#include "../common/mesh_utils.h"

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
    UART_CMD_SET_CONFIG = 0x13      // Update configuration
};

// UART packet structure
#pragma pack(1)
struct UartPacket {
    uint8_t startByte;              // 0xAA
    uint8_t packetType;             // UartPacketType
    uint8_t payloadLength;          // Length of payload (0-200)
    uint8_t sequenceNumber;         // Sequence number for ordering
    uint8_t payload[200];           // Variable payload
    uint8_t checksum;               // Simple checksum
    uint8_t endByte;                // 0x55
};
#pragma pack()

// Bridge status structure for UART
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

// Node information structure
struct UartNodeInfo {
    uint16_t nodeId;
    uint8_t hopCount;              // Hops to reach this node
    int8_t rssi;                   // Last RSSI from this node
    uint32_t lastSeen;             // Timestamp of last packet
    uint8_t nodeType;              // Node role/type
};

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
    
    // Receive functions
    bool receivePacket(UartPacket& packet);
    void processReceivedPacket(const UartPacket& packet);
    
    // Utility functions
    void update();  // Call in main loop to handle incoming data
    void flush();   // Clear buffers
    bool isConnected(); // Check if external ESP32 is responding
};

#endif // _UART_PROTOCOL_H