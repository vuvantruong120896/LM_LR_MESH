# Chi Tiết Fix: Queue Feedback + Buffer Management + Deduplication

## 📌 PHẦN 1: Feedback từ Queue

### 🔴 PROBLEM HIỆN TẠI

**File**: `src/components/cellular/src/cellular_firebase_queue.cpp`

```cpp
bool CellularFirebaseQueue::enqueueSensorData(const sensorData& data, int16_t rssi, float snr, uint8_t priority) {
    QueueItem item;
    item.operation = Operation::SensorData;
    item.sensor.data = data;
    item.sensor.rssi = rssi;
    item.sensor.snr = snr;
    item.priority = priority;
    
    return enqueueItem(item);  // ← Returns TRUE nếu item thêm vào queue
                                // ← KHÔNG biết upload result (chưa upload mà!)
}
```

**Gateway side** (`gateway_app.cpp:1060`):
```cpp
if (CellularFirebaseQueue::getInstance().isRunning()) {
    success = CellularFirebaseQueue::getInstance()
        .enqueueSensorData(*s, rssi, snr, 2);  // ← success = enqueue result only!
}

if (success) {
    gatewayState.packetsUploaded++;
    lastProcessedCounter[sourceNode] = s->counter;  // ← MARK AS PROCESSED ❌
}
```

### ✅ SOLUTION 1: Thêm Callback để Báo Kết Quả Upload

**Thêm vào header**:
```cpp
// File: src/components/cellular/include/cellular_firebase_queue.h

class CellularFirebaseQueue {
public:
    // Callback khi upload complete
    using UploadCallback = std::function<void(
        Operation operation,      // Loại operation (Sensor, Status, etc.)
        bool success,             // Upload result (TRUE = success, FALSE = all retries failed)
        const sensorData* data,   // Pointer to data (if SensorData op)
        uint8_t attempts          // Số lần retry đã thử
    )>;
    
    // Đăng ký callback
    void setUploadCallback(UploadCallback callback) {
        m_uploadCallback = callback;
    }

private:
    UploadCallback m_uploadCallback = nullptr;
    
    // ... existing code ...
};
```

**Implement callback trong queue**:
```cpp
// File: src/components/cellular/src/cellular_firebase_queue.cpp

void CellularFirebaseQueue::processItem(QueueItem& item, uint32_t& consecutiveFailures, uint32_t& lastFailureTime) {
    if (!m_client) {
        ESP_LOGE(TAG, "Firebase HTTPS client not set - dropping operation");
        cleanupItem(item);
        
        // Notify failure immediately
        if (m_uploadCallback) {
            m_uploadCallback(item.operation, false, nullptr, 0);
        }
        return;
    }

    bool success = false;
    
    switch (item.operation) {
        case Operation::SensorData: {
            auto result = m_client->uploadSensorData(item.sensor.data,
                                                     static_cast<int8_t>(item.sensor.rssi),
                                                     item.sensor.snr);
            success = result.success;
            if (!success) {
                ESP_LOGW(TAG, "Sensor upload failed: %s", result.message.c_str());
            }
            break;
        }
        // ... other operations ...
    }

    // Update stats and health tracking
    if (success) {
        consecutiveFailures = 0;
        ESP_LOGD(TAG, "✓ Operation completed successfully");
    } else {
        consecutiveFailures++;
        lastFailureTime = millis();
        ESP_LOGW(TAG, "✗ Operation failed (failures: %d)", consecutiveFailures);
    }

    // Handle result (retry logic)
    if (!handleProcessResult(item, success)) {
        // Operation completed (success or max retries exceeded)
        
        // 🔔 NOTIFY GATEWAY WITH ACTUAL RESULT
        if (m_uploadCallback && item.operation == Operation::SensorData) {
            m_uploadCallback(
                item.operation,
                success,           // ← TRUE = upload worked, FALSE = all retries failed
                &item.sensor.data, // ← Pointer to sensor data for reference
                item.attempts      // ← Number of attempts made
            );
        }
        
        cleanupItem(item);
    }
}

bool CellularFirebaseQueue::handleProcessResult(QueueItem& item, bool success) {
    if (success) {
        return false;  // cleanup now
    }

    if (item.attempts >= item.maxRetries) {
        // Max retries reached - final failure
        ESP_LOGE(TAG, "Operation %u failed after %u attempts (FINAL)",
                 static_cast<uint8_t>(item.operation), item.attempts + 1);
        
        // ← Callback already called in processItem() after this returns false
        return false;
    }

    // Retry: create retry item
    QueueItem retryItem = item;
    retryItem.attempts++;
    
    // ... backoff and re-enqueue ...
    
    return true;  // Item re-queued, skip cleanup
}
```

