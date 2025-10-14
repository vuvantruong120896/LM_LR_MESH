# Phase 1.2: Data Flow Mapping

## 🔄 Complete System Data Flow

### **Current Architecture (2 ESP32s)**

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          SENSOR NODE (ESP32 + LoRa)                             │
├─────────────────────────────────────────────────────────────────────────────────┤
│  1. Read sensors (temp, humidity, battery)                                      │
│  2. Package into sensorData struct (24 bytes)                                   │
│  3. Encrypt with network key                                                    │
│  4. Send via LoRa mesh (max 255 bytes)                                          │
└──────────────────────────────┬──────────────────────────────────────────────────┘
                               │ LoRa 868/915 MHz
                               ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                        GATEWAY (ESP32 + LoRa Module)                            │
├─────────────────────────────────────────────────────────────────────────────────┤
│  [LoRa Reception Layer]                                                         │
│    ├─ RadioLib driver receives packet                                           │
│    ├─ Interrupt handler (GPIO 15)                                               │
│    └─ Store in receive queue                                                    │
│                                                                                 │
│  [Mesh Processing Layer] - KEEP 100%                                            │
│    ├─ LoraMesher::processReceivedPacket()                                       │
│    ├─ Decrypt packet using MeshSecurityService                                  │
│    ├─ Validate MAC & replay protection                                          │
│    ├─ Update routing table (RoutingTableService)                                │
│    ├─ Check if destined for gateway (dst == GATEWAY_ADDRESS)                    │
│    └─ Push to application queue                                                 │
│                                                                                 │
│  [Gateway Application Layer] - MODIFY THIS PART                                 │
│    ├─ GatewayApp::processGatewayPackets() task                                  │
│    ├─ Pop packet from queue                                                     │
│    ├─ Cast to AppPacket<sensorData>                                             │
│    └─ Call forwardToUART(packet)  ◄─── **CUT POINT** ❌                         │
│                                                                                 │
│  [UART Communication Layer] - REMOVE THIS SECTION ❌                            │
│    ├─ UartProtocol::sendDataPacket()                                            │
│    ├─ Package into UartPacket structure                                         │
│    │   ├─ Start bytes: 0x4C 0x4D                                                │
│    │   ├─ Packet type: 0x01 (DATA)                                              │
│    │   ├─ Payload: dataPacket (16 bytes)                                        │
│    │   ├─ Checksum: XOR of all bytes                                            │
│    │   └─ End byte: 0x55                                                        │
│    └─ Serial1.write() to UART TX pin                                            │
└──────────────────────────────┬──────────────────────────────────────────────────┘
                               │ UART 115200 baud
                               │ GPIO 21 (TX) → GPIO RX
                               ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                    EXTERNAL ESP32 (WiFi + Firebase Client)                      │
├─────────────────────────────────────────────────────────────────────────────────┤
│  [UART Reception Layer]                                                         │
│    ├─ Serial.available() polling                                                │
│    ├─ Find start bytes (0x4C 0x4D)                                              │
│    ├─ Read packet structure                                                     │
│    ├─ Validate checksum                                                         │
│    └─ Parse payload based on packet type                                        │
│                                                                                 │
│  [Data Processing Layer]                                                        │
│    ├─ Extract dataPacket from payload                                           │
│    ├─ Convert to JSON:                                                          │
│    │   {                                                                        │
│    │     "nodeId": "0x1234",                                                    │
│    │     "counter": 1523,                                                       │
│    │     "timestamp": 1729000000,                                               │
│    │     "temperature": 25.5,                                                   │
│    │     "humidity": 60.2,                                                      │
│    │     "battery": 3.7                                                         │
│    │   }                                                                        │
│    └─ Add metadata (RSSI, SNR from gateway status)                              │
│                                                                                 │
│  [WiFi Layer]                                                                   │
│    ├─ Maintain WiFi connection                                                  │
│    ├─ Auto-reconnect on disconnect                                              │
│    └─ Monitor signal strength                                                   │
│                                                                                 │
│  [Firebase Upload Layer]                                                        │
│    ├─ FirebaseESP32 library                                                     │
│    ├─ Authenticate with API key                                                 │
│    ├─ HTTPS POST to Firebase REST API                                           │
│    │   POST /nodes/node_0x1234/sensorData.json                                  │
│    └─ Handle upload errors & retry                                              │
└──────────────────────────────┬──────────────────────────────────────────────────┘
                               │ HTTPS (WiFi)
                               ▼
                        ┌──────────────────┐
                        │  Firebase Cloud  │
                        │  - Realtime DB   │
                        │  - Authentication│
                        └──────────────────┘
