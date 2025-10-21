# BLE Offline Data Sync - Phân Tích Chi Tiết & Kế Hoạch Phát Triển

**Ngày**: Oct 21, 2025  
**Trạng Thái**: Phân Tích Chi Tiết  
**Phiên Bản**: v1.0

---

## 🎯 YÊU CẦU CHỐT LẠI

### **A. GATEWAY SIDE (ESP32)**

#### 1. Dữ Liệu Đồng Bộ
- ✅ **Sensor Data** ONLY (từ OfflineDataBuffer)
- ❌ Không sync routing table / status trong MVP này
- Dữ liệu: Array của `{timestamp, nodeId, counter, battery, sensorReadings}`

#### 2. Trigger Sync
- 📲 **On-demand từ App**: App gửi BLE request → Gateway response
- ❌ Không auto-push từ Gateway
- Gateway dành sẵn dữ liệu trong buffer, chờ app request

#### 3. Xóa Data
- ✅ **Delete after sync**: Sau khi app receive thành công → remove từ NVS
- Cơ chế: App gửi ACK → Gateway delete

#### 4. Retry Policy
- ✅ **Max 2 retries** nếu BLE transfer fail
- Logic: Retry từ app side (not from gateway)
- Gateway không cần retry, chỉ keep data trong buffer

#### 5. Status Management
- ✅ **Quản lý định kỳ với interval 10s** (heap, uptime, rssi)
- ⚠️ **Nhưng không gửi qua BLE** (chỉ log local lúc này)
- Chuẩn bị cho future: routing table, extended status

#### 6. ⚠️ BLE Conflict Handling
**CẦN CHÂN TRỌNG**: Hiện tại có BLE Provisioning task riêng
```
Current BLE Stack:
├─ ProvisionManager (BLE Provisioning)
│  └─ Handle WiFi/UID provisioning
│  └─ Task: BLE GATT attributes
│
└─ NEW: BLE Sync Service (DATA SYNC)
   └─ Handle sensor data sync
   └─ INDEPENDENT GATT attributes
   └─ NO CONFLICT (separate characteristics)
```

**Strategy**:
- Provisioning: GATT UUID `550e8400-xxx` (provisioning data)
- Data Sync: GATT UUID `660e8400-xxx` (sensor data)
- Cùng BLE connection, nhưng KHÔNG share state
- Mutex: Nếu app đang provisioning → block sync (và vice versa)

---

### **B. APP SIDE (Mobile - Kagri App)**

#### 1. UI: Long Press Menu (3s)
```
Device Screen:
┌─────────────────────────────┐
│  Gateway: "0xE764"          │
│  Status: Connected          │
│  [Long Press 3s on card]    │
└─────────────────────────────┘
        ↓ (Hold 3s)
┌──────────────────────────────────┐
│  📋 Device Options               │
├──────────────────────────────────┤
│  1️⃣  📝 Đổi tên Device          │
│  2️⃣  📶 Cập nhập WiFi (mới)     │
│  3️⃣  🔧 Cập nhật phần mềm       │
│  4️⃣  🔄 Đồng bộ dữ liệu (NEW)   │
├──────────────────────────────────┤
│  [Cancel]                        │
└──────────────────────────────────┘
```

#### 2. Long Press Duration
- ✅ **3 seconds** hold time
- Visual: Progress bar / haptic feedback
- Cancel on release before 3s

#### 3. Data Sync Flow
```
App (Sync Data button)
    ↓
BLE Connect (if not connected)
    ↓
Request: "GET_BUFFERED_DATA" (offset=0)
    ↓
Gateway Response: {status, count, payload: [sensorData...]}
    ↓
App: Parse & Store Locally
    ↓
Send ACK: "CLEAR_BUFFER" (optional)
    ↓
Gateway: Delete from NVS ✅
    ↓
App: Batch Upload to Firebase
    ↓
Show: "Synced X samples"
```

#### 4. Firebase Upload
- ✅ 收到 BLE data → Transform format
- ✅ Upload like normal sensor data
- ✅ Same schema: timestamp, nodeId, sensorData
- ✅ Mark as "offline_synced" (optional metadata)

---

## 🏗️ KIẾN TRÚC GATEWAY

### **BLE Provisioning vs BLE Data Sync**

