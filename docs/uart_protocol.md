UART Protocol for Bridge <-> External ESP32

Overview

This document describes the UART protocol used between the Bridge (this firmware) and an external ESP32 controller. The protocol is lightweight, framed, and supports commands, data transfer, status reporting and provisioning control.

Packet framing

All UART messages use a fixed framing with a two-byte start sequence, and an end byte, plus a small header describing packet type and payload length. Packets are packed with #pragma pack(1) on the bridge side.

Structure (visual):

- startBytes (0x4C, 0x4D)
- packetType (1 byte)  — UartPacketType
- payloadLength (1 byte) — number of bytes in payload (0-200)
- sequenceNumber (1 byte) — monotonic sequence assigned by sender
- payload (up to 200 bytes)
- checksum (1 byte) — XOR of packetType, payloadLength, sequenceNumber and payload bytes
- endByte (0x55)

C struct on bridge side (wire layout):

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

Checksum

The checksum is a simple XOR of these fields:
- packetType
- payloadLength
- sequenceNumber
- each byte of payload (payloadLength bytes)

Validation also checks startBytes=={0x4C,0x4D} and endByte==0x55 and payloadLength<=200.

Packet types

- UART_PACKET_DATA (0x01): mesh data packet forwarded to external ESP32
- UART_PACKET_STATUS (0x02): bridge or provisioning status
- UART_PACKET_HEARTBEAT (0x03): periodic heartbeat with timestamp
- UART_PACKET_COMMAND (0x04): command from external ESP32 to bridge
- UART_PACKET_ACK (0x05): acknowledgement
- UART_PACKET_ERROR (0x06): error response

Commands (payload of UART_PACKET_COMMAND)

The first byte of the payload indicates the command code (UartCommand). Additional bytes in payload encode command-specific parameters.

- UART_CMD_GET_STATUS (0x10)
  - Request bridge status. Response: `UART_PACKET_STATUS` with `UartBridgeStatus` payload.

- UART_CMD_GET_NODES (0x11)
  - Request node list. Bridge may stream node entries as `UART_PACKET_DATA` or `UART_PACKET_STATUS` depending on implementation.

- UART_CMD_RESET (0x12)
  - Reset the bridge (calls ESP.restart()). No payload expected.

- UART_CMD_SET_CONFIG (0x13)
  - Update configuration. Payload depends on configuration schema (not defined here).

- UART_CMD_SET_NETKEY (0x14)
  - Set network key. Payload: first byte = command code (0x14), followed by `UartNetworkKey` structure.
  - `UartNetworkKey` (packed):
    - uint8_t networkKey[16]
    - uint8_t authToken[8]
    - uint16_t networkId
    - uint8_t keyVersion
    - uint32_t timestamp

  - Bridge calls the registered netkey callback if present, and responds with an ACK packet where payload [0] = 0x14 and payload[1] = 0x01 on success or 0x00 on failure.

- UART_CMD_START_PROVISIONING (0x15)
  - Start provisioning mode on the bridge. Payload: command code + `UartProvisioningControl`:
    - action (uint8_t) - ignored on sender side; bridge treats this as START (1)
    - durationMs (uint32_t) - 0 means indefinite
    - maxSessions (uint8_t)
    - authMethod (uint8_t)

  - Bridge will call provisioning callback with parsed `UartProvisioningControl` and send an ACK if handler present.

- UART_CMD_STOP_PROVISIONING (0x16)
  - Stop provisioning immediately. Payload: command code only or with `UartProvisioningControl` zeros.

- UART_CMD_GET_PROVISIONING_STATUS (0x17)
  - Bridges responds by invoking the provisioning callback which should eventually call `sendProvisioningStatus()` with `UartProvisioningStatus` payload.

Data structures

- UartBridgeStatus (used with UART_PACKET_STATUS):
  - uint16_t bridgeId
  - uint32_t uptime
  - uint16_t connectedNodes
  - uint32_t totalPacketsReceived
  - uint32_t totalPacketsSent
  - uint16_t freeHeap
  - int8_t lastRSSI
  - int8_t lastSNR
  - uint8_t meshHealth

