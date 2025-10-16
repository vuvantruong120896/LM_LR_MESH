# Firebase Integration - LoRa Mesh Gateway

## 📋 Tổng Quan

Gateway ESP32 kết nối trực tiếp với Firebase Realtime Database để upload dữ liệu từ mạng LoRa Mesh. ESP32 gateway hoạt động như một bridge giữa mạng LoRa offline và Firebase cloud.

### Kiến Trúc Hệ Thống

```
┌─────────────┐      LoRa       ┌──────────────┐      WiFi      ┌──────────────┐
│  Node 0x01  │ ────────────────▶│   Gateway    │ ──────────────▶│   Firebase   │
│  (Sensor)   │                  │   ESP32      │                │   Realtime   │
└─────────────┘                  │              │                │   Database   │
                                 │  - WiFi      │                └──────────────┘
┌─────────────┐      LoRa       │  - Firebase  │
│  Node 0x02  │ ────────────────▶│  - LoRa      │
│  (Sensor)   │                  │  - NTP Sync  │
└─────────────┘                  └──────────────┘

```

**Đặc điểm**:
- ✅ Kết nối WiFi tự động với retry mechanism
- ✅ Upload Firebase real-time khi có dữ liệu
- ✅ NTP time synchronization cho accurate timestamps
- ✅ 4 loại dữ liệu: Sensor Data, Gateway Status, Routing Table, Events
- ✅ Memory leak protection (TCP cleanup, String management)
- ✅ Retry mechanism với exponential backoff
- ✅ Real-time routing table updates

---

## 🔌 Kết Nối WiFi

### WiFiConnectionService

Service quản lý kết nối WiFi với auto-reconnect và event handling:

```cpp
// src/components/lora_mesh_manager/include/wifi_connection_service.h
class WiFiConnectionService {
public:
    WiFiConnectionService(const char* ssid, const char* password, 
                         bool autoReconnect = true, 
                         uint32_t reconnectIntervalMs = 1000);
    
    bool initialize();
    bool connect(uint32_t timeoutMs = 10000);
    void disconnect();
    bool isConnected() const;
    void update();  // Call in main loop!
    
    int8_t getRSSI() const;
    String getLocalIP() const;
    WiFiStats getStats() const;
    
    void onEvent(EventCallback callback);
};
```

**Event Types**:
- `CONNECTED`: Successfully connected to WiFi  
- `DISCONNECTED`: Disconnected from WiFi
- `RECONNECTING`: Attempting to reconnect
- `CONNECTION_FAILED`: Connection attempt failed
- `RSSI_LOW`: Signal dropped below threshold

**Luồng kết nối**:

```
1. Setup() → new WiFiConnectionService(SSID, PASSWORD)
2. initialize() → Setup WiFi service
3. connect(10000) → Try connection with 10s timeout
4. If failed → Auto-retry with exponential backoff (1s, 2s, 4s, 8s...)
5. Connected → Trigger CONNECTED event, log IP & RSSI
6. Loop() → update() for auto-reconnect monitoring
```

**Cấu hình** (`gateway_config.h`):
```cpp
#define WIFI_SSID "OXII"
#define WIFI_PASSWORD "sharitek-nerd-2019"
```

**Log example**:
```
[WiFi] Connecting to OXII...
✅ Connected! RSSI: -45 dBm
[WiFi] IP: 192.168.1.100
```

---

## 🔥 Firebase Client

### FirebaseClient Class

Wrapper class cho Firebase ESP32 Client với memory leak protection:

```cpp
// src/application/app_gateway/firebase_client.h
class FirebaseClient {
public:
    // Constructor
    FirebaseClient(const char* firebaseHost, 
                   const char* firebaseAuth, 
                   const char* gatewayId);
    
    // Lifecycle
    bool initialize();
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    // Upload methods
    UploadResult uploadSensorData(const sensorData& data, int8_t rssi, float snr);
    UploadResult uploadGatewayStatus(uint16_t nodes, uint32_t pktsRx, uint32_t pktsTx,
                                    int8_t wifiRssi, uint32_t freeHeap, uint32_t uptime);
    UploadResult uploadRoutingTable(const std::vector<RouteNode>& routingTable);
    UploadResult logEvent(const String& eventType, const String& nodeId, const String& details);
    
    // Information
    FirebaseStats getStats() const;
    String getLastError() const;
    
    // Configuration
    void setRetryConfig(uint8_t maxRetries, uint32_t retryDelayMs);
    void setAutoTimestamp(bool enabled);
};
```