```cpp
// ============ Current: BLE Provisioning ============
ProvisionManager::startBLEProvisioning()
  ├─ BLE Server init
  ├─ GATT Service: Provisioning
  │  ├─ Characteristic: WiFi SSID (write)
  │  ├─ Characteristic: WiFi Password (write)
  │  ├─ Characteristic: UID (write)
  │  └─ Characteristic: Status (notify)
  └─ Thread: "BLE_Provisioning" (handles writes)

// ============ NEW: BLE Data Sync ============
BleSyncService::init()
  ├─ Reuse existing BLE Server (SAME)
  ├─ Add GATT Service: Data Sync (NEW)
  │  ├─ Characteristic: Command (write)  [App → Gateway]
  │  │  Payload: {cmd_id, offset, ...}
  │  │
  │  ├─ Characteristic: Response (read)  [Gateway → App]
  │  │  Payload: {status, count, data...}
  │  │
  │  └─ Characteristic: Status (notify)  [Unidirectional]
  │     Payload: {sync_progress}
  │
  └─ Handler: BleSyncService::handleCommand() (SYNC context)

// ============ Conflict Prevention ============
Mutex: ble_sync_mutex
  ├─ When provisioning active → BLOCK data sync
  ├─ When data sync active → ALLOW provisioning (separate task)
  └─ Check on each request
```

### **Data Flow in Gateway**

```
┌─────────────────────────────────────────────┐
│         LoRa Mesh Packets                   │
└────────────────┬────────────────────────────┘
                 │
        ┌────────▼────────┐
        │ processLoRaData │
        └────────┬────────┘
                 │
         ┌───────▼────────┐
         │ Firebase Queue │
         │   (Online)     │
         │   (Offline)    │
         └───────┬────────┘
                 │
        ┌────────▼─────────────┐
        │ OfflineDataBuffer    │
        │ (Already exists)     │
        │ [50 samples max]     │
        └────────┬─────────────┘
                 │
        ┌────────▼──────────┐
        │  NEW: Status Mgr  │
        │ (Local tracking)  │
        └────────┬──────────┘
                 │
        ┌────────▼────────────────┐
        │ BLE Sync Service (NEW)  │
        │                         │
        │ On BLE Request:         │
        │ - Read from buffers     │
        │ - Serialize payload     │
        │ - Send via GATT         │
        │ - Wait for ACK          │
        │ - Delete on ACK         │
        │ - Retry (max 2x)        │
        └─────────────────────────┘
                 │
        ┌────────▼─────────────┐
        │   BLE Connection    │
        │   (to Mobile App)   │
        └─────────────────────┘
```

---

## 📡 BLE SYNC PROTOCOL (Chi Tiết)

### **GATT Services Layout**

```
Device Name: "Gateway-E764"

SERVICE 1: Provisioning (UUID: 550e8400-e29b-41d4-a716-446655440000)
├─ Char: WiFi SSID          [550e8400-...0001] RW
├─ Char: WiFi Password      [550e8400-...0002] RW
├─ Char: UID                [550e8400-...0003] RW
└─ Char: Provision Status   [550e8400-...0004] RN

SERVICE 2: Data Sync (UUID: 660e8400-e29b-41d4-a716-446655440000) ← NEW
├─ Char: Sync Command       [660e8400-...0001] RW
├─ Char: Sync Response      [660e8400-...0002] RN
├─ Char: Sync Status        [660e8400-...0003] RN
└─ Char: Device Status      [660e8400-...0004] RN (future)
```

### **Command: GET_BUFFERED_DATA**

```
REQUEST (App → Gateway):
┌─────────────┬──────────────┬──────────────┬──────────────┐
│ Command ID  │ Flags        │ Offset       │ Reserved     │
│ 0x01        │ 1 byte       │ 2 bytes      │ 4 bytes      │
├─────────────┼──────────────┼──────────────┼──────────────┤
│ GET_DATA    │ 0x00         │ 0 (start)    │ 0x00000000   │
└─────────────┴──────────────┴──────────────┴──────────────┘

RESPONSE (Gateway → App):
┌──────────┬───────┬──────────┬─────────┬──────────────────────┐
│ Status   │ Count │ Total    │ Checksum│ Payload              │
│ 1 byte   │ 1 byte│ 2 bytes  │ 2 bytes │ Variable (max 240B)  │
├──────────┼───────┼──────────┼─────────┼──────────────────────┤
│ 0x00=OK  │ 10    │ 10       │ CRC16   │ [SensorData × 10]    │
│ 0x01=EOF │       │          │         │                      │
│ 0x02=ERR │       │          │         │                      │
└──────────┴───────┴──────────┴─────────┴──────────────────────┘

Each SensorData in payload:
┌──────────┬──────────┬──────────┬──────────┬──────┬───────────┐
│ Timestamp│ NodeID   │ Counter  │ Battery  │ Temp │ ... (var) │
│ 4 bytes  │ 2 bytes  │ 2 bytes  │ 1 byte   │ 2 byt│ ... (var) │
└──────────┴──────────┴──────────┴──────────┴──────┴───────────┘
```