```

---

## 🎯 Target Architecture (1 ESP32)

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          SENSOR NODE (ESP32 + LoRa)                             │
│  - UNCHANGED -                                                                  │
└──────────────────────────────┬──────────────────────────────────────────────────┘
                               │ LoRa 868/915 MHz
                               ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│              UNIFIED GATEWAY (ESP32 + LoRa + WiFi Integrated)                   │
├─────────────────────────────────────────────────────────────────────────────────┤
│  [LoRa Reception Layer] - UNCHANGED ✅                                          │
│    ├─ RadioLib driver receives packet                                           │
│    ├─ Interrupt handler (GPIO 15)                                               │
│    └─ Store in receive queue                                                    │
│                                                                                 │
│  [Mesh Processing Layer] - UNCHANGED ✅                                         │
│    ├─ LoraMesher::processReceivedPacket()                                       │
│    ├─ Decrypt packet using MeshSecurityService                                  │
│    ├─ Validate MAC & replay protection                                          │
│    ├─ Update routing table (RoutingTableService)                                │
│    ├─ Check if destined for gateway                                             │
│    └─ Push to application queue                                                 │
│                                                                                 │
│  [Gateway Application Layer] - MODIFIED ⚠️                                      │
│    ├─ GatewayApp::processGatewayPackets() task                                  │
│    ├─ Pop packet from queue                                                     │
│    ├─ Cast to AppPacket<sensorData>                                             │
│    └─ Call uploadToFirebase(packet)  ◄─── **NEW METHOD** ✨                    │
│                                                                                 │
│  [WiFi Management Layer] - NEW ✨                                               │
│    ├─ WiFiManager::connect()                                                    │
│    │   ├─ WiFi.begin(SSID, PASSWORD)                                            │
│    │   ├─ Wait for connection (timeout 10s)                                     │
│    │   └─ Get IP address                                                        │
│    ├─ WiFiManager::isConnected()                                                │
│    │   └─ Check WiFi.status() == WL_CONNECTED                                   │
│    └─ WiFiManager::reconnect()                                                  │
│        ├─ Detect disconnect                                                     │
│        ├─ Exponential backoff retry                                             │
│        └─ Log connection events                                                 │
│                                                                                 │
│  [Firebase Client Layer] - NEW ✨                                               │
│    ├─ FirebaseClient::initialize()                                              │
│    │   ├─ Set Firebase host & auth                                              │
│    │   ├─ Configure SSL certificates                                            │
│    │   └─ Test connection                                                       │
│    │                                                                            │
│    ├─ FirebaseClient::uploadSensorData()                                        │
│    │   ├─ Convert sensorData to JSON:                                           │
│    │   │   FirebaseJson json;                                                   │
│    │   │   json.add("temperature", data.temperature);                           │
│    │   │   json.add("humidity", data.humidity);                                 │
│    │   │   json.add("battery", data.battery);                                   │
│    │   │   json.add("timestamp", data.timestamp);                               │
│    │   │   json.add("counter", data.counter);                                   │
│    │   ├─ Build path: /nodes/node_0x1234/sensorData                             │
│    │   ├─ Firebase.setJSON(fbdo, path, json)                                    │
│    │   └─ Return success/failure                                                │
│    │                                                                            │
│    ├─ FirebaseClient::uploadGatewayStatus()                                     │
│    │   ├─ Build JSON with gateway metrics                                       │
│    │   └─ Upload to /gateways/gateway_0x0001/status                             │
│    │                                                                            │
│    └─ FirebaseClient::uploadRoutingTable()                                      │
│        ├─ Iterate through RoutingTableService entries                           │
│        ├─ Build JSON array of routes                                            │
│        └─ Upload to /gateways/gateway_0x0001/routingTable                       │
│                                                                                 │
│  [Periodic Tasks] - MODIFIED ⚠️                                                 │
│    ├─ Every 100ms: Process LoRa packets ✅                                      │
│    ├─ Every 30s: Upload gateway status ✨ (was: send via UART)                  │
│    ├─ Every 60s: Upload routing table ✨ (was: send via UART)                   │
│    └─ Every 5s: Check WiFi connection ✨ (was: UART heartbeat)                  │
└──────────────────────────────┬──────────────────────────────────────────────────┘
                               │ HTTPS (WiFi)
                               ▼
                        ┌──────────────────┐
                        │  Firebase Cloud  │
                        │  - Realtime DB   │
                        │  - Authentication│
                        └──────────────────┘
```

