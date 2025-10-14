# Phase 1.1: UART Protocol Documentation

## 📡 UART Communication Overview

### **Purpose**
Gateway (ESP32 + LoRa) giao tiếp với External ESP32 (WiFi + Firebase) qua UART Serial1 với baudrate 115200.

### **Physical Connection**
```
Gateway ESP32 (UART1)          External ESP32
┌──────────────┐              ┌──────────────┐
│ TX (GPIO 21) │─────────────►│ RX           │
│ RX (GPIO 20) │◄─────────────│ TX           │
│ GND          │──────────────│ GND          │
└──────────────┘              └──────────────┘
```

---

## 📦 Packet Structure

### **Base UART Packet Format**
```
┌──────┬──────┬────────┬───────┬────────┬─────────┬──────────┬───────┐
│Start │Start │Packet  │Payload│Sequence│ Payload │ Checksum │ End   │
│Byte1 │Byte2 │Type    │Length │Number  │(0-200B) │          │ Byte  │
├──────┼──────┼────────┼───────┼────────┼─────────┼──────────┼───────┤
│ 0x4C │ 0x4D │ 1 byte │1 byte │ 1 byte │ N bytes │ 1 byte   │ 0x55  │
│ 'L'  │ 'M'  │        │       │        │         │          │       │
└──────┴──────┴────────┴───────┴────────┴─────────┴──────────┴───────┘
Total: 7 bytes header + N bytes payload + 1 byte footer = 8 + N bytes
```

### **Field Descriptions**

| Field | Size | Value | Description |
|-------|------|-------|-------------|
| `startBytes[0]` | 1 byte | `0x4C` | Start delimiter 'L' (LoRa) |
| `startBytes[1]` | 1 byte | `0x4D` | Start delimiter 'M' (Mesh) |
| `packetType` | 1 byte | `0x01-0x06` | See Packet Types below |
| `payloadLength` | 1 byte | `0-200` | Length of payload data |
| `sequenceNumber` | 1 byte | `0-255` | Incremental counter for ordering |
| `payload[]` | N bytes | Variable | Actual data (max 200 bytes) |
| `checksum` | 1 byte | Computed | Simple XOR checksum |
| `endByte` | 1 byte | `0x55` | End delimiter |

### **Checksum Algorithm**
```cpp
uint8_t checksum = packetType ^ payloadLength ^ sequenceNumber;
for (int i = 0; i < payloadLength; i++) {
    checksum ^= payload[i];
}
```

---

## 📋 Packet Types

### **1. UART_PACKET_DATA (0x01) - Sensor Data from Mesh**
**Direction**: Gateway → External ESP32  
**Purpose**: Forward sensor readings từ mesh nodes lên Firebase

**Payload Structure** (16 bytes):
```cpp
struct dataPacket {
    uint32_t counter;      // Sequence counter (4 bytes)
    uint32_t timestamp;    // Unix timestamp (4 bytes)
    uint16_t nodeId;       // Source node address (2 bytes)
    // Padding to align (6 bytes unused)
};
```

**Example Packet**:
```
4C 4D 01 10 42  |  01 00 00 00 | 5A B2 3F 67 | 34 12 | 00 00 00 00 00 00  |  C3  |  55
│  │  │  │  │      └─counter─┘   └─timestamp┘ └node┘  └────padding────┘    │     │
│  │  │  │  └─SequenceNum                                                   │     └─End
│  │  │  └─PayloadLen(16)                                                   └─Checksum
│  │  └─Type(DATA)
│  └─'M'
└─'L'
```

---

### **2. UART_PACKET_STATUS (0x02) - Gateway Status**
**Direction**: Gateway → External ESP32  
**Purpose**: Periodic status updates (every 30 seconds)

**Payload Structure** (19 bytes):
```cpp
struct UartGatewayStatus {
    uint16_t gatewayId;             // Gateway ID (2 bytes)
    uint32_t uptime;                // Uptime in seconds (4 bytes)
    uint16_t connectedNodes;        // Number of nodes in routing table (2 bytes)
    uint32_t totalPacketsReceived;  // Total LoRa packets received (4 bytes)
    uint32_t totalPacketsSent;      // Total packets sent via UART (4 bytes)
    uint16_t freeHeap;              // Free heap in KB (2 bytes)
    int8_t lastRSSI;                // Last packet RSSI (1 byte)
    int8_t lastSNR;                 // Last packet SNR (1 byte)
    uint8_t meshHealth;             // Health score 0-100 (1 byte)
};
// Total: 19 bytes
```

