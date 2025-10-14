# Firebase Integration - LoRa Mesh Gateway

## 📋 Tổng Quan

Gateway ESP32 kết nối trực tiếp với Firebase Realtime Database để upload dữ liệu từ mạng LoRa Mesh. Không cần Bridge PC - ESP32 tự xử lý WiFi và Firebase.

### Kiến Trúc

```
┌─────────────┐      LoRa       ┌──────────────┐      WiFi      ┌──────────────┐
│  Node 0x01  │ ────────────────▶│   Gateway    │ ──────────────▶│   Firebase   │
│  (Sensor)   │                  │   ESP32      │                │   Realtime   │
└─────────────┘                  │              │                │   Database   │
                                 │  - WiFi      │                └──────────────┘
┌─────────────┐      LoRa       │  - Firebase  │
│  Node 0x02  │ ────────────────▶│  - LoRa      │
│  (Sensor)   │                  └──────────────┘
└─────────────┘

```

**Đặc điểm**:
- ✅ Kết nối WiFi tự động với retry
- ✅ Upload Firebase real-time khi có dữ liệu
- ✅ 3 loại dữ liệu: Sensor, Gateway Status, Routing Table
- ✅ Memory leak protection (TCP cleanup, String management)
- ✅ Retry mechanism với exponential backoff

---

## 🔌 Kết Nối WiFi

### WiFiConnectionService

Service quản lý kết nối WiFi với auto-reconnect:

```cpp
// src/components/lora_mesh_manager/include/wifi_connection_service.h
class WiFiConnectionService {
public:
    static bool connect(const char* ssid, const char* password);
    static bool isConnected();
    static int8_t getRSSI();  // Signal strength
};
```

**Luồng kết nối**:

```
1. Setup() → WiFiConnectionService::connect(SSID, PASSWORD)
2. Thử kết nối WiFi (timeout 10s)
3. Nếu thất bại → Retry với backoff (2s, 4s, 8s...)
4. Kết nối thành công → Log IP address & RSSI
5. Loop() → Auto-reconnect nếu mất kết nối
```

**Cấu hình** (`gateway_config.h`):
```cpp
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define WIFI_CONNECTION_TIMEOUT 10000  // 10 seconds
```

**Log example**:
```
[WiFi] Connecting to YourNetwork...
[WiFi] Connected! IP: 192.168.1.100
[WiFi] Signal strength: -45 dBm (Excellent)
```

---

## 🔥 Firebase Client

### FirebaseClient Class

Wrapper class cho Firebase ESP32 Client với memory leak protection:

```cpp
// src/application/app_gateway/firebase_client.h
class FirebaseClient {
public:
    bool connect(const char* apiKey, const char* databaseURL, 
                 const char* authToken, const char* gatewayId);
    
    UploadResult uploadSensorData(const SensorData& data, int8_t rssi, float snr);
    UploadResult uploadGatewayStatus(...);
    UploadResult uploadRoutingTable(const std::vector<RouteNode>& table);
    
    bool isConnected();
    FirebaseStats getStats();
};
```

### Khởi Tạo Firebase

**Cấu hình** (`gateway_config.h`):
```cpp
#define FIREBASE_API_KEY "AIzaSyXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define FIREBASE_DATABASE_URL "https://your-project.firebaseio.com"
#define FIREBASE_AUTH_TOKEN "your-legacy-database-secret"
#define GATEWAY_ID "gateway_001"
```

**Setup sequence**:
```cpp
void setup() {
    // 1. Kết nối WiFi
    WiFiConnectionService::connect(WIFI_SSID, WIFI_PASSWORD);
    
    // 2. Khởi tạo Firebase Client
    firebaseClient.connect(
        FIREBASE_API_KEY,
        FIREBASE_DATABASE_URL,
        FIREBASE_AUTH_TOKEN,
        GATEWAY_ID
    );
    
    // 3. Verify connection
    if (firebaseClient.isConnected()) {
        Serial.println("[Firebase] Ready to upload data");
    }
}
```

---

## 📊 Định Dạng Dữ Liệu Firebase

### 1. Sensor Data (Dữ liệu cảm biến từ Nodes)

**Path**: `sensor_data/{nodeId}/{timestamp}` và `nodes/{nodeId}/latest_data`