### **Command: CLEAR_BUFFER**

```
REQUEST (App → Gateway):
┌─────────────┬───────┐
│ Command ID  │ ACK   │
│ 0x02        │ 0x01  │
└─────────────┴───────┘

RESPONSE (Gateway → App):
┌──────────┬──────────┐
│ Status   │ Deleted  │
│ 0x00=OK  │ 10       │
└──────────┴──────────┘
```

### **Pagination Example**

```
App Request 1: offset=0
  ↓ Gateway Response: 10 items, total=50
  ↓ App stores [0-9]

App Request 2: offset=10
  ↓ Gateway Response: 10 items, total=50
  ↓ App stores [10-19]

...

App Request 5: offset=40
  ↓ Gateway Response: 10 items, total=50, EOF=1
  ↓ App stores [40-49]

App Request 6: offset=50
  ↓ Gateway Response: 0 items, total=50, EOF=1, status=0x01 (EOF)
  ↓ App knows sync is complete
```

---

## 🛠️ GATEWAY IMPLEMENTATION STRUCTURE

### **New Files to Create**

```
src/application/app_gateway/
├─ ble_sync_service.h         (header)
├─ ble_sync_service.cpp       (implementation)
│
├─ offline_status_buffer.h    (header)
├─ offline_status_buffer.cpp  (implementation - status snapshots)
│
└─ ble_sync_protocol.h        (protocol definitions)
    ├─ Command IDs
    ├─ Response status codes
    ├─ Packet structures
    └─ Constants (CRC, timeouts)
```

### **Integration Points**

```
gateway_app.cpp:
├─ setup(): 
│  └─ Call BleSyncService::init() AFTER ProvisionManager init
│
├─ loop():
│  └─ Call StatusBuffer::update() every 10s (non-blocking)
│
└─ setupFirebase():
   └─ Ensure both services initialized

provision_manager.h/cpp:
└─ Add: registerDataSyncService() method
   └─ Pass BLE server handle to BleSyncService
```

---

## 📱 APP IMPLEMENTATION STRUCTURE

### **New Files to Create (Kagri App)**

```
lib/features/device_detail/
├─ presentation/
│  ├─ widgets/
│  │  ├─ device_long_press_menu.dart       (NEW)
│  │  └─ ble_sync_progress_dialog.dart     (NEW)
│  │
│  └─ pages/
│     └─ device_detail_page.dart           (MODIFY)
│        └─ Add long press detector
│
├─ domain/
│  ├─ entities/
│  │  └─ offline_sensor_data.dart          (NEW)
│  │
│  └─ usecases/
│     ├─ sync_offline_data_usecase.dart    (NEW)
│     └─ upload_synced_data_usecase.dart   (NEW)
│
└─ data/
   ├─ datasources/
   │  └─ ble_data_sync_datasource.dart     (NEW)
   │     ├─ getBufferedData()
   │     ├─ clearBuffer()
   │     └─ retryWithBackoff()
   │
   └─ repositories/
      └─ offline_sync_repository.dart      (NEW)
```

### **UI Flow (State Machine)**