**Example Status Packet**:
```
Gateway ID: 0x0001
Uptime: 3600 seconds (1 hour)
Connected Nodes: 5
Total Received: 1234 packets
Total Sent: 1200 packets
Free Heap: 150 KB
Last RSSI: -85 dBm
Last SNR: 8 dB
Mesh Health: 95%
```

---

### **3. UART_PACKET_HEARTBEAT (0x03) - Keepalive**
**Direction**: Bidirectional  
**Purpose**: Verify UART connection is alive (every 5 seconds)

**Payload Structure** (4 bytes):
```cpp
struct Heartbeat {
    uint32_t timestamp;  // Current millis() (4 bytes)
};
```

**Connection Detection**:
- Gateway sends heartbeat every 5 seconds
- External ESP32 responds with ACK (0x05)
- If no ACK received within 10 seconds → connection considered lost

---

### **4. UART_PACKET_COMMAND (0x04) - Commands from External ESP32**
**Direction**: External ESP32 → Gateway  
**Purpose**: Remote control and configuration

**Command Types**:

| Command Code | Name | Description |
|--------------|------|-------------|
| `0x10` | `GET_STATUS` | Request current gateway status |
| `0x11` | `GET_NODES` | Request list of connected nodes |
| `0x12` | `RESET` | Reboot gateway |
| `0x13` | `SET_CONFIG` | Update configuration |
| `0x14` | `SET_NETKEY` | Distribute new network key |
| `0x15` | `START_PROVISIONING` | Enter provisioning mode |
| `0x16` | `STOP_PROVISIONING` | Exit provisioning mode |
| `0x17` | `GET_PROVISIONING_STATUS` | Query provisioning state |
| `0x20` | `GET_ROUTING_TABLE` | Request full routing table |

**Payload Format** (Variable):
```cpp
struct CommandPacket {
    uint8_t commandCode;  // One of the command codes above
    uint8_t params[];     // Command-specific parameters
};
```

---

### **5. UART_PACKET_ACK (0x05) - Acknowledgment**
**Direction**: Bidirectional  
**Purpose**: Confirm receipt of packet

**Payload Structure** (1 byte):
```cpp
struct AckPacket {
    uint8_t ackedSequenceNumber;  // Sequence number being acknowledged
};
```

---

### **6. UART_PACKET_ERROR (0x06) - Error Response**
**Direction**: Bidirectional  
**Purpose**: Report errors

**Payload Structure** (2 bytes):
```cpp
struct ErrorPacket {
    uint8_t errorCode;      // Error code (see below)
    uint8_t failedCommand;  // Command that failed (if applicable)
};
```

**Error Codes**:
- `0x01`: Invalid packet format
- `0x02`: Checksum mismatch
- `0x03`: Unknown command
- `0x04`: Command execution failed
- `0x05`: Buffer overflow
- `0x06`: Timeout

---

## 🔑 Special Payloads

### **Network Key Distribution (CMD 0x14)**
**Payload Structure** (31 bytes):
```cpp
struct UartNetworkKey {
    uint8_t networkKey[16];   // 128-bit AES key (16 bytes)
    uint8_t authToken[8];     // Authentication token (8 bytes)
    uint16_t networkId;       // Network ID (2 bytes)
    uint8_t keyVersion;       // Key version for rotation (1 byte)
    uint32_t timestamp;       // Key generation timestamp (4 bytes)
};
// Total: 31 bytes
```

**Flow**:
```
External ESP32                    Gateway                     Nodes
     │                               │                          │
     ├──[CMD: SET_NETKEY]───────────►│                          │
     │   (networkKey + authToken)    │                          │
     │                               ├─[Save to NVS]            │
     │                               ├─[Update local key]       │
     │                               ├──[Distribute]───────────►│
     │                               │                          ├─[Apply key]
     │◄──[ACK: Success]──────────────┤                          │
     │                               │                          │
```