**JSON Structure**:
```json
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1729008000,
  "rssi": -45,
  "snr": 10.5
}
```

**Giải thích fields**:
- `counter`: Số thứ tự packet từ node (tăng dần)
- `temperature`: Nhiệt độ (°C)
- `humidity`: Độ ẩm (%)
- `battery`: Điện áp pin (V)
- `timestamp`: Unix timestamp (seconds since 1970)
- `rssi`: Signal strength từ gateway nhận được (-dBm, càng gần 0 càng tốt)
- `snr`: Signal-to-Noise Ratio (dB, càng cao càng tốt)

**Code tạo JSON**:
```cpp
String FirebaseClient::createSensorDataJson(
    const SensorData& data, 
    int8_t rssi, 
    float snr
) {
    StaticJsonDocument<256> doc;
    
    doc["counter"] = data.counter;
    doc["temperature"] = data.temperature;
    doc["humidity"] = data.humidity;
    doc["battery"] = data.battery;
    doc["timestamp"] = data.timestamp;
    
    // Chỉ thêm nếu có giá trị (node trong phạm vi)
    if (rssi != 0) doc["rssi"] = rssi;
    if (snr != 0.0f) doc["snr"] = snr;
    
    String output;
    serializeJson(doc, output);
    return output;
}
```

**Upload flow**:
```
Node gửi packet → Gateway decrypt → Parse sensor data
    ↓
    Upload đến 2 locations:
    1. nodes/{nodeId}/latest_data    (realtime dashboard)
    2. sensor_data/{nodeId}/{timestamp}    (historical charts)
    ↓
    100ms delay (prevent TCP buildup)
```

**Firebase Database structure**:
```
firebase-project/
├── nodes/
│   ├── 0xCC64/
│   │   └── latest_data/
│   │       ├── counter: 1234
│   │       ├── temperature: 25.5
│   │       ├── humidity: 65.0
│   │       └── timestamp: 1729008000
│   └── 0x4F70/
│       └── latest_data/ ...
│
└── sensor_data/
    ├── 0xCC64/
    │   ├── 1729008000/
    │   │   ├── counter: 1234
    │   │   ├── temperature: 25.5
    │   │   └── ...
    │   └── 1729008060/ ...
    └── 0x4F70/ ...
```

---

### 2. Gateway Status (Trạng thái Gateway)

**Path**: `gateways/{gatewayId}/status`

**JSON Structure**:
```json
{
  "connected_nodes": 3,
  "packets_received": 1523,
  "packets_sent": 45,
  "wifi_rssi": -52,
  "free_heap": 189456,
  "uptime": 3600,
  "timestamp": 1729008000
}
```

**Giải thích fields**:
- `connected_nodes`: Số lượng nodes trong routing table (đang kết nối)
- `packets_received`: Tổng số packets nhận được từ mạng LoRa
- `packets_sent`: Tổng số packets gửi đi (broadcast, unicast)
- `wifi_rssi`: WiFi signal strength (-dBm)
- `free_heap`: Bộ nhớ RAM còn trống (bytes)
- `uptime`: Thời gian chạy liên tục (seconds)
- `timestamp`: Thời điểm upload

**Upload interval**: Mỗi 60 giây (định kỳ)

**Code**:
```cpp
// gateway_app.cpp - loop()
if (millis() - lastStatusUploadTime >= GATEWAY_STATUS_INTERVAL) {
    firebaseClient.uploadGatewayStatus(
        connectedNodes,        // Từ routing table
        totalPacketsReceived,  // Counter toàn cục
        totalPacketsSent,      // Counter toàn cục
        WiFiConnectionService::getRSSI(),
        ESP.getFreeHeap(),
        millis() / 1000        // Uptime
    );
    lastStatusUploadTime = millis();
}
```

**Firebase structure**:
```
gateways/
└── gateway_001/
    └── status/
        ├── connected_nodes: 3
        ├── packets_received: 1523
        ├── wifi_rssi: -52
        └── timestamp: 1729008000
```

---

### 3. Routing Table (Bảng định tuyến mạng)

**Path**: `gateways/{gatewayId}/routing_table`