---

## ⚡ Critical Data Paths

### **Path 1: Sensor Data Upload (Most Frequent)**

| Step | Location | Function | Duration | Status |
|------|----------|----------|----------|--------|
| 1 | Node | Read sensors & send LoRa packet | ~50ms | ✅ Keep |
| 2 | Gateway | LoRa receive interrupt | ~1ms | ✅ Keep |
| 3 | Gateway | Decrypt & validate packet | ~5ms | ✅ Keep |
| 4 | Gateway | Update routing table | ~2ms | ✅ Keep |
| 5 | Gateway | Extract sensorData | ~1ms | ✅ Keep |
| 6 | Gateway | ~~forwardToUART()~~ | ~~2ms~~ | ❌ Remove |
| 6 | Gateway | **uploadToFirebase()** | **100-300ms** | ✨ New |
| 7 | ~~External ESP32~~ | ~~UART parse & process~~ | ~~5ms~~ | ❌ Remove |
| 8 | ~~External ESP32~~ | ~~WiFi upload to Firebase~~ | ~~100-200ms~~ | ❌ Remove |

**Old Total Latency**: 50 + 1 + 5 + 2 + 1 + 2 + 5 + 150 = **216ms**  
**New Total Latency**: 50 + 1 + 5 + 2 + 1 + 200 = **259ms** (slight increase acceptable)

---

### **Path 2: Network Key Distribution**

**OLD FLOW**:
```
External ESP32         Gateway (UART)        Gateway (LoRa)          Node
     │                       │                      │                  │
     ├─[UART CMD 0x14]──────►│                      │                  │
     │  UartNetworkKey       ├─[Parse]              │                  │
     │                       ├─[Save to NVS]        │                  │
     │                       ├─[Update local key]   │                  │
     │                       │                      ├─[Broadcast]─────►│
     │                       │                      │                  ├─[Apply key]
     │◄─[UART ACK]───────────┤                      │                  │
```

**NEW FLOW**:
```
Firebase Console       Gateway (WiFi)        Gateway (LoRa)          Node
     │                       │                      │                  │
     ├─[HTTP POST]──────────►│                      │                  │
     │  /admin/netkey.json   ├─[Parse JSON]         │                  │
     │                       ├─[Save to NVS]        │                  │
     │                       ├─[Update local key]   │                  │
     │                       │                      ├─[Broadcast]─────►│
     │                       │                      │                  ├─[Apply key]
     │◄─[HTTP 200 OK]────────┤                      │                  │
```

**Alternative**: Implement Firebase Cloud Function to trigger distribution

---

### **Path 3: Provisioning Mode Control**

**OLD FLOW**:
```
Web Dashboard → External ESP32 → [UART CMD 0x15] → Gateway → [Fast Discovery Mode]
```

**NEW FLOW - Option A (Firebase Trigger)**:
```
Firebase Console → /admin/provisioning/command → Gateway polls → [Fast Discovery Mode]
```

**NEW FLOW - Option B (Direct API)**:
```
Mobile App → HTTP POST to ESP32 → Gateway REST endpoint → [Fast Discovery Mode]
```

**Recommended**: Option A (Firebase-based) for consistency

---

## 📊 Data Structure Mapping

### **sensorData (LoRa Mesh) → Firebase JSON**

