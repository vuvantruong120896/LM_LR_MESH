# Architecture Migration Plan: UART Gateway → Firebase Gateway

## 📊 Current Architecture Analysis

### **Mô hình cũ (2 ESP32)**
```
┌─────────────────────────┐         UART         ┌──────────────────────┐
│  External ESP32         │◄──────────────────────►│ ESP32 + LoRa        │
│  (WiFi + Firebase)      │   Serial Protocol     │ (Gateway)           │
│  - Web Dashboard        │                       │ - Mesh Coordinator  │
│  - Data Processing      │                       │ - Node Management   │
│  - Cloud Upload         │                       │ - Netkey Distrib    │
└─────────────────────────┘                       └──────────────────────┘
                                                            │
                                                            │ LoRa Mesh
                                                            ▼
                                                   ┌──────────────────┐
                                                   │ ESP32 Node 1     │
                                                   │ + LoRa + Sensor  │
                                                   └──────────────────┘
                                                   ┌──────────────────┐
                                                   │ ESP32 Node 2     │
                                                   │ + LoRa + Sensor  │
                                                   └──────────────────┘
```

### **Mô hình mới (1 ESP32)**
```
┌─────────────────────────────────────────────────┐
│          ESP32 Unified Gateway                  │
│  ┌──────────────┐    ┌─────────────────────┐  │
│  │ LoRa Module  │    │  WiFi + Firebase    │  │
│  │ - Mesh Stack │    │  - Direct Upload    │  │
│  │ - Security   │    │  - Real-time Data   │  │
│  │ - Routing    │    │  - Node Info        │  │
│  └──────────────┘    └─────────────────────┘  │
└─────────────────────────────────────────────────┘
                    │
                    │ LoRa Mesh
                    ▼
            ┌──────────────────┐
            │ ESP32 Node 1     │
            │ + LoRa + Sensor  │
            └──────────────────┘
            ┌──────────────────┐
            │ ESP32 Node 2     │
            │ + LoRa + Sensor  │
            └──────────────────┘
```

---

## 🔍 Current Code Structure Analysis

### **Components cần GIỮ LẠI (LoRa Mesh Core)**
```
src/components/lora_mesh_manager/
├── include/
│   ├── LoraMesher.h                    ✅ KEEP - Core mesh functionality
│   ├── mesh_security.h                 ✅ KEEP - Security layer
│   ├── mesh_security_config.h          ✅ KEEP - Security config
│   ├── mesh_types.h                    ✅ KEEP - Common types
│   └── secure_packet.h                 ✅ KEEP - Encrypted packets
├── src/
│   ├── core/
│   │   ├── LoraMesher.cpp              ✅ KEEP - Main mesh manager
│   │   └── EspHal.cpp                  ✅ KEEP - Hardware abstraction
│   ├── network/
│   │   └── entities/                   ✅ KEEP - Network entities
│   ├── services/
│   │   ├── RoutingTableService.cpp     ✅ KEEP - Routing logic
│   │   ├── MeshSecurityService.cpp     ✅ KEEP - Security
│   │   ├── NetkeyDistributionService.cpp ✅ KEEP - Key management
│   │   ├── AddressManagementService.cpp ✅ KEEP - Address allocation
│   │   ├── NVSStorageService.cpp       ✅ KEEP - Persistent storage
│   │   └── HelloProtocolService.cpp    ✅ KEEP - Node discovery
│   └── radio/
│       └── LM_*.cpp                    ✅ KEEP - LoRa drivers
```

### **Components cần THAY THẾ (UART → Firebase)**
```
src/application/app_gateway/
├── uart_protocol.h                     ❌ REMOVE - UART communication
├── uart_protocol.cpp                   ❌ REMOVE - UART packet handling
└── gateway_config.h                    ⚠️  MODIFY - Remove UART pins, add WiFi config
```

### **Components cần THÊM MỚI (Firebase Integration)**
```
src/application/app_gateway/
├── firebase_client.h                   ✨ NEW - Firebase REST API client
├── firebase_client.cpp                 ✨ NEW - Data upload/download
├── wifi_manager.h                      ✨ NEW - WiFi connection manager
└── wifi_manager.cpp                    ✨ NEW - Auto-reconnect logic
```

---

## 📋 Migration Phases

### **Phase 1: Preparation & Analysis** ⏱️ 2-3 hours
**Objective**: Hiểu rõ data flow và dependencies

**Tasks**:
1. ✅ Document current UART protocol format
   - `UartPacket` structure analysis
   - `dataPacket` và `sensorData` structures
   - `UartGatewayStatus` format
   
2. ✅ Map data flow:
   ```
   Node → LoRa Mesh → Gateway → UART → External ESP32 → Firebase
                              ↓
                            [CUT HERE]
                              ↓
   Node → LoRa Mesh → Gateway → WiFi → Firebase (Direct)
   ```