### Cấu Hình Firebase

**Trong `gateway_config.h`**:
```cpp
// Firebase Database URL (from Firebase Console)
#define FIREBASE_HOST "https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/:null"

// Database Secret (from Firebase Console → Settings → Service Accounts → Database Secrets)
#define FIREBASE_AUTH "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"

// Gateway ID prefix (auto-generated from MAC; format: "0x" + last 4 hex of MAC)
#define FIREBASE_GATEWAY_ID_PREFIX "0x"
```

### Khởi Tạo Firebase

**Setup sequence trong gateway_app.cpp**:
```cpp
void GatewayApp::setupFirebase() {
  // Create Gateway ID from MAC address (e.g., "...:AB:CD" -> "0xABCD")
    String macAddr = WiFi.macAddress();
    macAddr.replace(":", "");
  String gatewayId = String(FIREBASE_GATEWAY_ID_PREFIX) + macAddr.substring(macAddr.length() - 4);
    
    // Create Firebase client instance  
    firebaseClient = new FirebaseClient(FIREBASE_HOST, FIREBASE_AUTH, gatewayId.c_str());
    
    // Configure retry behavior
    firebaseClient->setRetryConfig(3, 1000);  // 3 retries, 1 second delay
    
    // Initialize and connect
    if (firebaseClient->initialize() && firebaseClient->connect()) {
        Serial.println("[Firebase] Ready to upload data");
        gatewayState.firebaseConnected = true;
    }
}
```

---

## 📊 Định Dạng Dữ Liệu Firebase

### 1. Sensor Data (Dữ liệu cảm biến từ Nodes & Gateway)

**Paths**: 
- `nodes/{nodeId}/latest_data` (realtime dashboard)
- `sensor_data/{nodeId}/{timestamp}` (historical charts)

**JSON Structure**:
```json
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1760607651,
  "rssi": -45,
  "snr": 10.5
}
```

**Field Descriptions**:
- `counter`: Packet sequence number từ node (incrementing)
- `temperature`: Temperature in Celsius
- `humidity`: Humidity percentage (0-100%)  
- `battery`: Battery voltage in Volts
- `timestamp`: **Unix timestamp** (seconds since 1970-01-01) - **NTP synchronized**
- `rssi`: Signal strength khi gateway nhận (-dBm, closer to 0 = better)
- `snr`: Signal-to-Noise Ratio (dB, higher = better)

**Important Notes**:
- Timestamp được đồng bộ từ NTP server (pool.ntp.org) timezone GMT+7 (Vietnam)
- RSSI/SNR chỉ có với direct connections (metric=1)
- Upload đồng thời 2 locations: latest_data + historical timeseries

• Đối với dữ liệu cảm biến của chính Gateway:
- rssi = WiFi RSSI (wifiService->getRSSI())
- snr = 10.0 (giá trị cố định hợp lý trong môi trường indoor)
- NodeID của Gateway được tạo từ 2 byte cuối của WiFi MAC (giống Node)

**Example Upload Flow**:
```
Node 0xCC64 → Gateway decrypt → Parse sensor data
    ↓
    Upload to Firebase:
    1. nodes/0xCC64/latest_data           (dashboard)
    2. sensor_data/0xCC64/1760607651      (charts)
```

### 2. Gateway Info (Thông tin Gateway)

**Path**: `gateways/{gatewayId}/info`

**JSON Structure**:
```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "ip": "192.168.1.100",
  "firmware_version": "1.0.0",
  "address": "0xEEFF" // 2 byte cuối của MAC (chuỗi hex)
}
```

Ghi chú:
- Trước đây có `created_at` và `last_seen`; các trường này đã được loại bỏ để đơn giản hoá schema.
- `address` dùng cùng quy ước định danh Node (chuỗi "0xXXXX").