**JSON Structure**:
```json
{
  "node_count": 3,
  "updated_at": 1729008000,
  "nodes": {
    "0xCC64": {
      "via": "0xCC64",
      "metric": 1,
      "rssi": -45,
      "snr": 10.5
    },
    "0x4F70": {
      "via": "0x4F70",
      "metric": 1,
      "rssi": -52,
      "snr": 9.2
    },
    "0x09F8": {
      "via": "0x4F70",
      "metric": 2
    }
  }
}
```

**Giải thích fields**:
- `node_count`: Tổng số nodes trong mạng
- `updated_at`: Timestamp của lần update cuối
- `nodes`: Object chứa thông tin từng node
  - `via`: Node trung gian để đến đích (nếu metric=1 thì direct)
  - `metric`: Số hop (1=direct, 2=qua 1 node, ...)
  - `rssi`, `snr`: Chỉ có nếu `metric=1` (direct connection)

**Upload triggers**:
1. **Real-time**: Khi routing table thay đổi (node join/leave)
2. **Backup**: Mỗi 5 phút (phòng trường hợp miss event)

**Code callback**:
```cpp
// gateway_app.cpp - setup()
void setup() {
    // Đăng ký callback cho routing table changes
    RoutingTableService::setRoutingTableChangedCallback(
        onRoutingTableChanged
    );
}

// Callback function
void onRoutingTableChanged() {
    if (firebaseClient.isConnected()) {
        auto routingTable = RoutingTableService::getRoutingTable();
        firebaseClient.uploadRoutingTable(routingTable);
        Serial.println("[Gateway] Real-time routing table upload triggered");
    }
}
```

**Firebase structure**:
```
gateways/
└── gateway_001/
    └── routing_table/
        ├── node_count: 3
        ├── updated_at: 1729008000
        └── nodes/
            ├── 0xCC64/
            │   ├── via: "0xCC64"
            │   ├── metric: 1
            │   ├── rssi: -45
            │   └── snr: 10.5
            ├── 0x4F70/ ...
            └── 0x09F8/ ...
```

---

## 🔄 Luồng Hoạt Động Chính