**Gateway side - Register callback**:
```cpp
// File: src/application/app_gateway/gateway_app.cpp (in setupFirebase())

void GatewayApp::setupFirebase() {
    // ... existing Firebase setup ...
    
#ifdef USE_CELLULAR
    bool queueReady = CellularFirebaseQueue::getInstance().initialize(firebaseClient);
    if (queueReady) {
        // ✅ Register callback to get upload results
        CellularFirebaseQueue::getInstance().setUploadCallback(
            [this](CellularFirebaseQueue::Operation op, bool success, 
                   const sensorData* data, uint8_t attempts) {
                this->handleQueueUploadResult(op, success, data, attempts);
            }
        );
        ESP_LOGI(TAG, "✅ Queue callback registered for upload feedback");
    }
#endif
}

// New method to handle queue results
void GatewayApp::handleQueueUploadResult(
    CellularFirebaseQueue::Operation op,
    bool success,
    const sensorData* data,
    uint8_t attempts
) {
    if (op == CellularFirebaseQueue::Operation::SensorData && data != nullptr) {
        if (success) {
            // ✅ Upload SUCCESS - safe to update counter
            ESP_LOGI(TAG, "✅ Queue upload SUCCESS for node 0x%04X counter=%u",
                     data->nodeId, data->counter);
            lastProcessedCounter[data->nodeId] = data->counter;
            gatewayState.packetsUploaded++;
        } else {
            // ❌ Upload FAILED after retries - should buffer
            ESP_LOGW(TAG, "❌ Queue upload FAILED for node 0x%04X after %u attempts",
                     data->nodeId, attempts);
            
            // Buffer to NVS
            char nodeIdStr[16];
            snprintf(nodeIdStr, sizeof(nodeIdStr), "0x%04X", data->nodeId);
            
            if (OfflineDataBuffer::addData(String(nodeIdStr), *data)) {
                ESP_LOGI(TAG, "📦 Data buffered to NVS after queue failure");
            }
            
            gatewayState.uploadErrors++;
        }
    } else if (op == CellularFirebaseQueue::Operation::GatewayStatus) {
        if (!success) {
            ESP_LOGW(TAG, "❌ Gateway status upload failed after %u attempts", attempts);
        }
    }
    // ... handle other operations ...
}
```

---

## 📌 PHẦN 2: Only Remove Buffer AFTER Upload Confirmed

### 🔴 PROBLEM HIỆN TẠI

**File**: `src/application/app_gateway/gateway_app.cpp` (line ~330)

```cpp
// Sync offline buffer to Firebase when online
if (currentTime - lastBufferSync >= BUFFER_SYNC_INTERVAL) {
    uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
    
    if (bufferedCount > 0) {
        for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
            String nodeId;
            sensorData data;
            
            if (OfflineDataBuffer::getOldestData(nodeId, data)) {
                bool success = false;
                
                if (CellularFirebaseQueue::getInstance().isRunning()) {
                    success = CellularFirebaseQueue::getInstance()
                        .enqueueSensorData(data, 0, 0.0f, 2);
                }
                
                if (success) {
                    // ❌ WRONG: Removed ngay khi enqueue, không phải khi upload!
                    OfflineDataBuffer::removeOldest();
                    uploaded++;
                }
            }
        }
    }
    lastBufferSync = currentTime;
}
```

### ✅ SOLUTION 2: Tracking Pending Buffer Items