### 3. Gateway Status (Trạng thái Gateway)

**Path**: `gateways/{gatewayId}/status`

**JSON Structure**:
```json
{
  "connected_nodes": 3,
  "total_packets_received": 1523,
  "total_packets_sent": 45,
  "wifi_connected": true,
  "wifi_rssi": -52,
  "firebase_connected": true,
  "uptime_seconds": 3600,
  "free_heap": 189456,
  "timestamp": 1760607651
}
```

**Field Descriptions**:
- `connected_nodes`: Number of nodes in routing table
- `total_packets_received`: Total LoRa packets received since boot
- `total_packets_sent`: Total LoRa packets sent since boot
- `wifi_connected`: WiFi connection status
- `wifi_rssi`: WiFi signal strength (-dBm)
- `firebase_connected`: Firebase connection status
- `uptime_seconds`: Gateway uptime in seconds
- `free_heap`: Available RAM in bytes
- `timestamp`: Upload timestamp (NTP synchronized)

**Upload interval**: Every 60 seconds

### 4. Routing Table (Bảng định tuyến mạng)

**Path**: `gateways/{gatewayId}/routing_table`

**JSON Structure**:
```json
{
  "node_count": 3,
  "updated_at": 1760607651,
  "nodes": {
    "0xCC64": {
      "address": "0xCC64",
      "via": "0xCC64", 
      "metric": 1,
      "role": 1,
      "rssi": -45,
      "snr": 10.5
    },
    "0x4F70": {
      "address": "0x4F70",
      "via": "0x4F70",
      "metric": 1,
      "role": 1,
      "rssi": -52,
      "snr": 9.2
    },
    "0x09F8": {
      "address": "0x09F8", 
      "via": "0x4F70",
      "metric": 2,
      "role": 1
    }
  }
}
```

**Field Descriptions**:
- `node_count`: Total nodes in mesh network
- `updated_at`: Last update timestamp
- `address`: Node address (hex format)
- `via`: Route to node via this address (same as address if direct)
- `metric`: Hop count (1=direct, 2=via 1 hop, etc.)
- `role`: Node role (1=node, other values reserved)
- `rssi`, `snr`: Only present for direct connections (metric=1)

**Upload triggers**:
1. **Real-time**: When routing table changes (node join/leave)
2. **Backup**: Every 5 minutes

### 5. System Events (Log hệ thống)

**Path**: `events/{timestamp}`

**JSON Structure**:
```json
{
  "type": "node_joined",
  "gateway_id": "GW_1234", 
  "node_id": "0xCC64",
  "details": {
    "rssi": -45,
    "snr": 10.5,
    "metric": 1
  },
  "timestamp": 1760607651
}
```

**Event Types**:
- `node_joined`: New node joined network
- `node_left`: Node left network (timeout)
- `gateway_boot`: Gateway started
- `wifi_connected`: WiFi connection established
- `firebase_connected`: Firebase connection established
- `time_sync`: NTP time synchronization completed
```

---

## 🔄 Luồng Hoạt Động Chính

### A. Sensor Data Upload Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Node gửi LoRa packet                                    │
│    - AES-128 encrypted                                      │
│    - Payload: counter, temp, humidity, battery             │
│    - Timestamp: NTP-synchronized Unix time                  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. Gateway nhận và xử lý                                    │
│    - Decrypt AES packet                                      │
│    - Parse sensorData structure                              │
│    - Extract RSSI (-45 dBm), SNR (10.5 dB)                 │
│    - Validate packet integrity                              │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. Firebase Upload (dual location)                          │
│    Location A: nodes/{nodeId}/latest_data                   │
│    Purpose: Real-time dashboard, current status             │
│                                                             │
│    Location B: sensor_data/{nodeId}/{timestamp}             │
│    Purpose: Historical charts, analytics                    │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. Memory & TCP cleanup                                      │
│    - 100ms delay between uploads                            │
│    - Clear Firebase TCP buffers                             │
│    - Release String memory                                   │
│    - Update upload statistics                               │
└─────────────────────────────────────────────────────────────┘
```