### A. Sensor Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Node gửi LoRa packet                                    │
│    - Encrypted với AES-128                                  │
│    - Chứa: counter, temp, humidity, battery                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. Gateway nhận và xử lý                                    │
│    - Decrypt packet                                          │
│    - Parse sensor data                                       │
│    - Lấy RSSI, SNR từ radio                                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. Upload to Firebase (2 locations)                         │
│    a) nodes/{nodeId}/latest_data                            │
│       → For real-time dashboard                             │
│    b) sensor_data/{nodeId}/{timestamp}                      │
│       → For historical data & charts                        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. Memory cleanup                                            │
│    - Wait 100ms between uploads                             │
│    - Clear Firebase buffers                                  │
│    - Release String memory                                   │
└─────────────────────────────────────────────────────────────┘
```

**Code implementation**:
```cpp
void processGatewayPackets(void* parameter) {
    while (true) {
        if (LoraMesher.dataQueueSize() > 0) {
            // 1. Lấy packet từ queue
            dataPacket* packet = LoraMesher.dataQueueReceive();
            
            // 2. Decrypt & parse
            SensorData sensorData = parseSensorData(packet->payload);
            int8_t rssi = packet->rssi;
            float snr = packet->snr;
            
            // 3. Upload to Firebase
            if (firebaseClient.isConnected()) {
                auto result = firebaseClient.uploadSensorData(
                    sensorData, rssi, snr
                );
                
                if (result.success) {
                    Serial.printf("[Firebase] Uploaded node 0x%04X data\n", 
                                 sensorData.nodeId);
                }
            }
            
            // 4. Free packet memory
            delete packet;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

### B. Routing Table Update Flow

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Routing table thay đổi                                   │
│    - Node mới join network (HELLO packet)                   │
│    - Node timeout (không phản hồi)                          │
│    - Route quality thay đổi (RSSI/SNR)                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 2. RoutingTableService trigger callback                     │
│    - Callback: onRoutingTableChanged()                      │
│    - Lấy toàn bộ routing table hiện tại                     │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 3. Upload to Firebase (real-time)                           │
│    - Path: gateways/{gatewayId}/routing_table               │
│    - Format: JSON với node_count, nodes[]                   │
│    - Bao gồm: via, metric, rssi, snr                        │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ 4. Backup upload (every 5 minutes)                          │
│    - Phòng trường hợp miss callback event                   │
│    - Đảm bảo Firebase luôn sync với gateway                 │
└─────────────────────────────────────────────────────────────┘
```

**Timeline example**:
```
00:00 - Gateway boot, routing table empty
00:05 - Node 0xCC64 join → Callback → Upload (1 node)
00:10 - Node 0x4F70 join → Callback → Upload (2 nodes)
00:15 - Node 0x09F8 join → Callback → Upload (3 nodes)
00:20 - Backup upload (3 nodes)
00:25 - Backup upload (3 nodes)
01:00 - Node 0xCC64 timeout → Callback → Upload (2 nodes)
```

---

### C. Gateway Status Monitoring

```
┌─────────────────────────────────────────────────────────────┐
│ Timer: Every 60 seconds                                      │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Thu thập metrics:                                            │
│ - Connected nodes (từ routing table size)                   │
│ - Packets RX/TX (global counters)                           │
│ - WiFi RSSI (from WiFiConnectionService)                    │
│ - Free heap (ESP.getFreeHeap())                             │
│ - Uptime (millis() / 1000)                                  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Upload to Firebase                                           │
│ Path: gateways/{gatewayId}/status                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 🛡️ Memory Leak Protection

### Vấn đề gốc

Firebase ESP32 Client library không tự động cleanup TCP connections → Stack và heap exhaustion:

```
Evidence từ logs:
- Stack: 4544 bytes → 2000 bytes (sau ~10 uploads)
- Heap: 253KB → 188KB (mất 64KB trong 2 phút)
- Pattern: tcpConnect() → Stack giảm
```

### Giải pháp implement

#### 1. TCP Connection Cleanup

```cpp
bool FirebaseClient::uploadToPath(const String& path, const String& json) {
    bool success = Firebase.updateNode(m_firebaseData, path.c_str(), json);
    
    // CRITICAL FIX: Force cleanup TCP connection
    // Observed leak: Stack 4544→2000 bytes, Heap -64KB in 2 min
    m_firebaseData.clear();  // Clear internal buffers & close TCP
    
    return success;
}
```

#### 2. Upload Spacing

```cpp
bool FirebaseClient::uploadToPathWithRetry(const String& path, const String& jsonData) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (uploadToPath(path, jsonData)) {
            delay(50);  // Give WiFi stack time to cleanup
            return true;
        }
        delay(500 * (attempt + 1));  // Exponential backoff
    }
    return false;
}
```

#### 3. String Memory Management

```cpp
UploadResult FirebaseClient::uploadSensorData(...) {
    String nodeIdStr = nodeIdToString(data.nodeId);
    String jsonData = createSensorDataJson(data, rssi, snr);
    
    // Upload location 1
    String latestPath = String("nodes/") + nodeIdStr + "/latest_data";
    bool success1 = uploadToPathWithRetry(latestPath, jsonData);
    
    delay(100);  // Prevent TCP buildup
    
    // Upload location 2
    String timeSeriesPath = String("sensor_data/") + nodeIdStr + "/" + String(timestamp);
    bool success2 = uploadToPathWithRetry(timeSeriesPath, jsonData);
    
    // MEMORY FIX: Force String cleanup to prevent heap fragmentation
    latestPath = String();
    timeSeriesPath = String();
    jsonData = String();
    
    return result;
}
```

#### 4. Stack Size Increase

```cpp
// gateway_app.cpp
void createGatewayReceiveTask() {
    xTaskCreate(
        processGatewayPackets,
        "GatewayReceive",
        8192,  // Stack: 4KB → 8KB (WiFi TCP ~2KB, Firebase ~2KB, calls ~1KB)
        NULL,
        5,
        &gatewayReceiveTaskHandle
    );
}
```

### Monitoring

```cpp
// Global heap monitoring (every 30s)
void loop() {
    if (millis() - lastMemoryLogTime >= 30000) {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap = ESP.getMinFreeHeap();
        uint32_t largestBlock = ESP.getMaxAllocHeap();
        
        Serial.printf("[MEMORY] Free heap: %u bytes, Min: %u, Largest block: %u\n",
                     freeHeap, minHeap, largestBlock);
        
        // Fragmentation warning
        if (largestBlock < freeHeap / 2) {
            Serial.println("⚠️ [MEMORY] Heap fragmentation detected!");
        }
        
        lastMemoryLogTime = millis();
    }
}

// Task-level monitoring
void processGatewayPackets(void* parameter) {
    uint32_t initialHeap = ESP.getFreeHeap();
    uint32_t packetCount = 0;
    
    while (true) {
        // Process packets...
        
        // Log every 10 packets
        if (++packetCount % 10 == 0) {
            uint32_t currentHeap = ESP.getFreeHeap();
            int32_t heapDelta = currentHeap - initialHeap;
            
            Serial.printf("[MEMORY] Packets: %u, Heap delta: %d bytes\n",
                         packetCount, heapDelta);
            
            if (heapDelta < -10000) {
                Serial.println("⚠️ [MEMORY LEAK?] Heap decreased significantly!");
            }
        }
        
        // Stack check
        UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(NULL);
        if (stackHighWater < 1024) {
            Serial.printf("⚠️ [STACK] Low stack: %u bytes free\n", 
                         stackHighWater * sizeof(StackType_t));
        }
    }
}
```

---

## 📈 Statistics & Monitoring

### FirebaseStats Structure

```cpp
struct FirebaseStats {
    uint32_t totalUploads;          // Tổng số lần upload
    uint32_t successfulUploads;     // Số lần thành công
    uint32_t failedUploads;         // Số lần thất bại
    uint32_t totalBytesUploaded;    // Tổng bytes đã upload
    uint32_t averageUploadTime;     // Thời gian upload trung bình (ms)
    uint32_t lastUploadTime;        // Timestamp upload cuối
};
```

**Usage**:
```cpp
FirebaseStats stats = firebaseClient.getStats();

Serial.printf("[Firebase Stats]\n");
Serial.printf("  Total uploads: %u\n", stats.totalUploads);
Serial.printf("  Success rate: %.1f%%\n", 
             (float)stats.successfulUploads / stats.totalUploads * 100);
Serial.printf("  Failed: %u\n", stats.failedUploads);
Serial.printf("  Total bytes: %u\n", stats.totalBytesUploaded);
Serial.printf("  Avg time: %u ms\n", stats.averageUploadTime);
```

**Output example**:
```
[Firebase Stats]
  Total uploads: 1523
  Success rate: 99.8%
  Failed: 3
  Total bytes: 458900
  Avg time: 245 ms
```

---

## 🐛 Debug & Troubleshooting

### Pretty JSON Printing

Tất cả uploads đều print JSON formatted để debug:

```cpp
String FirebaseClient::createSensorDataJson(...) {
    StaticJsonDocument<256> doc;
    doc["counter"] = data.counter;
    doc["temperature"] = data.temperature;
    // ... other fields
    
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
  "timestamp": 1729008000,
  "rssi": -45,
  "snr": 10.5
}
[Firebase] Uploading to: nodes/0xCC64/latest_data
[Firebase] Upload successful (156 bytes, 245 ms)
```

### Common Issues

#### 1. WiFi Connection Failed
```
[WiFi] Connection failed, retrying...
[WiFi] Retry 1/5 in 2 seconds...
```
**Solution**: 
- Kiểm tra SSID/password trong `gateway_config.h`
- Kiểm tra WiFi signal strength (RSSI > -70 dBm)
- Reboot router nếu cần

#### 2. Firebase Authentication Error
```
[Firebase] Authentication failed: Invalid token
```
**Solution**:
- Verify `FIREBASE_AUTH_TOKEN` (Legacy database secret)
- Check Firebase Rules (allow read/write for authenticated)
- Regenerate token from Firebase Console

#### 3. Memory Leak Warnings
```
⚠️ [MEMORY LEAK?] Heap decreased by 15000 bytes
⚠️ [STACK] Low stack: 512 bytes free
```
**Solution**:
- Đã fix với TCP cleanup + String management
- Nếu vẫn xảy ra → tăng delay giữa uploads (100ms → 200ms)
- Monitor logs để xác định leak source

#### 4. Upload Failed (Retry exhausted)
```
[Firebase] Upload failed: Connection timeout
[Firebase] Retry 3/3 failed
```
**Solution**:
- Check internet connection
- Verify Firebase Database URL
- Check Firebase quotas (free tier limits)
- Increase retry attempts hoặc timeout

---

## 📊 Performance Metrics

### Typical Values

| Metric | Value | Notes |
|--------|-------|-------|
| Upload time (sensor) | 200-300 ms | 2 locations (latest + timeseries) |
| Upload time (status) | 150-250 ms | 1 location |
| Upload time (routing) | 180-280 ms | Depends on node count |
| Memory usage | ~48KB RAM | Stable after memory fixes |
| Stack usage (task) | ~4KB free | From 8KB allocation |
| Heap fragmentation | < 20% | With String cleanup |
| Success rate | > 99% | With retry mechanism |

### Expected Behavior

**Normal operation**:
```
[MEMORY] Free heap: 189456 bytes, Min: 175234, Largest block: 110592
[Firebase] Upload successful (156 bytes, 245 ms)
[MEMORY] Packets: 10, Heap delta: -1200 bytes
[Gateway] Stack high water: 4544 bytes
```

**Memory stable** → No leak:
- Heap delta oscillates ±2000 bytes (normal allocation/deallocation)
- Stack stays at ~4544 bytes free
- Largest block > 50% of free heap (low fragmentation)

---

## 🔐 Firebase Security Rules

### Recommended Rules

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

**Giải thích**:
- `auth != null`: Chỉ authenticated clients (sử dụng AUTH_TOKEN)
- `.indexOn`: Optimize queries by timestamp cho historical data
- Separate paths cho gateways/nodes để dễ quản lý permissions

---

## 📝 Configuration Summary

### gateway_config.h - All Settings

```cpp
// WiFi Configuration
#define WIFI_SSID "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
#define WIFI_CONNECTION_TIMEOUT 10000  // 10 seconds

// Firebase Configuration
#define FIREBASE_API_KEY "AIzaSyXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define FIREBASE_DATABASE_URL "https://your-project.firebaseio.com"
#define FIREBASE_AUTH_TOKEN "your-legacy-database-secret"
#define GATEWAY_ID "gateway_001"

// Upload Intervals
#define GATEWAY_STATUS_INTERVAL 60000          // 60 seconds
#define GATEWAY_ROUTING_TABLE_INTERVAL 300000  // 5 minutes (backup)

// Memory Settings
#define GATEWAY_RECEIVE_TASK_STACK_SIZE 8192   // 8KB stack
#define MEMORY_LOG_INTERVAL 30000              // 30 seconds
```

---

## 🎯 Quick Start Checklist

- [ ] **WiFi Setup**
  - [ ] Update SSID/password in `gateway_config.h`
  - [ ] Test connection (check serial for IP address)
  
- [ ] **Firebase Setup**
  - [ ] Create Firebase project
  - [ ] Get API Key from Project Settings
  - [ ] Get Database URL from Realtime Database
  - [ ] Generate Legacy Token from Database → Rules
  - [ ] Update all credentials in `gateway_config.h`
  - [ ] Configure Firebase Security Rules
  
- [ ] **Build & Upload**
  - [ ] `pio run -e esp32-gateway`
  - [ ] Upload firmware to ESP32
  - [ ] Monitor serial output (115200 baud)
  
- [ ] **Verify Operation**
  - [ ] Check WiFi connected (IP address logged)
  - [ ] Check Firebase connected
  - [ ] Wait for node packets
  - [ ] Verify data appears in Firebase Console
  - [ ] Monitor memory stability (no leak warnings)

---

## 📚 Related Documentation

- `STACK_OVERFLOW_FIX.md` - Stack overflow analysis & fix
- `MEMORY_LEAK_DETECTION.md` - Memory leak investigation
- `PRETTY_JSON_PRINTING.md` - JSON formatting implementation
- `PHASE3_COMPLETION_REPORT.md` - Firebase integration completion

---

**Document version**: 1.0  
**Last updated**: October 15, 2025  
**Author**: Gateway ESP32 Firebase Integration