**Thêm tracking struct**:
```cpp
// File: src/application/app_gateway/gateway_app.h

class GatewayApp {
private:
    // Track buffered items waiting for upload confirmation
    struct PendingBufferItem {
        String nodeId;
        sensorData data;
        uint32_t queuedAtMs;
        uint8_t attempts;
    };
    
    // Map: node address → pending item
    std::map<uint16_t, PendingBufferItem> m_pendingBufferUploads;
    
    // Max time to wait for queue confirmation (15 seconds)
    static constexpr uint32_t PENDING_UPLOAD_TIMEOUT = 15000;
    
    // Process pending uploads
    void processPendingBufferUploads();
};
```

**Implement tracking**:
```cpp
// File: src/application/app_gateway/gateway_app.cpp

void GatewayApp::loop() {
    // ... existing code ...
    
    // Process pending buffer uploads
    if (gatewayState.firebaseConnected) {
        processPendingBufferUploads();  // Check for timeouts, etc.
    }
    
    // ... rest of loop ...
}

void GatewayApp::processPendingBufferUploads() {
    uint32_t currentTime = millis();
    
    // Check for timeouts on pending uploads
    for (auto it = m_pendingBufferUploads.begin(); 
         it != m_pendingBufferUploads.end(); ) {
        
        if (currentTime - it->second.queuedAtMs > PENDING_UPLOAD_TIMEOUT) {
            // Timeout - item still not confirmed
            ESP_LOGW(TAG, "⏱️ Pending buffer upload timeout for node 0x%04X",
                     it->first);
            
            // Re-queue for retry
            if (CellularFirebaseQueue::getInstance().isRunning()) {
                bool requeued = CellularFirebaseQueue::getInstance()
                    .enqueueSensorData(it->second.data, 0, 0.0f, 3);  // Higher priority
                
                if (requeued) {
                    it->second.attempts++;
                    it->second.queuedAtMs = currentTime;  // Reset timer
                    ++it;
                } else {
                    // Failed to re-queue - keep in pending
                    ++it;
                }
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

// Modified handleQueueUploadResult
void GatewayApp::handleQueueUploadResult(
    CellularFirebaseQueue::Operation op,
    bool success,
    const sensorData* data,
    uint8_t attempts
) {
    if (op == CellularFirebaseQueue::Operation::SensorData && data != nullptr) {
        uint16_t nodeId = data->nodeId;
        
        if (success) {
            // ✅ Upload SUCCESS
            ESP_LOGI(TAG, "✅ Queue upload SUCCESS for node 0x%04X counter=%u",
                     nodeId, data->counter);
            
            lastProcessedCounter[nodeId] = data->counter;
            gatewayState.packetsUploaded++;
            
            // 🗑️ ONLY NOW remove from buffer after confirmed success
            if (m_pendingBufferUploads.count(nodeId)) {
                OfflineDataBuffer::removeOldest();
                m_pendingBufferUploads.erase(nodeId);
                ESP_LOGI(TAG, "📤 Removed buffered item from NVS after upload success");
            }
            
        } else {
            // ❌ Upload FAILED after max retries
            ESP_LOGW(TAG, "❌ Queue upload FAILED for node 0x%04X after %u attempts",
                     nodeId, attempts);
            
            // Check if already in pending list
            if (m_pendingBufferUploads.count(nodeId)) {
                auto& pending = m_pendingBufferUploads[nodeId];
                
                // Retry limit exceeded?
                if (pending.attempts >= 3) {
                    ESP_LOGE(TAG, "🚨 Giving up on node 0x%04X after %u total attempts",
                             nodeId, pending.attempts);
                    
                    // Remove from buffer to prevent infinite retry loop
                    OfflineDataBuffer::removeOldest();
                    m_pendingBufferUploads.erase(nodeId);
                    gatewayState.uploadErrors++;
                } else {
                    // Will be retried by timeout handler
                    pending.attempts++;
                }
            }
        }
    }
}
```