- UartNodeInfo (used to list nodes):
  - uint16_t nodeId
  - uint8_t hopCount
  - int8_t rssi
  - uint32_t lastSeen
  - uint8_t nodeType

- UartProvisioningControl:
  - uint8_t action  // 0 stop, 1 start, 2 get_status
  - uint32_t durationMs
  - uint8_t maxSessions
  - uint8_t authMethod

- UartProvisioningStatus:
  - bool active
  - uint32_t remainingTimeMs
  - uint8_t activeSessions
  - uint8_t maxSessions
  - uint16_t totalRequests
  - uint16_t successfulProvisions
  - uint16_t rejectedRequests

Usage examples

Binary packet (example) - Start Provisioning for 10 seconds, max 2 sessions, CHALLENGE auth (authMethod=1)

Payload bytes (command + struct):
- 0x15 (UART_CMD_START_PROVISIONING)
- action: 0x01 (start) [bridge forces start]
- durationMs: 0x00 0x00 0x27 0x10 (10000ms little-endian)
- maxSessions: 0x02
- authMethod: 0x01

Full framed packet (hex):
- 0x4C 0x4D (start)
- 0x04 (packetType = COMMAND)
- 0x07 (payloadLength = 7 bytes)
- 0x01 (sequenceNumber)
- payload bytes above (7 bytes)
- checksum = XOR of packetType, payloadLength, sequenceNumber and payload
- 0x55 (end)

Processing logic

Bridge side (receiver):
- Continuously call `UartProtocol::update()` in main loop
- When a `UART_PACKET_COMMAND` arrives, `processReceivedPacket()` parses the first payload byte as a `UartCommand` and dispatches
- For provisioning commands: the bridge constructs a `UartProvisioningControl` and invokes the registered `provisioningCallback`
- For netkey updates: if `netkeyCallback` is registered, it will be invoked with the parsed `UartNetworkKey`

External ESP32 (sender) pseudocode

- Open Serial1 at agreed baud
- Build the framed packet as above (start, type, length, seq, payload, checksum, end)
- Write bytes to serial
- Optionally wait for ACK (UART_PACKET_ACK) with payload containing the original seq

Example: Start provisioning (pseudocode)

// Build payload: command + UartProvisioningControl
uint8_t payload[] = { 0x15,             // UART_CMD_START_PROVISIONING
                      0x01,             // action (start)
                      0x10, 0x27, 0x00, 0x00, // durationMs = 10000 (little-endian)
                      0x02,             // maxSessions
                      0x01 };           // authMethod

// Header fields
uint8_t start1 = 0x4C;
uint8_t start2 = 0x4D;
uint8_t packetType = 0x04; // COMMAND
uint8_t payloadLength = sizeof(payload); // 8
uint8_t seq = nextSeq();

// Compute checksum: XOR of packetType, payloadLength, seq and each payload byte
uint8_t checksum = packetType ^ payloadLength ^ seq;
for (int i = 0; i < payloadLength; ++i) checksum ^= payload[i];

// Frame layout to send: start1, start2, packetType, payloadLength, seq, payload..., checksum, end(0x55)
serial.write(start1);
serial.write(start2);
serial.write(packetType);
serial.write(payloadLength);
serial.write(seq);
serial.write(payload, payloadLength);
serial.write(checksum);
serial.write(0x55);

// Example full hex frame (example seq = 0x01):
// 4C 4D 04 08 01 15 01 10 27 00 00 02 01 <checksum> 55
// Compute checksum step-by-step for clarity:
// 0x04 ^ 0x08 = 0x0C
// 0x0C ^ 0x01 = 0x0D
// 0x0D ^ 0x15 = 0x18
// 0x18 ^ 0x01 = 0x19
// 0x19 ^ 0x10 = 0x09
// 0x09 ^ 0x27 = 0x2E
// 0x2E ^ 0x00 = 0x2E
// 0x2E ^ 0x00 = 0x2E
// 0x2E ^ 0x02 = 0x2C
// 0x2C ^ 0x01 = 0x2D  --> checksum = 0x2D
// Final framed bytes:
// 4C 4D 04 08 01 15 01 10 27 00 00 02 01 2D 55