**Source** (24 bytes binary):
```cpp
struct sensorData {
    uint32_t counter;      // 4 bytes
    float temperature;     // 4 bytes
    float humidity;        // 4 bytes
    float battery;         // 4 bytes
    uint32_t timestamp;    // 4 bytes
    uint16_t nodeId;       // 2 bytes
};
```

**Target** (Firebase JSON):
```json
{
  "nodes": {
    "node_0x1234": {
      "sensorData": {
        "counter": 1523,
        "temperature": 25.5,
        "humidity": 60.2,
        "battery": 3.7,
        "timestamp": 1729000000,
        "nodeId": "0x1234",
        "receivedAt": "2025-10-14T10:30:00Z",
        "rssi": -85,
        "snr": 8
      },
      "metadata": {
        "gatewayId": "0x0001",
        "hopCount": 2,
        "lastSeen": "2025-10-14T10:30:00Z"
      }
    }
  }
}
```

**Size Comparison**:
- Binary: 24 bytes
- JSON: ~180 bytes (7.5x larger)
- With Firebase overhead: ~200 bytes

**Impact**: Acceptable overhead for cloud storage

---

### **UartGatewayStatus → Firebase JSON**

**Source** (19 bytes binary):
```cpp
struct UartGatewayStatus {
    uint16_t gatewayId;
    uint32_t uptime;
    uint16_t connectedNodes;
    uint32_t totalPacketsReceived;
    uint32_t totalPacketsSent;
    uint16_t freeHeap;
    int8_t lastRSSI;
    int8_t lastSNR;
    uint8_t meshHealth;
};
```

**Target** (Firebase JSON):
```json
{
  "gateways": {
    "gateway_0x0001": {
      "status": {
        "gatewayId": "0x0001",
        "uptime": 3600,
        "connectedNodes": 5,
        "totalPacketsReceived": 1234,
        "totalPacketsSent": 1200,
        "freeHeap": 150,
        "lastRSSI": -85,
        "lastSNR": 8,
        "meshHealth": 95,
        "wifiRSSI": -65,
        "wifiSSID": "MyNetwork",
        "ipAddress": "192.168.1.100",
        "lastUpdate": "2025-10-14T10:30:00Z"
      }
    }
  }
}
```

---

### **Routing Table Entries → Firebase JSON**

**Source** (12 bytes per entry):
```cpp
struct UartRoutingEntry {
    uint16_t address;
    uint16_t via;
    uint8_t metric;
    uint8_t role;
    int8_t receivedSNR;
    uint32_t timeToLive;
};
```

**Target** (Firebase JSON):
```json
{
  "gateways": {
    "gateway_0x0001": {
      "routingTable": {
        "0x1234": {
          "address": "0x1234",
          "via": "0x0001",
          "metric": 1,
          "role": "node",
          "receivedSNR": 8,
          "timeToLive": 3600,
          "lastUpdate": "2025-10-14T10:30:00Z"
        },
        "0x5678": {
          "address": "0x5678",
          "via": "0x1234",
          "metric": 2,
          "role": "node",
          "receivedSNR": 6,
          "timeToLive": 3400,
          "lastUpdate": "2025-10-14T10:29:55Z"
        }
      }
    }
  }
}
```

**Benefit**: No multi-packet response needed! Firebase handles large documents automatically.

---

## 🔪 Cut Points Analysis

### **✂️ CUT POINT 1: Gateway Application Layer**

**File**: `gateway_app.cpp`  
**Function**: `forwardToUART(AppPacket<sensorData>* packet)`  
**Line**: ~210

**BEFORE**:
```cpp
void GatewayApp::forwardToUART(AppPacket<sensorData>* packet) {
    if (!uartProtocol || !gatewayState.uartConnected) return;
    
    sensorData* s = reinterpret_cast<sensorData*>(packet->payload);
    uint16_t sourceNode = packet->src;
    
    dataPacket dp;
    dp.counter = s->counter;
    dp.timestamp = s->timestamp;
    dp.nodeId = sourceNode;
    
    if (uartProtocol->sendDataPacket(dp, sourceNode)) {
        gatewayState.packetsForwarded++;
    }
}
```