**Modified buffer sync logic**:
```cpp
// In loop() - Sync offline buffer
if (currentTime - lastBufferSync >= BUFFER_SYNC_INTERVAL) {
    uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
    
    if (bufferedCount > 0) {
        ESP_LOGI(TAG, "📤 Syncing offline buffer: %u samples pending", bufferedCount);
        
        // Only sync if not already at max pending
        if (m_pendingBufferUploads.size() < 5) {  // Max 5 pending uploads
            
            String nodeId;
            sensorData data;
            
            if (OfflineDataBuffer::getOldestData(nodeId, data)) {
                if (CellularFirebaseQueue::getInstance().isRunning()) {
                    bool queued = CellularFirebaseQueue::getInstance()
                        .enqueueSensorData(data, 0, 0.0f, 2);
                    
                    if (queued) {
                        // ✅ Track as pending (NOT removed yet!)
                        m_pendingBufferUploads[data.nodeId] = {
                            nodeId,
                            data,
                            currentTime,
                            0  // attempts
                        };
                        ESP_LOGI(TAG, "📍 Marked buffered item as PENDING for upload");
                    }
                }
            }
        }
    }
    lastBufferSync = currentTime;
}
```

---

## 📌 PHẦN 3: Firebase Rules Deduplication

### 🔴 PROBLEM: Nếu Duplicate Xảy Ra

**Có 2 copies cùng dữ liệu gửi lên Firebase**:
- Copy 1: Từ real-time mesh
- Copy 2: Từ buffer retry

**Kết quả**: Data point lặp lại trong time-series chart

### ✅ SOLUTION 3: Dedup by Counter + Firebase Rules

**1. Counter Field - Natural Unique ID**

Mỗi sensor data đã có `counter` field (sequence number):
```cpp
struct sensorData {
    uint32_t counter;   // ← 0, 1, 2, 3, ... (incrementing from node)
    uint16_t nodeId;    // ← Node address
    // ...
};
```

**Unique key = `nodeId + counter`** (không thể có 2 data cùng counter từ 1 node)

**2. Firebase Rules - Prevent Overwrite + Detect Duplicate**

**File**: `firebase-rules.json`

```json
{
  "rules": {
    "users": {
      "$uid": {
        "gateways": {
          "$gatewayMAC": {
            "sensor_data": {
              "$nodeId": {
                ".read": "auth.uid == $uid",
                ".write": "auth.uid == $uid",
                
                // Store all sensor readings indexed by counter
                "$counter": {
                  ".validate": "newData.hasChildren(['counter', 'temperature', 'humidity', 'battery', 'timestamp'])",
                  
                  // Allow write only if counter doesn't exist yet
                  ".write": "
                    auth.uid == $uid &&
                    !data.exists() &&  // Only allow NEW entries (counter didn't exist)
                    newData.child('counter').val() == $counter &&
                    newData.child('timestamp').isNumber()
                  ",
                  
                  "counter": {
                    ".validate": "newData.isNumber() && newData.val() >= 0"
                  },
                  "temperature": { ".validate": "newData.isNumber()" },
                  "humidity": { ".validate": "newData.isNumber()" },
                  "battery": { ".validate": "newData.isNumber()" },
                  "timestamp": { ".validate": "newData.isNumber()" }
                }
              }
            },
            
            // Also store latest reading for quick access
            "latest_sensor": {
              "$nodeId": {
                ".read": "auth.uid == $uid",
                ".write": "auth.uid == $uid",
                
                // Can always update latest, but track version
                ".validate": "
                  newData.hasChildren(['counter', 'timestamp']) &&
                  newData.child('counter').isNumber() &&
                  (!data.exists() || newData.child('counter').val() > data.child('counter').val())
                "
              }
            }
          }
        }
      }
    }
  }
}
```

**Key Points**:
- `".write": "!data.exists()"` ← **ONLY accept if counter NOT exist** → Automatic dedup!
- If duplicate sent with same counter:
  - 1st write: ✅ Accepted (`!data.exists()` = true)
  - 2nd write: ❌ Rejected (`!data.exists()` = false, data now exists)
  - Result: Only 1 copy in Firebase!

**3. Client-side Dedup Check (Double-check)**