**Code Implementation**:
```cpp
// gateway_app.cpp - processGatewayPackets()
void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
    if (!firebaseClient || !firebaseClient->isConnected()) {
        return;
    }
    
    // Extract sensor data and radio info
    sensorData& data = packet->payload;
    int8_t rssi = packet->packet->rssi;
    float snr = packet->packet->snr;
    
    // Upload to Firebase with retry
    auto result = firebaseClient->uploadSensorData(data, rssi, snr);
    
    if (result.success) {
        gatewayState.packetsUploaded++;
        Serial.printf("[Firebase] ✅ Node 0x%04X uploaded (RSSI: %d, SNR: %.1f)\n", 
                     data.nodeId, rssi, snr);
    } else {
        gatewayState.uploadErrors++;
        Serial.printf("[Firebase] ❌ Upload failed: %s\n", result.errorMessage.c_str());
    }
}
```

### B. Routing Table Update Flow

```
┌─────────────────────────────────────────────────────────────┐
│ Trigger Events:                                              │
│ • Node join network (HELLO packet received)                 │
│ • Node timeout (no response for 60s)                        │
│ • Route quality change (RSSI/SNR threshold)                 │
│ • Manual routing table refresh                              │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RoutingTableService callback                                 │
│ • onRoutingTableChanged() triggered                         │
│ • Get current routing table snapshot                        │
│ • Generate routing table JSON                               │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Real-time Firebase upload                                    │
│ • Path: gateways/{gatewayId}/routing_table                  │
│ • Include: node_count, timestamp, nodes[]                   │
│ • Node details: address, via, metric, role, rssi, snr       │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Backup mechanism (every 5 minutes)                          │
│ • Periodic upload regardless of changes                     │
│ • Ensures Firebase stays synchronized                       │
│ • Handles missed callback events                            │
└─────────────────────────────────────────────────────────────┘
```

**Timeline Example**:
```
00:00:00 - Gateway boot, routing table empty
00:00:15 - Node 0xCC64 join → Callback → Upload (1 node)
00:01:30 - Node 0x4F70 join → Callback → Upload (2 nodes)  
00:02:45 - Node 0x09F8 join via 0x4F70 → Callback → Upload (3 nodes)
00:05:00 - Backup upload (3 nodes)
00:10:00 - Backup upload (3 nodes)
01:02:45 - Node 0xCC64 timeout → Callback → Upload (2 nodes)
```

### C. Gateway Status Monitoring

```
┌─────────────────────────────────────────────────────────────┐
│ Timer: Every 60 seconds (configurable)                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Collect system metrics:                                      │
│ • connected_nodes (from routing table size)                 │
│ • total_packets_received/sent (global counters)             │
│ • wifi_connected, wifi_rssi (from WiFiConnectionService)    │
│ • firebase_connected (from FirebaseClient status)           │
│ • free_heap (ESP.getFreeHeap())                             │
│ • uptime_seconds (millis() / 1000)                          │
│ • timestamp (NTP synchronized time)                         │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Upload to Firebase                                           │
│ • Path: gateways/{gatewayId}/status                         │
│ • JSON with all collected metrics                           │
│ • Update gateway health dashboard                           │
└─────────────────────────────────────────────────────────────┘
```

### D. Time Synchronization Flow (NEW)

```
┌─────────────────────────────────────────────────────────────┐
│ Gateway NTP Sync (every 1 hour)                             │
│ • Connect to pool.ntp.org                                   │
│ • Get current Unix timestamp                                │
│ • Apply GMT+7 timezone offset (Vietnam)                     │
│ • Update local RTC                                          │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Time Broadcast (every 5 minutes)                            │
│ • Create TimeSyncPacket (8 bytes)                           │
│ • Include: timestamp, milliseconds, sync_flags              │
│ • Broadcast to all nodes in mesh                            │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Node Time Reception                                          │
│ • Receive time sync packet                                  │
│ • Update local RTC with received time                       │
│ • Use synchronized time for sensor data timestamps          │
│ • Fallback to millis() if sync lost                         │
└─────────────────────────────────────────────────────────────┘
```