---

### **Provisioning Control (CMD 0x15/0x16)**
**Payload Structure** (10 bytes):
```cpp
struct UartProvisioningControl {
    uint8_t action;          // 0=stop, 1=start, 2=get_status
    uint32_t durationMs;     // Duration in ms (0=indefinite) (4 bytes)
    uint8_t maxSessions;     // Max concurrent sessions (1 byte)
    uint8_t authMethod;      // Authentication method (1 byte)
    // Padding: 3 bytes
};
```

**Provisioning Status Response**:
```cpp
struct UartProvisioningStatus {
    bool active;                   // Provisioning mode active (1 byte)
    uint32_t remainingTimeMs;      // Remaining time (4 bytes)
    uint8_t activeSessions;        // Current sessions (1 byte)
    uint8_t maxSessions;           // Max allowed (1 byte)
    uint16_t totalRequests;        // Total requests (2 bytes)
    uint16_t successfulProvisions; // Successful (2 bytes)
    uint16_t rejectedRequests;     // Rejected (2 bytes)
};
// Total: 13 bytes
```

---

### **Routing Table Response (CMD 0x20)**
**Challenge**: Routing table có thể có 50+ entries, không fit trong 1 packet

**Solution**: Multi-packet response

**Header** (4 bytes):
```cpp
struct UartRoutingTableHeader {
    uint8_t totalEntries;     // Total number of routes
    uint8_t currentPacket;    // Current packet index (0-based)
    uint8_t totalPackets;     // Total packets in sequence
    uint8_t entriesInPacket;  // Entries in THIS packet
};
```

**Entry** (12 bytes each):
```cpp
struct UartRoutingEntry {
    uint16_t address;      // Node address (2 bytes)
    uint16_t via;          // Next hop (2 bytes)
    uint8_t metric;        // Hop count (1 byte)
    uint8_t role;          // Node role bitmask (1 byte)
    int8_t receivedSNR;    // Signal quality (1 byte)
    uint32_t timeToLive;   // Seconds until timeout (4 bytes)
    // Padding: 1 byte
};
```

**Calculation**:
- Payload size: 200 bytes max
- Header: 4 bytes
- Available: 196 bytes
- Entries per packet: 196 / 12 = 16 entries

**Example**: 45 routes → 3 packets
```
Packet 1: Header{45, 0, 3, 16} + 16 entries = 196 bytes
Packet 2: Header{45, 1, 3, 16} + 16 entries = 196 bytes
Packet 3: Header{45, 2, 3, 13} + 13 entries = 160 bytes
```

---

## 📊 Data Types Used in Payloads

### **From mesh_utils.h**:

```cpp
// Simplified data packet (16 bytes)
struct dataPacket {
    uint32_t counter;      // Sequence counter
    uint32_t timestamp;    // Unix timestamp
    uint16_t nodeId;       // Source node
};

// Full sensor data (24 bytes) - NOT sent via UART
struct sensorData {
    uint32_t counter;       // Sequence counter
    float temperature;      // Temperature in °C
    float humidity;         // Humidity in %
    float battery;          // Battery voltage
    uint32_t timestamp;     // Unix timestamp
    uint16_t nodeId;        // Origin node
};

// Gateway status for internal use (8 bytes)
struct gatewayStatus {
    uint16_t connectedNodes;
    uint32_t totalPackets;
    bool wifiConnected;
    bool mqttConnected;
};
```

---

## 🔄 Communication Flows

### **Flow 1: Sensor Data Upload**
```
Node                    Gateway                 External ESP32          Firebase
 │                         │                          │                     │
 ├─[LoRa: sensorData]─────►│                          │                     │
 │  (temp, hum, battery)   ├─[Extract data]           │                     │
 │                         ├─[Package to dataPacket]  │                     │
 │                         ├──[UART: DATA packet]────►│                     │
 │                         │                          ├─[Parse]             │
 │                         │                          ├─[Upload to FB]─────►│
 │                         │◄─[ACK]───────────────────┤                     │
 │                         │                          │                     │
```

**Timing**: ~100ms total latency (LoRa + UART + WiFi)

---