**AFTER**:
```cpp
void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
    if (!firebaseClient || !gatewayState.firebaseConnected) {
        ESP_LOGW(TAG, "Firebase not available, queuing data");
        queueForLater(packet);
        return;
    }
    
    sensorData* s = reinterpret_cast<sensorData*>(packet->payload);
    uint16_t sourceNode = packet->src;
    
    if (firebaseClient->uploadSensorData(sourceNode, *s)) {
        gatewayState.packetsForwarded++;
        ESP_LOGI(TAG, "Uploaded data from node 0x%04X", sourceNode);
    } else {
        gatewayState.firebaseErrors++;
        ESP_LOGW(TAG, "Upload failed for node 0x%04X, retrying", sourceNode);
        retryQueue.push(packet);
    }
}
```

---

### **✂️ CUT POINT 2: Setup Function**

**File**: `gateway_app.cpp`  
**Function**: `setup()`  
**Line**: ~85

**BEFORE**:
```cpp
setupLoRaMesher();
setupUART();  // ◄── REMOVE
```

**AFTER**:
```cpp
setupLoRaMesher();
setupWiFi();      // ◄── NEW
setupFirebase();  // ◄── NEW
```

---

### **✂️ CUT POINT 3: Loop Function**

**File**: `gateway_app.cpp`  
**Function**: `loop()`  
**Line**: ~120

**BEFORE**:
```cpp
// Handle UART communication
if (uartProtocol) {
    uartProtocol->update();
    updateUARTConnection();
}
```

**AFTER**:
```cpp
// Handle WiFi connection
if (!wifiManager->isConnected()) {
    wifiManager->reconnect();
}

// Periodic Firebase uploads
static uint32_t lastStatusUpload = 0;
if (millis() - lastStatusUpload > 30000) {
    uploadGatewayStatus();
    uploadRoutingTable();
    lastStatusUpload = millis();
}
```

---

### **✂️ CUT POINT 4: Callbacks**

**File**: `gateway_app.cpp`  
**Functions**: `onNetkeyReceived()`, `onProvisioningControl()`

**BEFORE**: Triggered by UART commands from external ESP32

**AFTER**: Triggered by Firebase database changes (listeners) or HTTP POST endpoints

---

## 📈 Performance Impact Analysis

| Metric | Old (UART) | New (Firebase) | Change |
|--------|-----------|----------------|--------|
| **Data Upload Latency** | ~150ms | ~200ms | +33% |
| **Bandwidth Usage** | 5 bytes/s | 50 bytes/s | +10x |
| **Memory Usage (RAM)** | 20KB | 60KB | +200% |
| **Memory Usage (Flash)** | 800KB | 950KB | +150KB |
| **Power Consumption** | 150mA | 220mA | +70mA |
| **Code Complexity** | High | Medium | -40% |
| **Lines of Code** | 1500 | 800 | -700 |

---

## 🎯 Summary

### **Components to KEEP (100% unchanged)**:
✅ LoRa radio driver (`RadioLib`)  
✅ Mesh processing (`LoraMesher`)  
✅ Security layer (`MeshSecurityService`)  
✅ Routing table (`RoutingTableService`)  
✅ Address management (`AddressManagementService`)  
✅ NVS storage (`NVSStorageService`)  
✅ Netkey distribution (`NetkeyDistributionService`)  
✅ Hello protocol (`HelloProtocolService`)  

### **Components to REMOVE**:
❌ `uart_protocol.h` (~200 lines)  
❌ `uart_protocol.cpp` (~500 lines)  
❌ `UartProtocol` class  
❌ All UART packet structures  
❌ UART command handling  

### **Components to ADD**:
✨ `wifi_manager.h/cpp` (~200 lines)  
✨ `firebase_client.h/cpp` (~300 lines)  
✨ WiFi connection monitoring  
✨ Firebase upload functions  
✨ JSON serialization  

### **Components to MODIFY**:
⚠️ `gateway_app.h` (add WiFi/Firebase members)  
⚠️ `gateway_app.cpp` (replace UART with Firebase)  
⚠️ `gateway_config.h` (remove UART pins, add WiFi config)  
⚠️ `platformio.ini` (add Firebase library)  

---

**Next Step**: Phase 1.3 - Identify Retained Components →