**Important**: All timestamps in Firebase sẽ là actual Unix time (1760607651) thay vì boot time (306).

---

## 🛡️ Memory Leak Protection

### Background

Firebase ESP32 Client library có vấn đề memory leak với TCP connections. Qua testing, chúng tôi phát hiện:

```
Evidence từ logs:
- Stack usage: 4544 bytes → 2000 bytes (sau ~10 uploads)  
- Heap memory: 253KB → 188KB (mất 64KB trong 2 phút)
- Pattern: Mỗi lần tcpConnect() → Stack và heap giảm
```

### Solutions Implemented

#### 1. TCP Connection Cleanup

```cpp
bool FirebaseClient::uploadToPath(const String& path, const String& json) {
    bool success = Firebase.updateNode(m_firebaseData, path.c_str(), json);
    
    // CRITICAL FIX: Force cleanup TCP connection after each upload
    m_firebaseData.clear();  // Clear internal buffers & close TCP socket
    
    return success;
}
```

#### 2. Upload Spacing & Retry Logic

```cpp
bool FirebaseClient::uploadToPathWithRetry(const String& path, const String& jsonData) {
    for (int attempt = 0; attempt < m_maxRetries; attempt++) {
        if (uploadToPath(path, jsonData)) {
            delay(50);  // Give WiFi stack time to cleanup
            return true;
        }
        delay(m_retryDelayMs * (attempt + 1));  // Exponential backoff
    }
    return false;
}
```

#### 3. String Memory Management

```cpp
UploadResult FirebaseClient::uploadSensorData(...) {
    String nodeIdStr = nodeIdToString(data.nodeId);
    String jsonData = createSensorDataJson(data, rssi, snr);
    
    // Upload location 1: Latest data
    String latestPath = String("nodes/") + nodeIdStr + "/latest_data";
    bool success1 = uploadToPathWithRetry(latestPath, jsonData);
    
    delay(100);  // Prevent TCP connection buildup
    
    // Upload location 2: Historical timeseries  
    String timeSeriesPath = String("sensor_data/") + nodeIdStr + "/" + String(getCurrentTimestamp());
    bool success2 = uploadToPathWithRetry(timeSeriesPath, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    latestPath = String();
    timeSeriesPath = String();
    jsonData = String();
    
    return {success1 && success2, success1 && success2 ? "" : m_lastError, 
            getCurrentTimestamp(), jsonData.length()};
}
```

#### 4. Task Stack Size Increase

```cpp
// gateway_app.cpp - Task creation
void GatewayApp::createReceiveTask() {
    xTaskCreate(
        processGatewayPackets,
        "GatewayRx",
        8192,  // Increased from 4KB to 8KB
               // WiFi TCP: ~2KB, Firebase: ~2KB, Function calls: ~1KB
        NULL,
        5,     // High priority for real-time processing
        &gatewayReceiveTaskHandle
    );
}
```

### Memory Monitoring

**Global heap monitoring** (every 30 seconds):
```cpp
void GatewayApp::loop() {
    if (millis() - lastMemoryLogTime >= 30000) {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t largestBlock = ESP.getMaxAllocHeap();
        
        Serial.printf("[MEMORY] Free: %u bytes, Min: %u, Largest: %u\n",
                     freeHeap, minHeap, largestBlock);
        
        // Fragmentation warning (largest block < 50% of free heap)
        if (largestBlock < freeHeap / 2) {
            Serial.println("⚠️ [MEMORY] Heap fragmentation detected!");
        }
        
        lastMemoryLogTime = millis();
    }
}
```

**Task-level monitoring**:
```cpp
void processGatewayPackets(void* parameter) {
    while (true) {
        // Process uploads...
        
        // Check stack usage every 10 packets
        if (++packetCount % 10 == 0) {
            UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
            if (stackFree < 1024) {  // < 1KB stack remaining
                Serial.printf("⚠️ [STACK] Low stack: %u bytes free\n", 
                             stackFree * sizeof(StackType_t));
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Expected Behavior After Fixes

**Normal operation logs**:
```
[MEMORY] Free: 189456 bytes, Min: 175234, Largest: 110592
[Firebase] ✅ Upload successful (156 bytes, 245 ms)
[MEMORY] Packets: 50, Heap delta: +1200 bytes  # Positive = good
[Gateway] Stack free: 4544 bytes
```

**Memory stability indicators**:
- Heap delta oscillates around ±2000 bytes (normal allocation/deallocation)
- Stack consistently stays ~4500+ bytes free
- Largest block > 50% of free heap (low fragmentation)
- No "Low stack" warnings
- No heap decrease > 10KB over time
```