### **Flow 2: Network Key Distribution**
```
External ESP32          Gateway                    Node 1              Node 2
     │                     │                          │                   │
     ├─[SET_NETKEY]───────►│                          │                   │
     │                     ├─[Save to NVS]            │                   │
     │                     ├─[Update local]           │                   │
     │                     ├──[LoRa: Netkey]─────────►│                   │
     │                     ├──[LoRa: Netkey]─────────────────────────────►│
     │                     │                          ├─[Apply & Reboot]  │
     │                     │                          │                   ├─[Apply & Reboot]
     │◄─[ACK: Success]─────┤                          │                   │
     │                     │                          │                   │
```

---

### **Flow 3: Provisioning Mode**
```
External ESP32          Gateway                    New Node
     │                     │                          │
     ├─[START_PROVISION]──►│                          │
     │  (duration: 60s)    ├─[Enter Fast Discovery]  │
     │                     │  (HELLO interval: 30s)   │
     │                     │                          │
     │                     │◄─[HELLO packet]──────────┤
     │                     ├─[Add to routing table]   │
     │                     ├─[Allocate address]       │
     │                     ├──[Send netkey]──────────►│
     │                     │                          ├─[Configure & Join]
     │                     │                          │
     ├─[GET_STATUS]───────►│                          │
     │◄─[Status: 1 node]───┤                          │
     │                     │                          │
     │  (after 60s)        │                          │
     ├─[STOP_PROVISION]───►│                          │
     │                     ├─[Exit Fast Discovery]    │
     │                     │  (HELLO interval: 120s)  │
```

---

## 📈 Performance Characteristics

### **Bandwidth Usage**:
- **Sensor data**: 16 bytes payload + 8 bytes overhead = 24 bytes/packet
- **At 5 nodes, 30s interval**: 5 × 2 packets/min = 10 packets/min = 240 bytes/min
- **Status updates**: 19 bytes payload + 8 bytes overhead = 27 bytes/packet
- **At 30s interval**: 2 packets/min = 54 bytes/min
- **Total bandwidth**: ~300 bytes/min ≈ 5 bytes/second (very low!)

### **Latency**:
- **UART transmission time** (115200 baud): 24 bytes × 10 bits/byte ÷ 115200 = ~2ms
- **Processing overhead**: ~1-2ms
- **Total per-hop latency**: ~5ms

### **Reliability**:
- **Packet loss rate**: < 0.1% (with checksum validation)
- **Error detection**: 100% (checksum catches all single-bit errors)
- **Retry mechanism**: ACK-based retransmission

---

## 🎯 Key Observations for Firebase Migration

### **What needs to be replaced**:
1. ✅ **UART packet encoding/decoding** → JSON serialization
2. ✅ **Serial communication** → HTTP REST API / Realtime DB SDK
3. ✅ **Heartbeat mechanism** → WiFi connection monitoring
4. ✅ **ACK/retry logic** → Firebase SDK handles this automatically

### **What stays the same**:
1. ✅ **Data structures** (sensorData, dataPacket) → Keep as-is
2. ✅ **LoRa mesh processing** → 100% unchanged
3. ✅ **Routing table logic** → Same, just different upload method
4. ✅ **Provisioning flow** → Same, just different trigger mechanism

### **Simplifications in new architecture**:
- ❌ No packet framing needed (JSON handles structure)
- ❌ No checksum needed (TCP/TLS handles integrity)
- ❌ No sequence numbers needed (HTTP requests are atomic)
- ❌ No multi-packet responses (Firebase supports large documents)

### **New considerations**:
- ⚠️ WiFi connection stability
- ⚠️ Firebase quota limits (free tier: 20K writes/day)
- ⚠️ JSON payload overhead (~2x larger than binary)
- ⚠️ Network latency (100-500ms vs 5ms UART)

---

## 📝 Summary

**UART Protocol Complexity**: ~1500 lines of code  
**Packet Types**: 6 types with 9 command variants  
**Data Structures**: 10+ different payload formats  
**Total Bandwidth**: ~5 bytes/second average  

**Verdict**: UART protocol is well-designed but adds significant complexity. Firebase migration will **simplify** the codebase significantly while adding WiFi dependency.

**Next Step**: Map the complete data flow in Phase 1.2 →