```
Device Detail Screen
    │
    └─ Long Press Detector (3s)
       │
       ├─ [Cancel] (< 3s release)
       │   └─ Do nothing
       │
       └─ [Complete] (≥ 3s hold)
          └─ Show Menu Dialog
             │
             ├─ Option 1: Rename → Existing flow
             ├─ Option 2: WiFi → Existing flow
             ├─ Option 3: Update → Existing flow
             └─ Option 4: Sync Data (NEW)
                │
                ├─ Check BLE Connection
                │  ├─ Connected: Proceed
                │  └─ Disconnected: Auto-connect
                │
                ├─ Show Progress Dialog
                │  ├─ "Requesting data..."
                │  ├─ Progress bar (0-100%)
                │  └─ Item count displayed
                │
                ├─ BLE Read with Retry (max 2)
                │  ├─ Request GET_BUFFERED_DATA
                │  ├─ On timeout/error → Retry
                │  ├─ After 2 failures → Show error
                │  └─ On success → Parse & store
                │
                ├─ Send ACK: CLEAR_BUFFER
                │
                ├─ Upload to Firebase
                │  ├─ Transform data format
                │  ├─ Batch upload
                │  └─ Mark as "offline_synced"
                │
                └─ Show Success
                   └─ "Synced X items"
                      "Uploaded to Firebase"
```

---

## ⚡ STATUS BUFFER (10s Interval)

### **Gateway: Status Management**

```cpp
// Status recorded every 10s (local only, not sent via BLE yet)
struct GatewayStatusSnapshot {
    uint32_t timestamp;           // When recorded
    uint32_t freeHeap;            // Current heap
    uint32_t heapMin;             // Minimum seen
    int8_t wifiRssi;              // WiFi signal
    uint32_t uptime;              // Seconds since boot
    uint16_t totalPacketsReceived; // LoRa counter
    uint8_t bufferedSamples;      // Items in offline buffer
    uint8_t syncProgress;         // 0-100%
    // ... more fields as needed
};

// Keep last 10 snapshots (circular buffer)
class OfflineStatusBuffer {
    static const int HISTORY_SIZE = 10;
    
    void update();                // Called every 10s from loop()
    void recordSnapshot();        // Internal
    const GatewayStatusSnapshot* getLatest();
    const GatewayStatusSnapshot* getHistory(int index); // 0=latest, 9=oldest
};
```

### **Gateway: Update Logic**

```cpp
// In gateway_app.cpp loop()
static uint32_t lastStatusUpdate = 0;
const uint32_t STATUS_UPDATE_INTERVAL = 10000; // 10 seconds

if (millis() - lastStatusUpdate >= STATUS_UPDATE_INTERVAL) {
    OfflineStatusBuffer::update();  // Record snapshot
    lastStatusUpdate = millis();
}
```

---

## 🔄 BLE SYNC SERVICE CLASS DESIGN

### **Header: ble_sync_service.h**

```cpp
class BleSyncService {
public:
    // Initialization
    static bool init(BLEServer* bleServer);
    static void shutdown();
    
    // Data operations
    static bool addDataFromOfflineBuffer(const OfflineDataBuffer& buffer);
    static std::vector<SensorData> getBufferedData(uint16_t offset, uint16_t maxCount);
    static bool clearBufferAfterSync(uint16_t count);
    
    // BLE command handlers
    static void handleGetBufferedData(BLECharacteristic* pCharacteristic);
    static void handleClearBuffer(BLECharacteristic* pCharacteristic);
    
    // Status
    static uint16_t getPendingCount();
    static bool isSyncing();
    static uint8_t getRetryCount();
    
private:
    static bool provisioning_active_;  // Conflict check
    static uint8_t retry_count_;
    static const uint8_t MAX_RETRIES = 2;
};
```

---

## 🚨 CONFLICT PREVENTION STRATEGY

### **Scenario 1: App syncing → User triggers provisioning**

```
Timeline:
T0: App starts BLE sync (GET_BUFFERED_DATA)
T1: User clicks WiFi change → Starts provisioning
T2: Block provisioning → Show "Sync in progress, wait"
T3: Sync completes → Provisioning can now start
```

**Implementation**:
```cpp
// In ProvisionManager
if (BleSyncService::isSyncing()) {
    ESP_LOGW(TAG, "BLE Sync in progress, defer provisioning");
    return false;  // Block
}
```

### **Scenario 2: Provisioning active → BLE data request**

```
Timeline:
T0: User in WiFi provisioning screen
T1: App tries to sync data (in background)
T2: Gateway checks provisioning_active_ flag
T3: Reject sync request → App retry after 5s
```