Additional concrete hex examples

1) Stop provisioning (immediate)

- Payload bytes: [0x16] (command only)
- Framed packet (example, sequenceNumber = 0x02):
  - 0x4C 0x4D -- start
  - 0x04  -- packetType = COMMAND
  - 0x01  -- payloadLength = 1
  - 0x02  -- sequenceNumber
  - 0x16  -- payload (UART_CMD_STOP_PROVISIONING)
  - checksum = XOR(0x04,0x01,0x02,0x16)
  - 0x55  -- end

  Example full hex (compute checksum manually):
  - Let's compute checksum step-by-step:
    - 0x04 ^ 0x01 = 0x05
    - 0x05 ^ 0x02 = 0x07
    - 0x07 ^ 0x16 = 0x11
  - Final framed bytes: 4C 4D 04 01 02 16 11 55

2) Get provisioning status (request)

- Payload bytes: [0x17] (command only)
- Framed packet (example, sequenceNumber = 0x03):
  - 0x4C 0x4D
  - 0x04
  - 0x01
  - 0x03
  - 0x17
  - checksum = XOR(0x04,0x01,0x03,0x17)
  - 0x55

  Compute checksum:
    - 0x04 ^ 0x01 = 0x05
    - 0x05 ^ 0x03 = 0x06
    - 0x06 ^ 0x17 = 0x11

  Final hex: 4C 4D 04 01 03 17 11 55

3) Set network key (example)

This example assumes a small made-up key and auth token for demonstration. `UartNetworkKey` layout (packed):
  - networkKey[16] (16 bytes)
  - authToken[8] (8 bytes)
  - networkId (2 bytes, little-endian)
  - keyVersion (1 byte)
  - timestamp (4 bytes, little-endian)

Example values (in hex):
  - networkKey (16 bytes): 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
  - authToken (8 bytes):   AA BB CC DD EE FF 00 11
  - networkId:             0x34 0x12  (0x1234 little-endian)
  - keyVersion:            0x01
  - timestamp:             0x5E 0x2A 0x00 0x00  (10846 decimal as example)

Payload composition: [0x14] + 16 + 8 + 2 + 1 + 4 = 31 bytes payload

Example payload hex (31 bytes):
  14 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF AA BB CC DD EE FF 00 11 34 12 01 5E 2A 00 00

Wrap into frame (example sequenceNumber = 0x04):
  - 0x4C 0x4D
  - 0x04 (COMMAND)
  - 0x20 (payloadLength = 32 decimal = 0x20)  <-- command byte + 31-byte `UartNetworkKey` = 32
  - 0x04 (sequenceNumber)
  - payload bytes (32 bytes as listed above)

  - checksum = 0x67  // computed XOR over packetType, payloadLength, sequenceNumber and all payload bytes (verified programmatically)
  - 0x55

Final full frame (hex):

4C 4D 04 20 04 14 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF AA BB CC DD EE FF 00 11 34 12 01 5E 2A 00 00 67 55

Explanation:
  - packetType = 0x04 (COMMAND)
  - payloadLength = 0x20 (32)
  - sequenceNumber = 0x04 (example)
  - payload starts with 0x14 (UART_CMD_SET_NETKEY) followed by the packed `UartNetworkKey`
  - checksum = 0x58 calculated by XOR'ing 0x04 ^ 0x20 ^ 0x04 ^ (each payload byte)

Bridge response: On success the bridge will send an ACK packet with payload [0x14,0x01] or an error packet on failure.

Notes & extensions

- The checksum and framing are intentionally simple for small microcontroller usage. If the environment includes noisy serial links, consider adding CRC16 or escaping/byte-stuffing.
- Consider adding a short command-specific response timeout logic on the external controller to detect missing ACKs.
- For large node lists, implement chunked responses with sequence numbers and possibly a LIST_END marker.

Document history

- 2025-09-27: Initial documentation created from `src/application/app_bridge/uart_protocol.*` implementation