```cpp
// Before uploading, check if counter already in Firebase
// File: src/components/cellular/src/cellular_firebase_https_client.cpp

bool CellularFirebaseHTTPSClient::uploadSensorData(const sensorData& data, int8_t rssi, float snr) {
    // ... prepare connection ...
    
    // Build Firebase path
    String path = "/users/" + m_userUID + 
                  "/gateways/" + m_gatewayId +
                  "/sensor_data/" + nodeIdStr +
                  "/" + String(data.counter);  // ← Path includes counter
    
    // Path looks like: /users/user123/gateways/MAC123/sensor_data/0x1234/100
    // This means: "Sensor reading from node 0x1234 with counter 100"
    // If counter 100 already exists → Firebase rules reject
    
    String jsonBody = buildSensorDataJSON(data, rssi, snr);
    
    // Attempt upload
    bool result = sendHTTPSRequest("PUT", path, jsonBody);
    
    if (!result) {
        // Could be because:
        // 1. Network error → will retry
        // 2. Duplicate counter → Firebase rejected (rule violation)
        //    This is OK! Means data already uploaded.
        ESP_LOGW(TAG, "Upload returned false - could be duplicate or network error");
        return false;
    }
    
    return true;
}
```

**4. Detection - Monitor Firebase Rejections**

```cpp
// Add to queue stats
struct QueueStats {
    uint32_t duplicateRejections = 0;  // Counter from rules violation
    uint32_t actualFailures = 0;       // Network/other errors
};

// In response handling:
if (response.statusCode == 400) {
    // Firebase rules validation failed (likely duplicate)
    m_stats.duplicateRejections++;
    ESP_LOGW(TAG, "⚠️ Possible duplicate detected (rules rejected)");
    return true;  // Treat as success since data already in DB
} else if (response.statusCode >= 500) {
    // Server error - should retry
    m_stats.actualFailures++;
    return false;
}
```

---

## 🎯 COMBINED FLOW - Setelah Fix

```
┌─────────────────────────────────────────────────────────┐
│ Node sends Sensor Data                                  │
└──────────────────┬──────────────────────────────────────┘
                   │
                   ▼
        ┌──────────────────────┐
        │ Gateway receives     │
        │ (uploadToFirebase)   │
        └──────────────────────┘
                   │
        ┌──────────┴──────────┐
        │                     │
        ▼                     ▼
    ONLINE              OFFLINE/FAIL
    (Firebase)          (NVS Buffer)
        │                     │
        ├─ Enqueue ───────────┤
        │  to Queue           │
        │  (track pending)    │
        │  [NOT remove       │
        │   from buffer yet] │
        │                     │
        ▼                     │
    ┌─────────────────┐      │
    │ Queue Worker    │      │
    │ Retry 3x        │      │
    └────────┬────────┘      │
             │               │
        ┌────┴────┐          │
        │          │         │
        ▼          ▼         │
      SUCCESS   FAIL        │
        │        (all 3x)   │
        │        │          │
        │        └──────────┼─ Callback: FAILED
        │                   │  [Buffer NOT removed]
        │                   │  [Re-queue later]
        ▼                   │
    Callback:              │
    SUCCESS               │
    [Remove from         │
     buffer]             │
    [Mark counter]       │
             │            │
             │            ▼
             │      ┌──────────────┐
             │      │ Buffer Retry │
             │      │ Loop         │
             │      │ (timeout     │
             │      │  handler)    │
             │      └──────────────┘
             │            │
             ▼            ▼
          Firebase Server with Rules
          ┌─────────────────────────┐
          │ Check: Counter exists?  │
          ├─────────────────────────┤
          │ NO  → Accept (1st copy) │ ✅
          │ YES → Reject (duplicate)│ ❌
          └─────────────────────────┘
             │            │
             ▼            ▼
          Database   Logged as
          (1 copy)   "Duplicate"
```

---

## 📋 Summary Cách Fix

| Phần | Thay Đổi | Lợi Ích |
|------|----------|---------|
| **1. Callback** | Queue báo upload result → Gateway biết | Chính xác track success vs enqueue |
| **2. Pending Tracking** | Keep buffer item until confirmed | Không mất data khi upload fail |
| **3. Firebase Rules** | `!data.exists()` rule | Auto-reject duplicate counter |
| **Combined** | Mọi data được try upload, backup NVS, dedup Firebase | Zero data loss + No duplicate |