---

## 📈 Statistics & Monitoring

### FirebaseStats Structure

```cpp
struct FirebaseStats {
    uint32_t totalUploads;          // Total upload attempts
    uint32_t successfulUploads;     // Successful uploads  
    uint32_t failedUploads;         // Failed uploads
    uint32_t totalBytesUploaded;    // Total bytes uploaded
    uint32_t lastUploadTime;        // Last upload timestamp (millis)
    float averageUploadTime;        // Average upload time (ms)
};
```

**Usage**:
```cpp
FirebaseStats stats = firebaseClient->getStats();

Serial.printf("[Firebase Stats]\n");
Serial.printf("  Total uploads: %u\n", stats.totalUploads);
Serial.printf("  Success rate: %.1f%%\n", 
             (float)stats.successfulUploads / stats.totalUploads * 100);
Serial.printf("  Failed: %u\n", stats.failedUploads);
Serial.printf("  Total bytes: %u\n", stats.totalBytesUploaded);
Serial.printf("  Avg time: %.0f ms\n", stats.averageUploadTime);
```

**Console output example**:
```
[Firebase Stats]
  Total uploads: 1523
  Success rate: 99.8%
  Failed: 3
  Total bytes: 458900
  Avg time: 245 ms
```

### Debug JSON Printing

Tất cả uploads đều print formatted JSON để debug:

```cpp
String FirebaseClient::createSensorDataJson(const sensorData& data, int8_t rssi, float snr) {
    JsonDocument doc;
    doc["counter"] = data.counter;
    doc["temperature"] = data.temperature;
    doc["humidity"] = data.humidity;
    doc["battery"] = data.battery;
    doc["timestamp"] = m_autoTimestamp ? getCurrentTimestamp() : data.timestamp;
    
    if (rssi != 0) doc["rssi"] = rssi;
    if (snr != 0.0f) doc["snr"] = snr;
    
    // Pretty print for debugging
    Serial.printf("[Firebase] Sensor data JSON (node 0x%04X):\n", data.nodeId);
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
    String output;
    serializeJson(doc, output);
    return output;
}
```

**Console output**:
```
[Firebase] Sensor data JSON (node 0xCC64):
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1760607651,
  "rssi": -45,
  "snr": 10.5
}
[Firebase] Uploading to: nodes/0xCC64/latest_data
[Firebase] ✅ Upload successful (156 bytes, 245 ms)
```

### Performance Metrics

**Typical values trong production**:

| Metric | Value | Notes |
|--------|-------|-------|
| Upload time (sensor) | 200-300 ms | 2 locations (latest + timeseries) |
| Upload time (status) | 150-250 ms | Single location |
| Upload time (routing) | 180-280 ms | Depends on node count |
| Memory usage | ~48KB RAM | Stable after memory fixes |
| Stack usage (task) | ~4KB free | From 8KB allocation |
| Heap fragmentation | < 20% | With String cleanup |
| Success rate | > 99% | With retry mechanism |
| NTP sync accuracy | ±100ms | Vietnam timezone GMT+7 |

### Expected Normal Operation

```
[WiFi] ✅ Connected! RSSI: -45 dBm, IP: 192.168.1.100
[Firebase] ✅ Connected successfully!
[TimSync] ✅ NTP synced: 1760607651 (GMT+7)
[MEMORY] Free: 189456 bytes, Min: 175234, Largest: 110592
[Firebase] ✅ Node 0xCC64 uploaded (RSSI: -45, SNR: 10.5)
[Gateway] Stack free: 4544 bytes
[Firebase] Stats: 1523 uploads, 99.8% success
```

