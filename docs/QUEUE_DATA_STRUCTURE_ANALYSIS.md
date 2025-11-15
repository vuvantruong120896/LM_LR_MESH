# Queue Data Structure & Buffering Logic Analysis

## 1️⃣ Cấu Trúc Dữ Liệu Cảm Biến Gửi Vào Queue

### A. Struct `sensorData` (44 bytes)
**File**: `src/application/common/mesh_utils.h`

```cpp
struct sensorData {
    // === Common fields (16 bytes) ===
    DeviceType deviceType;      // 1 byte  - Loại cảm biến (SOIL, ENV, WATER, etc.)
    uint32_t counter;           // 4 bytes - Sequence counter từ node
    float battery;              // 4 bytes - Điện áp pin (V)
    uint32_t timestamp;         // 4 bytes - Unix timestamp (seconds)
    uint16_t nodeId;            // 2 bytes - Node gửi dữ liệu
    
    // === Union (28 bytes max) - Chỉ 1 loại dùng tại 1 thời điểm ===
    union {
        // Soil sensor: 7 tham số (28 bytes)
        struct {
            float soilMoisture;     // 0-100%
            float soilTemperature;  // °C
            float pH;               // [0-14]
            float ec;               // mS/cm (độ dẫn điện)
            float nitrogen;         // mg/kg
            float phosphorus;       // mg/kg
            float potassium;        // mg/kg
        } soil;
        
        // Environment: 4 tham số (16 bytes)
        struct {
            float temperature;      // °C
            float humidity;         // %
            float pressure;         // hPa
            float lightIntensity;   // lux
        } environment;
        
        // Water: 4 tham số (16 bytes)
        struct {
            float waterTemp;        // °C
            float pH;               // [0-14]
            float tds;              // ppm (Dissolved Solids)
            float turbidity;        // NTU
        } water;
        
        // Custom: 8 floats (32 bytes)
        float values[8];
    } data;
};
// Total: 44 bytes
```

### B. Dữ Liệu Gửi Vào Queue
**File**: `src/components/cellular/src/cellular_firebase_queue.cpp:98`

```cpp
bool CellularFirebaseQueue::enqueueSensorData(
    const sensorData& data,  // ← Toàn bộ struct 44 bytes
    int16_t rssi,            // ← Signal strength
    float snr,               // ← Signal-to-noise ratio
    uint8_t priority = 2
)
```

**QueueItem Structure**:
```cpp
struct QueueItem {
    Operation operation;     // SensorData, GatewayStatus, RoutingTable, LogEvent
    uint8_t priority;       // 0-3 (higher = more urgent)
    uint8_t attempts;       // Retry count (0-3)
    uint8_t maxRetries;     // Max 3 retries
    
    struct {
        sensorData data;    // ← **44 bytes binary blob**
        int16_t rssi;       // ← 2 bytes
        float snr;          // ← 4 bytes
    } sensor;               // Total: 50 bytes per queue item
    
    // ... other operation types ...
};
```

### C. Memory in Queue
- **Queue size**: 64 items
- **Per item**: ~50-100 bytes (tùy loại operation)
- **Total queue**: ~3-6 KB RAM

---

## 2️⃣ Logic Khi Upload FAIL - Có Xóa Khỏi Queue Hay Không?

### A. Retry Logic trong Queue (handleProcessResult)
**File**: `src/components/cellular/src/cellular_firebase_queue.cpp:351`

```cpp
bool CellularFirebaseQueue::handleProcessResult(QueueItem& item, bool success) {
    
    // ✅ SUCCESS: Item xóa khỏi queue
    if (success) {
        return false;  // ← Return false = cleanup immediately (xóa item)
    }

    // ❌ FAIL: Kiểm tra retry
    if (item.attempts >= item.maxRetries) {
        // ❌ Đã retry 3 lần rồi → xóa khỏi queue
        ESP_LOGE(TAG, "Operation failed after %u attempts", item.attempts + 1);
        return false;  // ← xóa item
    }

    // ⏳ Retry: Tạo item mới và gửi lại vào queue
    QueueItem retryItem = item;
    retryItem.attempts++;  // Tăng retry count
    
    vTaskDelay(pdMS_TO_TICKS(250 * retryItem.attempts));  // Backoff: 250ms, 500ms, 750ms
    
    if (!enqueueItem(retryItem)) {
        // Fail to queue retry → xóa
        return false;
    }

    return true;  // ← Item được gửi lại vào queue, đặt lại cho next cycle
}
```

**Flow**:
1. **Attempt 1 FAIL** → `attempts=0` → `retryItem.attempts=1` → Enqueue lại với delay 250ms
2. **Attempt 2 FAIL** → `attempts=1` → `retryItem.attempts=2` → Enqueue lại với delay 500ms
3. **Attempt 3 FAIL** → `attempts=2` → `retryItem.attempts=3` → Enqueue lại với delay 750ms
4. **Attempt 4 FAIL** → `attempts=3` → `attempts >= maxRetries` → **XÓA KHỎI QUEUE**