**Implementation**:
```cpp
// In BleSyncService::handleGetBufferedData()
if (ProvisionManager::isProvisioningActive()) {
    ESP_LOGW(TAG, "Provisioning active, deny sync request");
    return sendErrorResponse(0x02);  // Error status
}
```

---

## 📊 TIMELINE PHÁT TRIỂN

### **Phase 1: Foundation (3-4 days)**

**Gateway**:
- [ ] `ble_sync_protocol.h` - Protocol definitions
- [ ] `ble_sync_service.h/cpp` - Core sync logic
- [ ] `offline_status_buffer.h/cpp` - Status tracking
- [ ] Integration: gateway_app.cpp (status update every 10s)
- [ ] Testing: Local unit tests

**App**:
- [ ] Nothing in Phase 1 (foundation prep)

**Output**: Gateway ready for BLE data requests

---

### **Phase 2: Integration (3-4 days)**

**Gateway**:
- [ ] Integrate BleSyncService into ProvisionManager
- [ ] Add GATT characteristic (write/read/notify)
- [ ] Implement conflict prevention (mutex logic)
- [ ] Handle ACK mechanism for buffer deletion
- [ ] Testing: BLE connection tests

**App**:
- [ ] Long press menu UI widget
- [ ] BLE data sync datasource
- [ ] Sync progress dialog
- [ ] Basic BLE read implementation (max 2 retries)
- [ ] Testing: UI/UX testing with mock data

**Output**: Full offline sync capability (Gateway ↔ App)

---

### **Phase 3: Firebase Sync (2-3 days)**

**App**:
- [ ] Transform synced data format
- [ ] Firebase batch upload
- [ ] Mark data as "offline_synced"
- [ ] Success/error handling
- [ ] Testing: E2E testing

**Output**: Complete offline → online flow

---

### **Phase 4: Testing & Polish (2 days)**

- [ ] E2E tests: Offline scenario
- [ ] Stress tests: 50 items sync, retry failures
- [ ] Edge cases: Connection drops, partial syncs
- [ ] Performance: Battery, BLE throughput
- [ ] Documentation & README

---

## 🎯 ACCEPTANCE CRITERIA

### **Gateway**

- [x] Status recorded every 10s (not sent yet)
- [x] BLE data sync independent from provisioning
- [x] No mutual blocking (both can work, but careful sync)
- [x] Retry max 2 times on failure
- [x] Delete after app ACK
- [x] Handle large buffer (50 items) with pagination
- [x] No crashes, proper error handling

### **App**

- [x] Long press (3s) shows menu with 4 options
- [x] "Sync Data" option visible and functional
- [x] BLE read with retry (max 2)
- [x] Show progress dialog during sync
- [x] Auto-upload to Firebase after sync
- [x] Show success/error messages
- [x] No UI blocking during BLE sync

---

## 🔗 DEPENDENCIES

**Gateway**:
- FreeRTOS (already used)
- ESP32 BLE Stack (already used in provisioning)
- NVS (already used)
- Firebase Queue (already implemented)

**App**:
- flutter_reactive_ble (already used for provisioning)
- Provider (state management, already used)
- FirebaseAuth + Firestore (already used)

---

## ⚠️ RISKS & MITIGATION

| Risk | Severity | Mitigation |
|------|----------|-----------|
| BLE provisioning & sync conflict | HIGH | Mutex + state tracking + clear documentation |
| Large buffer not fitting in BLE MTU | MEDIUM | Pagination + offset mechanism tested |
| Sync timeout during upload | MEDIUM | Retry (max 2) + user can retry manually |
| Data loss on error | LOW | ACK-based deletion (only delete after confirm) |
| Battery drain from long BLE sync | LOW | Show progress + allow user to cancel |

---

## 📝 NOTES

1. **Status buffer** (10s) không gửi BLE trong MVP - chỉ local logging
2. **Provisioning conflict** handled via state flags, không lock/mutex (simpler)
3. **Retry logic** on App side, not Gateway (Gateway keep data until ACK)
4. **Pagination** necessary nếu >240B per response (BLE MTU ~244)
5. **Protocol version** hardcoded v1 (can extend later)

---

## ✅ NEXT STEPS

1. **Review & Confirm** document with user
2. **Start Phase 1**: Create protocol definitions
3. **Create GitHub issue** for each phase
4. **Code review**: Each file reviewed before merge
5. **Testing**: Unit tests for sync logic before integration