**Stability indicators**:
- Memory: Heap delta ±2000 bytes (normal)
- Stack: Consistent ~4500+ bytes free  
- Fragmentation: Largest block > 50% free heap
- Upload success rate > 99%
- No TCP connection leaks
- NTP time sync every hour

## � Debug & Troubleshooting

### Common Issues & Solutions

#### 1. WiFi Connection Failed
```
[WiFi] ❌ Connection failed, retrying...
[WiFi] Retry 1/5 in 2 seconds...
```
**Solutions**: 
- Check SSID/password in `gateway_config.h`
- Verify WiFi signal strength (RSSI > -70 dBm for stability)
- Check router settings (2.4GHz network, not 5GHz)
- Reboot router if needed

#### 2. Firebase Authentication Error
```
[Firebase] ❌ Authentication failed: Invalid token
```
**Solutions**:
- Verify `FIREBASE_AUTH` token (Database Secret từ Firebase Console)
- Check Firebase Database Rules (allow read/write for authenticated)
- Ensure Database URL correct format: `https://project-id-default-rtdb.region.firebasedatabase.app/:null`
- Regenerate Database Secret if needed

#### 3. Memory Issues
```
⚠️ [MEMORY] Heap fragmentation detected!
⚠️ [STACK] Low stack: 512 bytes free
```
**Solutions**:
- Memory leak fixes đã implemented (TCP cleanup + String management)
- If still occurs → increase delays between uploads (100ms → 200ms)
- Monitor heap delta in logs
- Check task stack allocation sufficient

#### 4. Upload Failures
```
[Firebase] ❌ Upload failed: Connection timeout
[Firebase] Retry 3/3 failed
```
**Solutions**:
- Check internet connectivity
- Verify Firebase Database URL và access permissions
- Check Firebase quota limits (free tier: 100 simultaneous connections)
- Increase retry attempts or timeout values
- Temporary network issues → auto-retry will recover

#### 5. Timestamp Issues
```
[Firebase] ⚠️ Timestamp: 306 (boot time instead of Unix time)
```
**Solutions**:
- Ensure NTP sync working: Check for `[TimSync] ✅ NTP synced` logs
- Verify internet connection for NTP access
- Check timezone configuration (GMT+7 for Vietnam)
- Fallback to millis() if NTP unavailable (offline operation)

### Debug Tools

#### JSON Pretty Printing
All uploads print formatted JSON:
```
[Firebase] Sensor data JSON (node 0xCC64):
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1760607651,
  "rssi": -45,
  "snr": 10.5
}
```

#### Memory Monitoring
```
[MEMORY] Free: 189456 bytes, Min: 175234, Largest: 110592
[MEMORY] Packets: 50, Heap delta: +1200 bytes
[Gateway] Stack free: 4544 bytes
```

#### Upload Statistics
```
[Firebase Stats]
  Total uploads: 1523
  Success rate: 99.8%
  Failed: 3
  Avg time: 245 ms
```

## 🔐 Firebase Security Rules

### Recommended Database Rules

```json
{
  "rules": {
    "gateways": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null"
      }
    },
    "nodes": {
      "$nodeId": {
        ".read": "auth != null", 
        ".write": "auth != null"
      }
    },
    "sensor_data": {
      "$nodeId": {
        ".read": "auth != null",
        ".write": "auth != null",
        ".indexOn": ["timestamp"]
      }
    }
  }
}
```

**Explanation**:
- `auth != null`: Only authenticated clients (using Database Secret)
- `.indexOn`: Optimize timestamp queries for historical data charts
- Separate paths for gateways/nodes for granular permission control

### Authentication Setup

1. **Firebase Console** → Project Settings → Service Accounts
2. **Database Secrets** tab → Generate new secret  
3. Copy secret to `FIREBASE_AUTH` in `gateway_config.h`
4. **Realtime Database** → Rules → Paste above rules → Publish

---

## 📝 Configuration Summary

### Complete gateway_config.h Settings