---

## 3️⃣ Logic Khi Queue Fail - Có Gửi Vào NVS Buffer Hay Không?

### ⚠️ VẤN ĐỀ QUAN TRỌNG!

**Data flow trong `gateway_app.cpp`**:

```cpp
void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
    // ... Extract sensor data s ...
    
    if (firebaseClient && gatewayState.firebaseConnected && !isDuplicate) {
        
#ifdef USE_CELLULAR
        // Cellular: Gửi vào queue
        if (CellularFirebaseQueue::getInstance().isRunning()) {
            success = CellularFirebaseQueue::getInstance().enqueueSensorData(*s, rssi, snr, 2);
            // ↑ `success` chỉ = true nếu item được enqueue thành công
            // ↑ Không phản ánh kết quả upload (chưa upload mà!)
        }
#else
        // WiFi: Gửi vào queue
        if (firebaseClient->isQueueRunning()) {
            success = firebaseClient->queueSensorData(*s, rssi, snr, 2);
        }
#endif
        
        if (success) {
            // ← Success = queue accepted (KHÔNG phải upload successful)
            gatewayState.packetsUploaded++;
            lastProcessedCounter[sourceNode] = s->counter;  // ← MARK AS PROCESSED
        } else {
            // ← Fail = queue rejected (NVS buffer backup)
            ESP_LOGW(TAG, "Firebase queue/upload failed");
            
            // ⚠️ BUFFER TO NVS
            if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
                lastProcessedCounter[sourceNode] = s->counter;
            }
        }
    }
}
```

### 🔴 PROBLEM DISCOVERED!

**Scenario khi DUPLICATE SENT:**

1. **Dữ liệu từ Node 0x65E8, counter=100** đến gateway
2. **Enqueue thành công** → `success=true` → Mark counter 100 as processed
3. **Worker task xử lý** → **Upload FAIL** (modem timeout) → Retry 3 lần → **Finally deleted from queue**
4. ❌ **Data KHÔNG được buffer to NVS** (vì enqueue success rồi)
5. **Dữ liệu bị LOST** (không upload, không buffer)

OR:

1. **Dữ liệu counter=100** enqueue thành công
2. **First retry FAIL** → Requeue lại
3. **Second retry SUCCESS** → Worker xóa khỏi queue → Hmm, thực ra nó upload được
4. Bình thường (no duplicate)

---

## 4️⃣ Có Xảy Ra DUPLICATE Data?

### ✅ **CÓ - 2 Scenarios**

### Scenario 1: Buffer Retry Loop
```cpp
// Buffered data sync (loop() function)
if (OfflineDataBuffer::getOldestData(nodeId, data)) {
    success = CellularFirebaseQueue::getInstance()
        .enqueueSensorData(data, 0, 0.0f, 2);
    
    if (success) {
        OfflineDataBuffer::removeOldest();  // ← Remove if queue accept
    }
}
```

**Problem**:
- Data X queued → fails to upload → NOT removed from buffer
- Next sync cycle → getOldestData() → returns **same Data X again**
- Data X gets queued **2nd time**
- If this time upload succeeds → **2 copies in Firebase!**

### Scenario 2: Retry Counter Issue
```cpp
// In handleProcessResult():
retryItem.attempts++;
if (!enqueueItem(retryItem)) {
    // Failed to re-queue → item deleted
    // But if firebaseClient has separate retry logic...
    // Item might be retried AGAIN
}
```

---

## 5️⃣ Summary - Vấn Đề Chính

| Vấn Đề | Status | Ảnh Hưởng |
|--------|--------|-----------|
| **Data loss** (enqueue success nhưng upload fail 3 lần) | 🔴 YES | High - Data bị mất |
| **Duplicate send** (buffer retry loop) | 🟡 MAYBE | Medium - Nhưng có counter check |
| **Double retry** (queue + external retry) | 🟡 MAYBE | Low - depends on logic |

---

## ✅ FIX RECOMMENDATIONS

### 1. **Track Queue Status in Gateway**
```cpp
// In gateway_app.cpp:
bool firebaseClient_uploadSuccess = false;  // Actually track result

// After queue processing (in loop, get feedback from queue)
if (firebaseQueue.getLastOperationResult()) {
    firebaseClient_uploadSuccess = true;
    removeFromBuffer();
} else {
    // Upload still pending/failed - keep in buffer
}
```

### 2. **Use Counter + Timestamp for Dedup**
```cpp
// Firebase already has `counter` field
// Use it to detect duplicates: counter + nodeId = unique
db.ref(`/nodes/${nodeId}/lastCounter`).set(counter);
// Before upload: check if counter already exists
```

### 3. **Explicit Buffer Feedback Loop**
```cpp
// When removing from buffer:
- Only remove AFTER confirmed upload (not just enqueue)
- Not: success = enqueueSensorData()
- But: success = getQueueResultAfterUpload()
```