3. ✅ Identify retained functionality:
   - Mesh network management (100% keep)
   - Security layer (100% keep)
   - Routing table (100% keep)
   - Node provisioning (100% keep)

4. ✅ List removed functionality:
   - UART packet encoding/decoding
   - UART connection monitoring
   - Serial protocol state machine

**Deliverables**:
- ✅ Data structure mapping document
- ✅ Component dependency diagram
- ✅ Firebase schema design

---

### **Phase 2: Add WiFi & Firebase Libraries** ⏱️ 1-2 hours
**Objective**: Thêm dependencies và test connectivity

**Tasks**:
1. Update `platformio.ini`:
   ```ini
   lib_deps = 
       jgromes/RadioLib@^6.6.0
       bblanchon/ArduinoJson@^7.0.0        ; NEW - JSON parsing
       mobizt/Firebase ESP32 Client@^4.4.14 ; NEW - Firebase SDK
   ```

2. Add WiFi configuration to `gateway_config.h`:
   ```cpp
   // WiFi Configuration
   #define WIFI_SSID              "YOUR_SSID"
   #define WIFI_PASSWORD          "YOUR_PASSWORD"
   #define WIFI_RECONNECT_INTERVAL 30000  // 30s
   
   // Firebase Configuration
   #define FIREBASE_HOST          "your-project.firebaseio.com"
   #define FIREBASE_AUTH          "your-database-secret-or-token"
   #define FIREBASE_PROJECT_ID    "your-project-id"
   ```

3. Create WiFi manager class:
   ```cpp
   class WiFiManager {
   public:
       bool connect();
       bool isConnected();
       void reconnect();
       int getRSSI();
   };
   ```

4. Test WiFi connection independently

**Deliverables**:
- ✅ WiFi connection manager
- ✅ Firebase authentication test
- ✅ Network status monitoring

---

### **Phase 3: Create Firebase Data Service** ⏱️ 3-4 hours
**Objective**: Implement Firebase upload logic

**Tasks**:
1. Design Firebase database structure:
   ```json
   {
     "gateways": {
       "gateway_0x0001": {
         "status": {
           "uptime": 123456,
           "connectedNodes": 5,
           "totalPackets": 1000,
           "lastUpdate": "2025-10-14T10:30:00Z"
         }
       }
     },
     "nodes": {
       "node_0x1234": {
         "info": {
           "gatewayId": "0x0001",
           "hopCount": 2,
           "rssi": -85,
           "lastSeen": "2025-10-14T10:29:55Z"
         },
         "sensorData": {
           "temperature": 25.5,
           "humidity": 60.2,
           "battery": 3.7,
           "timestamp": "2025-10-14T10:29:55Z",
           "counter": 1523
         }
       }
     },
     "routingTable": {
       "gateway_0x0001": {
         "0x1234": {"via": "0x0001", "metric": 1},
         "0x5678": {"via": "0x1234", "metric": 2}
       }
     }
   }
   ```

2. Create `FirebaseClient` class:
   ```cpp
   class FirebaseClient {
   public:
       bool initialize();
       bool uploadSensorData(uint16_t nodeId, const sensorData& data);
       bool uploadGatewayStatus(const GatewayStatus& status);
       bool uploadRoutingTable();
       bool uploadNodeInfo(uint16_t nodeId, const NodeInfo& info);
   private:
       FirebaseData fbdo;
       FirebaseAuth auth;
       FirebaseConfig config;
   };
   ```

3. Implement upload methods with error handling
4. Add retry logic for failed uploads
5. Implement batch upload for efficiency

**Deliverables**:
- ✅ Firebase client library integration
- ✅ Data upload functions
- ✅ Error handling & retry mechanism

---

### **Phase 4: Refactor Gateway Application** ⏱️ 4-5 hours
**Objective**: Replace UART logic with Firebase logic

**Tasks**:
1. **Modify `gateway_app.h`**:
   ```cpp
   // REMOVE:
   #include "uart_protocol.h"
   UartProtocol* uartProtocol;
   bool uartConnected;
   
   // ADD:
   #include "firebase_client.h"
   #include "wifi_manager.h"
   FirebaseClient* firebaseClient;
   WiFiManager* wifiManager;
   bool wifiConnected;
   bool firebaseConnected;
   ```

2. **Modify `gateway_app.cpp` - setup()**:
   ```cpp
   void GatewayApp::setup() {
       // ... existing mesh setup ...
       
       // REMOVE:
       // setupUART();
       
       // ADD:
       setupWiFi();
       setupFirebase();
   }
   
   void GatewayApp::setupWiFi() {
       wifiManager = new WiFiManager();
       if (wifiManager->connect()) {
           ESP_LOGI(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
       }
   }
   
   void GatewayApp::setupFirebase() {
       firebaseClient = new FirebaseClient();
       if (firebaseClient->initialize()) {
           ESP_LOGI(TAG, "Firebase initialized successfully");
       }
   }
   ```