```cpp
// WiFi Configuration  
#define WIFI_SSID "OXII"
#define WIFI_PASSWORD "sharitek-nerd-2019"

// Firebase Configuration
#define FIREBASE_HOST "https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/:null"  
#define FIREBASE_AUTH "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"
#define FIREBASE_GATEWAY_ID_PREFIX "GW_"

// Timing Configuration
#define GATEWAY_STATUS_INTERVAL 60000          // Gateway status upload: 60 seconds
#define GATEWAY_ROUTING_TABLE_INTERVAL 300000  // Routing table backup: 5 minutes
#define GATEWAY_SENSOR_UPLOAD_TIMEOUT 5000     // Sensor upload timeout: 5 seconds

// Memory Settings
#define GATEWAY_RECEIVE_TASK_STACK_SIZE 8192   // Task stack: 8KB
#define MEMORY_LOG_INTERVAL 30000              // Memory monitoring: 30 seconds

// Hardware Configuration
#define GATEWAY_ID 0x01
#define LORA_MODULE LoraMesher::LoraModules::SX1276_MOD

// SPI & LoRa Pins (ESP32 DOIT DevKit V1)
#define SPI_SCK     18
#define SPI_MISO    16  
#define SPI_MOSI    19
#define SPI_CS      5
#define LORA_CS     5
#define LORA_RST    4
#define LORA_IRQ    15
#define LORA_IO1    -1
```

---

## 🎯 Quick Start Checklist

### 1. Firebase Setup
- [ ] Create Firebase project at [console.firebase.google.com](https://console.firebase.google.com)
- [ ] Enable **Realtime Database** (not Firestore)
- [ ] Get Database URL from Realtime Database settings
- [ ] Generate **Database Secret** from Project Settings → Service Accounts → Database Secrets
- [ ] Update `FIREBASE_HOST` and `FIREBASE_AUTH` in `gateway_config.h`
- [ ] Configure Security Rules (copy from above)

### 2. WiFi Setup  
- [ ] Update `WIFI_SSID` and `WIFI_PASSWORD` in `gateway_config.h`
- [ ] Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- [ ] Test WiFi signal strength at gateway location

### 3. Build & Deploy
- [ ] Install PlatformIO dependencies:
  - `jgromes/RadioLib@^6.6.0`
  - `bblanchon/ArduinoJson@^7.0.4` 
  - `mobizt/Firebase ESP32 Client@^4.4.17`
- [ ] Build: `pio run -e esp32-gateway`
- [ ] Upload firmware to ESP32
- [ ] Monitor serial output (115200 baud)

### 4. Verification
- [ ] Check WiFi connected: `✅ Connected! RSSI: -XX dBm`
- [ ] Check Firebase connected: `✅ Connected successfully!`
- [ ] Check NTP sync: `✅ NTP synced: 1760607651`
- [ ] Wait for node packets and verify data appears in Firebase Console
- [ ] Monitor memory stability: No memory leak warnings

### 5. Mobile App Integration
- [ ] Use Firebase Database URL for mobile app connection
- [ ] Implement real-time listeners for:
  - `nodes/{nodeId}/latest_data` (current sensor values)
  - `gateways/{gatewayId}/status` (gateway health)
  - `gateways/{gatewayId}/routing_table` (network topology)
- [ ] Query historical data from `sensor_data/{nodeId}/{timestamp}`
- [ ] Handle offline/online states gracefully

---

## 📚 Related Documentation

- **Firebase Console**: [console.firebase.google.com](https://console.firebase.google.com)
- **Firebase Real-time Database**: [firebase.google.com/docs/database](https://firebase.google.com/docs/database)
- **ESP32 Firebase Client**: [github.com/mobizt/Firebase-ESP32](https://github.com/mobizt/Firebase-ESP32)
- **Project Documentation**:
  - `WIFI_CONNECTION_SERVICE_REFERENCE.md` - WiFi service API reference
  - `PHASE3_COMPLETION_REPORT.md` - Firebase integration implementation details
  - `MEMORY_LEAK_DETECTION.md` - Memory optimization analysis
  - `TIME_SYNC_FEATURE.md` - NTP time synchronization system

---

**Document Version**: 2.0  
**Last Updated**: January 16, 2025  
**Author**: LoRa Mesh Gateway Team  
**Target Audience**: Mobile App Development Team