3. **Modify `gateway_app.cpp` - loop()**:
   ```cpp
   void GatewayApp::loop() {
       // Check WiFi connection
       if (!wifiManager->isConnected()) {
           wifiManager->reconnect();
       }
       
       // ... existing mesh processing ...
       
       // Periodic status upload (every 30s)
       static uint32_t lastStatusUpload = 0;
       if (millis() - lastStatusUpload > 30000) {
           uploadGatewayStatus();
           uploadRoutingTable();
           lastStatusUpload = millis();
       }
   }
   ```

4. **Replace `forwardToUART()` with `uploadToFirebase()`**:
   ```cpp
   // OLD:
   void GatewayApp::forwardToUART(AppPacket<sensorData>* packet) {
       if (!uartProtocol || !gatewayState.uartConnected) return;
       uartProtocol->sendDataPacket(...);
   }
   
   // NEW:
   void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
       if (!firebaseClient || !gatewayState.firebaseConnected) {
           ESP_LOGW(TAG, "Firebase not available");
           return;
       }
       
       sensorData* data = reinterpret_cast<sensorData*>(packet->payload);
       uint16_t nodeId = packet->src;
       
       if (firebaseClient->uploadSensorData(nodeId, *data)) {
           gatewayState.packetsForwarded++;
           ESP_LOGI(TAG, "Uploaded sensor data from node 0x%04X", nodeId);
       } else {
           gatewayState.firebaseErrors++;
           ESP_LOGW(TAG, "Failed to upload data from node 0x%04X", nodeId);
       }
   }
   ```

5. **Update packet processing task**:
   ```cpp
   void GatewayApp::processGatewayPackets(void* parameter) {
       for (;;) {
           ulTaskNotifyTake(pdPASS, portMAX_DELAY);
           
           while (GatewayApp::instance->radio.getReceivedQueueSize() > 0) {
               AppPacket<uint8_t>* packet = 
                   GatewayApp::instance->radio.getNextAppPacket<uint8_t>();
               
               AppPacket<sensorData>* sensorPacket = 
                   reinterpret_cast<AppPacket<sensorData>*>(packet);
               
               // CHANGED: Upload to Firebase instead of forwarding to UART
               GatewayApp::instance->uploadToFirebase(sensorPacket);
               
               GatewayApp::instance->radio.deletePacket(packet);
           }
       }
   }
   ```

**Deliverables**:
- ✅ Refactored `GatewayApp` class
- ✅ WiFi connection management integrated
- ✅ Firebase upload integrated
- ✅ UART code removed

---

### **Phase 5: Remove UART Dependencies** ⏱️ 1-2 hours
**Objective**: Clean up unused code

**Tasks**:
1. Delete files:
   - `src/application/app_gateway/uart_protocol.h`
   - `src/application/app_gateway/uart_protocol.cpp`

2. Remove from `gateway_config.h`:
   ```cpp
   // DELETE these lines:
   #define UART_NUM                1
   #define UART_BAUD_RATE          115200
   #define UART_TX_PIN             21
   #define UART_RX_PIN             20
   #define UART_BUFFER_SIZE        1024
   // ... all UART defines
   ```

3. Update `GatewayState` struct:
   ```cpp
   struct GatewayState {
       // REMOVE:
       // bool uartConnected = false;
       // uint32_t uartErrors = 0;
       
       // ADD:
       bool wifiConnected = false;
       bool firebaseConnected = false;
       uint32_t firebaseErrors = 0;
       uint32_t packetsForwarded = 0;
       uint32_t totalMeshPackets = 0;
   };
   ```

4. Clean up includes in `gateway_app.h`

**Deliverables**:
- ✅ UART code removed
- ✅ Clean compilation
- ✅ Reduced binary size

---

### **Phase 6: Testing & Validation** ⏱️ 2-3 hours
**Objective**: Verify all functionality works

**Tests**:
1. **WiFi Connection Test**:
   - Boot gateway
   - Verify WiFi connects automatically
   - Test reconnection after disconnect
   - Check RSSI monitoring

2. **Firebase Upload Test**:
   - Manually trigger sensor data upload
   - Verify JSON format in Firebase Console
   - Check timestamp accuracy
   - Validate data types

3. **Mesh Network Test**:
   - Connect 2-3 nodes
   - Verify routing table builds correctly
   - Check HELLO protocol still works
   - Validate netkey distribution

4. **Integration Test**:
   - Node sends sensor data
   - Gateway receives via LoRa
   - Data appears in Firebase (< 5s delay)
   - Routing table synced to Firebase

5. **Stress Test**:
   - 5 nodes sending data every 30s
   - Run for 1 hour
   - Check packet loss rate
   - Monitor memory usage
   - Verify Firebase quota usage

**Deliverables**:
- ✅ Test results document
- ✅ Performance metrics
- ✅ Known issues list

---

### **Phase 7: Optimization & Documentation** ⏱️ 2-3 hours
**Objective**: Polish and document

**Tasks**:
1. **Performance Optimization**:
   - Reduce Firebase write frequency
   - Implement local data buffering
   - Add data aggregation (batch upload)
   - Optimize JSON payload size

2. **Error Handling**:
   - WiFi connection failures
   - Firebase authentication errors
   - Network timeout handling
   - Data queue overflow

3. **Documentation**:
   - Update README.md
   - Create FIREBASE_SETUP.md
   - Document Firebase schema
   - Add troubleshooting guide

4. **Code Cleanup**:
   - Remove debug logs
   - Add proper comments
   - Consistent naming
   - Remove dead code

**Deliverables**:
- ✅ Production-ready code
- ✅ Complete documentation
- ✅ Deployment guide

---

## 📁 New File Structure

```
src/application/app_gateway/
├── gateway_app.h                    ⚠️  MODIFIED - Remove UART, add WiFi/Firebase
├── gateway_app.cpp                  ⚠️  MODIFIED - Replace forwardToUART with uploadToFirebase
├── gateway_config.h                 ⚠️  MODIFIED - Remove UART pins, add WiFi credentials
├── firebase_client.h                ✨ NEW - Firebase REST API wrapper
├── firebase_client.cpp              ✨ NEW - Upload/download implementation
├── wifi_manager.h                   ✨ NEW - WiFi connection manager
└── wifi_manager.cpp                 ✨ NEW - Auto-reconnect & monitoring

src/components/lora_mesh_manager/   ✅ NO CHANGES - Keep all existing code
```

---

## ⚠️ Critical Considerations

### **Memory Usage**
- Firebase library adds ~100KB to binary
- WiFi stack uses ~40KB RAM
- Consider reducing LoRa packet buffer if RAM limited
- Monitor heap fragmentation

### **Network Reliability**
- Implement exponential backoff for Firebase retries
- Queue data locally if WiFi disconnected (max 100 packets)
- Prioritize mesh processing over cloud upload
- Add watchdog timer for WiFi freeze

### **Security**
- **DO NOT hardcode WiFi password in source code**
- Store credentials in NVS (encrypted)
- Use Firebase authentication tokens (not API key)
- Implement HTTPS certificate validation
- Consider WPA2-Enterprise for production

### **Firebase Quotas**
- Free tier: 50K reads + 20K writes per day
- Typical usage: 5 nodes × 120 updates/day = 600 writes/day ✅
- Gateway status: 2,880 writes/day (every 30s) ⚠️
- Consider reducing status frequency to 5 minutes

### **Power Consumption**
- WiFi increases power by ~70mA average
- Consider deep sleep between uploads
- Disable WiFi during mesh provisioning
- Monitor battery drain on ESP32

---

## 🎯 Success Criteria

✅ **Must Have**:
1. Gateway boots and connects to WiFi automatically
2. Sensor data from nodes uploaded to Firebase within 10 seconds
3. Routing table synchronized to Firebase
4. No mesh functionality degraded
5. Stable operation for 24+ hours
6. WiFi auto-reconnect works reliably

⭐ **Nice to Have**:
1. Real-time Firebase listeners (bidirectional)
2. Remote configuration updates via Firebase
3. Over-the-air (OTA) firmware updates
4. Historical data caching during WiFi outage
5. Mobile app integration

---

## 📅 Estimated Timeline

| Phase | Duration | Cumulative |
|-------|----------|-----------|
| Phase 1: Analysis | 2-3 hours | 3h |
| Phase 2: Libraries | 1-2 hours | 5h |
| Phase 3: Firebase Service | 3-4 hours | 9h |
| Phase 4: Refactor Gateway | 4-5 hours | 14h |
| Phase 5: Cleanup | 1-2 hours | 16h |
| Phase 6: Testing | 2-3 hours | 19h |
| Phase 7: Optimization | 2-3 hours | 22h |

**Total: ~20-22 hours** (2.5-3 working days)

---

## 🚀 Next Steps

1. ✅ Review this migration plan
2. ⏳ Approve Phase 1 to proceed
3. ⏳ Create Firebase project and get credentials
4. ⏳ Backup current working code (`git branch backup-uart-version`)
5. ⏳ Begin Phase 2: Add WiFi & Firebase libraries

Ready to proceed with Phase 2? 🚀
