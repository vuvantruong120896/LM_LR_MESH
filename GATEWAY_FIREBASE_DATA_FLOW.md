# 🌐 Gateway Firebase Data Flow - Complete Architecture

**Date:** November 23, 2025  
**Firmware Version:** LM_LR_MESH_VER_1.5.0  
**Document:** Comprehensive analysis of data upload, offline handling, and synchronization

---

## 📋 Table of Contents

1. [System Architecture Overview](#system-architecture-overview)
2. [Data Upload Flow - Online Mode](#data-upload-flow---online-mode)
3. [Offline Data Buffer System](#offline-data-buffer-system)
4. [Network Disconnection Handling](#network-disconnection-handling)
5. [Network Reconnection & Sync](#network-reconnection--sync)
6. [Queue System Architecture](#queue-system-architecture)
7. [Complete Flow Diagrams](#complete-flow-diagrams)
8. [Code References](#code-references)

---

## 1. System Architecture Overview

### 🏗️ Components

```
┌─────────────────────────────────────────────────────────────────┐
│                    GATEWAY FIRMWARE                             │
│                   (ESP32 Dual-Core)                             │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌──────────────┐    ┌──────────────────┐    ┌─────────────────┐
│ LoRa Mesh    │    │ Network Layer    │    │ Storage Layer   │
│              │    │                  │    │                 │
│ - Receive    │    │ WiFi Mode:       │    │ - NVS Flash     │
│   packets    │    │   WiFiService    │    │ - Buffer 50     │
│ - Parse      │    │                  │    │   samples       │
│   sensor     │    │ Cellular Mode:   │    │ - FIFO queue    │
│   data       │    │   CellularConn   │    │ - Persistent    │
│              │    │   Service        │    │                 │
└──────────────┘    └──────────────────┘    └─────────────────┘
        │                     │                     │
        └─────────────────────┼─────────────────────┘
                              ▼
                   ┌──────────────────────┐
                   │  Firebase Layer      │
                   │                      │
                   │  WiFi:               │
                   │  - FirebaseClient    │
                   │  - Queue Manager     │
                   │                      │
                   │  Cellular:           │
                   │  - HTTPS Client      │
                   │  - Queue Manager     │
                   └──────────────────────┘
                              │
                              ▼
                   ┌──────────────────────┐
                   │  Firebase Realtime   │
                   │  Database (Cloud)    │
                   └──────────────────────┘
```

### 🔀 Two Operating Modes

| Mode | Network | Queue | Upload Method | Buffer |
|------|---------|-------|--------------|--------|
| **WiFi** | WiFiService | FirebaseQueueManager | REST API | OfflineDataBuffer |
| **Cellular** | CellularConnectionService | CellularFirebaseQueue | HTTPS | OfflineDataBuffer |

---

## 2. Data Upload Flow - Online Mode

### 📡 Step-by-Step Process

#### **Step 1: Receive Sensor Data from LoRa Mesh**

**File:** `gateway_app.cpp:1200-1300`

```cpp
// Gateway receives packet from mesh network
void processGatewayPackets(void* parameter) {
    while (true) {
        appPacket* receivedPacket = radio.getReceivedQueue();
        
        // Extract sensor data
        sensorData* s = reinterpret_cast<sensorData*>(receivedPacket->payload);
        int8_t rssi = receivedPacket->rssi;
        float snr = receivedPacket->snr;
        
        // Check duplicate
        if (lastProcessedCounter[sourceNode] == s->counter) {
            ESP_LOGD(TAG, "⚠️ Duplicate - skipping");
            continue;
        }
        
        // Proceed to upload...
    }
}
```

**Key Points:**
- Runs in background task "Gateway Receive Task" (Core 0, Priority 2)
- Extracts: nodeId, counter, sensor data, RSSI, SNR
- Duplicate detection using `lastProcessedCounter` map

---

#### **Step 2: Attempt Firebase Upload**

**Condition Check:**
```cpp
if (firebaseClient && gatewayState.firebaseConnected && !isDuplicate) {
    // Attempt upload
}
```

**Upload Logic - WiFi Mode:**

```cpp
// WiFi: Use queue for non-blocking upload
if (firebaseClient->isQueueRunning()) {
    success = firebaseClient->queueSensorData(*s, rssi, snr, 2); // Priority 2
    ESP_LOGD(TAG, "📤 Queued sensor data");
} else {
    // Fallback to direct upload (rare)
    auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
    success = result.success;
}
```

**Upload Logic - Cellular Mode:**

```cpp
#ifdef USE_CELLULAR
if (CellularFirebaseQueue::getInstance().isRunning()) {
    success = CellularFirebaseQueue::getInstance().enqueueSensorData(*s, rssi, snr, 2);
} else {
    // Direct upload fallback
    auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
    success = result.success;
}
#endif
```

**Queue Depths:**
- **WiFi Mode**: 100 items (FirebaseQueueManager::QUEUE_SIZE)
- **Cellular Mode**: 64 items (CellularFirebaseQueue::kQueueLength)

---

#### **Step 3A: Upload Success**

```cpp
if (success) {
    gatewayState.packetsUploaded++;
    ESP_LOGI(TAG, "✅ Upload queued/successful");
    
    // Update last processed counter (prevent duplicate)
    lastProcessedCounter[sourceNode] = s->counter;
    
    // Force GC after upload
    delay(10);
}
```

**Result:**
- Counter incremented
- Packet marked as processed
- Memory cleanup triggered

---

#### **Step 3B: Upload Failed → Buffer to NVS**

```cpp
else {
    gatewayState.uploadErrors++;
    ESP_LOGW(TAG, "❌ Firebase queue/upload failed");
    ESP_LOGI(TAG, "📦 Buffering data to NVS for later sync...");
    
    if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
        uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
        ESP_LOGI(TAG, "✅ Data buffered (%u/%u samples)", 
                 bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
        
        // Still mark as processed to prevent duplicate buffering
        lastProcessedCounter[sourceNode] = s->counter;
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to buffer - NVS full or error");
        // Don't update counter - allow retry on next packet
    }
}
```

**Failure Scenarios:**
1. Queue full (100/64 items)
2. Network disconnected during enqueue
3. Firebase API error
4. Queue system not initialized

**Buffer Capacity:** 50 samples × ~44 bytes = ~2.2 KB

---

## 3. Offline Data Buffer System

### 💾 NVS-Based Persistent Storage

**File:** `offline_data_buffer.h/cpp`

#### **Architecture:**

```
┌─────────────────────────────────────────────────────────┐
│              NVS Flash (Non-Volatile Storage)           │
│                                                         │
│  Namespace: "offline_buf"                               │
│                                                         │
│  ┌──────────────────────────────────────────────┐     │
│  │ Metadata (4 keys)                            │     │
│  │  - head:    Next write position (0-49)       │     │
│  │  - tail:    Next read position (0-49)        │     │
│  │  - count:   Current buffered count (0-50)    │     │
│  │  - version: Schema version (1)               │     │
│  └──────────────────────────────────────────────┘     │
│                                                         │
│  ┌──────────────────────────────────────────────┐     │
│  │ Data Entries (100 keys)                      │     │
│  │                                              │     │
│  │  For each index 0-49:                        │     │
│  │    - "nid_XX": nodeId string                 │     │
│  │    - "dat_XX": sensorData struct (44 bytes)  │     │
│  └──────────────────────────────────────────────┘     │
│                                                         │
│  Total Storage: ~4.7 KB                                 │
└─────────────────────────────────────────────────────────┘
```

#### **FIFO Circular Buffer Logic:**

```cpp
// Add data (circular buffer)
bool OfflineDataBuffer::addData(const String& nodeId, const sensorData& data) {
    uint16_t head, tail, count;
    nvs_get_u16(nvsHandle, KEY_HEAD, &head);
    nvs_get_u16(nvsHandle, KEY_TAIL, &tail);
    nvs_get_u16(nvsHandle, KEY_COUNT, &count);
    
    // If full, overwrite oldest (move tail forward)
    if (count >= MAX_BUFFER_SIZE) {
        tail = (tail + 1) % MAX_BUFFER_SIZE;
        ESP_LOGW(TAG, "Buffer full - overwriting oldest data");
    } else {
        count++;
    }
    
    // Write data at head position
    String nodeIdKey = String("nid_") + String(head);
    String dataKey = String("dat_") + String(head);
    
    nvs_set_str(nvsHandle, nodeIdKey.c_str(), nodeId.c_str());
    nvs_set_blob(nvsHandle, dataKey.c_str(), &data, sizeof(sensorData));
    
    // Move head forward (circular)
    head = (head + 1) % MAX_BUFFER_SIZE;
    
    // Update metadata
    nvs_set_u16(nvsHandle, KEY_HEAD, head);
    nvs_set_u16(nvsHandle, KEY_TAIL, tail);
    nvs_set_u16(nvsHandle, KEY_COUNT, count);
    nvs_commit(nvsHandle);
    
    return true;
}
```

#### **Buffer Statistics:**

**Typical Scenario:**
- Sensor interval: 9 minutes (540s)
- Buffer capacity: 50 samples
- **Offline window:** 50 × 9 min = **450 minutes (7.5 hours)**

**When buffer fills:**
- Oldest data is overwritten (FIFO)
- Log: `⚠️ Buffer full - overwriting oldest data`
- No data loss during **FIRST 7.5 hours** of offline

---

## 4. Network Disconnection Handling

### ❌ Disconnect Scenarios

#### **Cellular Mode - Disconnect Detection**

**File:** `gateway_app.cpp:1426-1432`

```cpp
case CellularConnectionService::Event::DISCONNECTED:
    ESP_LOGW(TAG, "❌ Cellular DISCONNECTED!");
    gatewayState.cellularConnected = false;
    gatewayState.firebaseConnected = false;
    
    logEventViaQueue("cellular_disconnected", "", "");
    break;
```

**Triggers:**
- SIM card removed
- Network signal lost
- Operator disconnection
- APN configuration error
- Network timeout

**State Changes:**
```cpp
gatewayState.cellularConnected = false;  // Network layer down
gatewayState.firebaseConnected = false;  // Firebase unreachable
```

---

#### **WiFi Mode - Disconnect Detection**

**File:** `wifi_service.cpp` (auto-reconnect logic)

```cpp
// WiFi connection loss
if (WiFi.status() != WL_CONNECTED) {
    gatewayState.wifiConnected = false;
    gatewayState.firebaseConnected = false;
    
    // Auto-reconnect attempt after delay
    reconnectAttempt++;
}
```

**Triggers:**
- WiFi AP offline
- Router reboot
- Network cable unplugged
- WiFi password changed
- Signal too weak

---

### 📦 Offline Behavior

**When Network Disconnected:**

```
┌─────────────────────────────────────────────────────────┐
│  Sensor Data Received from LoRa Mesh                    │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────┐
        │ Check: firebaseConnected?     │
        └───────────────────────────────┘
                        │
                        │ FALSE (offline)
                        ▼
        ┌───────────────────────────────┐
        │ Skip upload attempt           │
        │ Log: "Gateway offline"        │
        └───────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────┐
        │ OfflineDataBuffer::addData()  │
        │ → Store to NVS Flash          │
        └───────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────┐
        │ Log: "📦 Data buffered        │
        │       (X/50 samples)"         │
        └───────────────────────────────┘
```

**Code Reference:**

```cpp
// File: gateway_app.cpp:1290-1300
else if (!isDuplicate) {
    // Not provisioned or offline - buffer data to NVS
    ESP_LOGD(TAG, "📦 Gateway offline - buffering data from node %s", nodeIdStr);
    
    if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
        uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
        ESP_LOGI(TAG, "📦 Data buffered (%u/%u samples)", 
                 bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
        
        // Mark as processed
        lastProcessedCounter[sourceNode] = s->counter;
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to buffer data - NVS full or error");
    }
}
```

**Key Behaviors:**
- ✅ No upload attempts when offline (save power)
- ✅ All data buffered to NVS
- ✅ Counter still incremented (prevent duplicate buffering)
- ✅ Buffer survives ESP32 reboot

---

## 5. Network Reconnection & Sync

### ✅ Reconnection Process

#### **Cellular Reconnection Event**

**File:** `gateway_app.cpp:1410-1424`

```cpp
case CellularConnectionService::Event::CONNECTED:
    ESP_LOGI(TAG, "✅ Cellular CONNECTED! Operator: %s, RSSI: %d dBm",
             cellularService->getOperator().c_str(), rssi);
    
    gatewayState.cellularConnected = true;
    gatewayState.cellularRSSI = rssi;

    // Try to reconnect Firebase
    if (firebaseClient && !gatewayState.firebaseConnected) {
        if (firebaseClient->testConnection()) {
            gatewayState.firebaseConnected = true;
            logEventViaQueue("firebase_reconnected", "", "Cellular reconnected");
        }
    }
    break;
```

**State Changes:**
```cpp
gatewayState.cellularConnected = true;   // Network layer up
gatewayState.firebaseConnected = true;   // Firebase reachable (after test)
```

**Firebase Connection Test:**
```cpp
bool testConnection() {
    // Attempt simple GET request to Firebase
    // Returns true if HTTP 200/204
}
```

---

### 🔄 Automatic Data Synchronization

#### **Sync Trigger in Main Loop**

**File:** `gateway_app.cpp:395-460`

```cpp
void loop() {
    // ... (other tasks)
    
    // Sync offline buffer when online (both WiFi & Cellular modes)
    if (isProvisioned && gatewayState.[wifi/cellular]Connected && firebaseClient) {
        static uint32_t lastBufferSync = 0;
        const uint32_t BUFFER_SYNC_INTERVAL = 60000; // Every 1 minute
        
        if (currentTime - lastBufferSync >= BUFFER_SYNC_INTERVAL) {
            uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
            
            if (bufferedCount > 0) {
                ESP_LOGI(TAG, "📤 Syncing offline buffer: %u samples pending", 
                         bufferedCount);
                
                // Upload up to 1 sample per cycle (avoid blocking)
                const uint16_t MAX_UPLOADS_PER_CYCLE = 1;
                
                for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
                    String nodeId;
                    sensorData data;
                    
                    if (OfflineDataBuffer::getOldestData(nodeId, data)) {
                        bool success = [attempt upload to queue or direct];
                        
                        if (success) {
                            OfflineDataBuffer::removeOldest();
                            uploaded++;
                            bufferedCount--;
                            ESP_LOGD(TAG, "✅ Synced buffered data from %s", nodeId.c_str());
                        } else {
                            ESP_LOGW(TAG, "❌ Failed to sync - retry next cycle");
                            break;
                        }
                    }
                }
                
                lastBufferSync = currentTime;
            }
        }
    }
}
```

**Sync Parameters:**
- **Interval:** Every 60 seconds (1 minute)
- **Rate:** 1 sample per cycle (non-blocking)
- **Total time to sync 50 samples:** 50 minutes

**Why slow sync?**
1. Prevent blocking main loop
2. Avoid overwhelming Firebase API
3. Prevent queue overflow
4. Allow real-time data priority

---

### 📊 Sync Flow Diagram

```
┌──────────────────────────────────────────────────────────┐
│  T+0: Network Reconnects                                 │
│  gatewayState.firebaseConnected = true                   │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│  T+60s: First Sync Cycle                                 │
│  Check: bufferedCount = 50 samples                       │
└──────────────────────────────────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────────┐
        │ Get oldest buffered data          │
        │ OfflineDataBuffer::getOldestData()│
        └───────────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────────┐
        │ Enqueue to Firebase Queue         │
        │ (CellularQueue or WiFiQueue)      │
        └───────────────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────────┐
        │ Success?                          │
        └───────────────────────────────────┘
                │                   │
          YES   │                   │ NO
                ▼                   ▼
    ┌──────────────────┐   ┌──────────────────┐
    │ Remove from NVS  │   │ Keep in buffer   │
    │ bufferedCount--  │   │ Retry next cycle │
    └──────────────────┘   └──────────────────┘
                │
                ▼
┌──────────────────────────────────────────────────────────┐
│  T+120s: Second Sync Cycle                               │
│  bufferedCount = 49 samples                              │
│  Upload 1 more sample...                                 │
└──────────────────────────────────────────────────────────┘
                        │
                       ...
                        │
                        ▼
┌──────────────────────────────────────────────────────────┐
│  T+50 min: All 50 samples synced                         │
│  bufferedCount = 0                                       │
│  Sync complete! ✅                                        │
└──────────────────────────────────────────────────────────┘
```

---

## 6. Queue System Architecture

### 🎯 Two Queue Implementations

#### **A. WiFi Mode - FirebaseQueueManager**

**File:** `firebase_queue.h/cpp`

**Configuration:**
```cpp
static constexpr uint16_t QUEUE_SIZE = 100;
static constexpr uint8_t WORKER_TASK_CORE = 1;      // CPU1
static constexpr uint8_t WORKER_TASK_PRIORITY = 2;
static constexpr uint32_t WORKER_STACK_SIZE = 16384; // 16 KB
```

**Worker Task Loop:**
```cpp
void FirebaseQueueManager::workerTask(void* parameter) {
    while (true) {
        FirebaseQueueItem_t item;
        
        // Wait for queue item
        if (xQueueReceive(m_queue, &item, pdMS_TO_TICKS(QUEUE_TIMEOUT_MS)) == pdTRUE) {
            
            // Acquire operation lock (prevent race with command poller)
            FirebaseOperationCoordinator::tryAcquireOperationLock(30000);
            
            // Process item
            processQueueItem(item);
            
            // Release lock
            FirebaseOperationCoordinator::releaseOperationLock();
            
            // Rate limiting
            vTaskDelay(pdMS_TO_TICKS(MIN_OPERATION_INTERVAL_MS));
        } else {
            // Queue idle - yield
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
```

**Queue Item Structure:**
```cpp
struct FirebaseQueueItem {
    FirebaseOperation_t operation;    // SENSOR, STATUS, ROUTING_TABLE, LOG
    FirebasePriority_t priority;      // LOW, NORMAL, HIGH, URGENT
    uint32_t timestamp;
    uint32_t retryCount;
    
    union {
        sensorData data;
        gatewayStatus status;
        routingTable table;
        logEvent event;
    } payload;
};
```

**Priority Levels:**
1. **URGENT (4):** System errors, reboot notifications
2. **HIGH (3):** Error reports, critical status
3. **NORMAL (2):** Sensor data, routing table ← **Most common**
4. **LOW (1):** Periodic uploads, analytics

---

#### **B. Cellular Mode - CellularFirebaseQueue**

**File:** `cellular_firebase_queue.h/cpp`

**Configuration:**
```cpp
static constexpr uint16_t kQueueLength = 64;
static constexpr uint8_t kWorkerCore = 1;           // CPU1
static constexpr UBaseType_t kWorkerPriority = 3;
static constexpr uint16_t kWorkerStackBytes = 12288; // 12 KB
```

**Worker Task Loop:**
```cpp
void CellularFirebaseQueue::workerTask(void* parameter) {
    while (true) {
        QueueItem item;
        
        if (xQueueReceive(self->m_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (item.operation == Operation::Shutdown) break;
            
            self->processItem(item, consecutiveFailures, lastFailureTime);
            
            // 5 second delay between cellular uploads
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}
```

**Why 5 second delay?**
- Cellular modem needs time between AT commands
- Prevent SIM card overload
- Reduce power consumption
- Ensure stable HTTPS connection

**Queue Item Structure:**
```cpp
struct QueueItem {
    Operation operation;     // SensorData, GatewayStatus, RoutingTable, LogEvent
    uint8_t priority;
    uint8_t attempts;
    uint8_t maxRetries = 3;
    
    union {
        struct { sensorData data; int16_t rssi; float snr; } sensor;
        struct { ... } status;
        struct { std::vector<RouteNode>* table; } routing;
        struct { char eventType[48]; char nodeId[48]; ... } log;
    };
};
```

**Retry Logic:**
```cpp
bool CellularFirebaseQueue::handleProcessResult(QueueItem& item, bool success) {
    if (success) {
        m_stats.succeeded++;
        // Invoke callback
        if (m_uploadCallback && item.operation == Operation::SensorData) {
            m_uploadCallback(item.operation, true, &item.sensor.data, item.attempts + 1);
        }
        return true;
    }
    
    // Failure - retry?
    if (item.attempts >= item.maxRetries) {
        ESP_LOGE(TAG, "Operation failed after %u attempts", item.attempts + 1);
        m_stats.failed++;
        
        // Invoke callback with failure
        if (m_uploadCallback && item.operation == Operation::SensorData) {
            m_uploadCallback(item.operation, false, &item.sensor.data, item.attempts + 1);
        }
        return false;
    }
    
    // Re-queue for retry
    item.attempts++;
    m_stats.retries++;
    vTaskDelay(pdMS_TO_TICKS(250 * item.attempts)); // Exponential backoff
    enqueueItem(item);
    
    return true; // Item re-queued
}
```

**Max Retries:**
- **Sensor data:** 3 retries
- **Periodic checks (CSQ, CREG, FetchCommands):** 1 retry

---

### 🔄 Queue Full Behavior

**WiFi Mode (100 items):**
```cpp
bool FirebaseQueueManager::enqueue(const FirebaseQueueItem_t& item) {
    if (uxQueueSpacesAvailable(m_queue) == 0) {
        // Queue full - check priority
        if (shouldDropLowPriorityItem(item.priority)) {
            ESP_LOGW(TAG, "Queue full, dropping low priority item for high priority");
            dropLowPriorityItem();
        } else {
            ESP_LOGE(TAG, "❌ Queue full, dropping new item (priority %d)", item.priority);
            m_stats.droppedItems++;
            return false;
        }
    }
    
    // Enqueue
    xQueueSend(m_queue, &item, 0);
    return true;
}
```

**Cellular Mode (64 items):**
```cpp
bool CellularFirebaseQueue::enqueueItem(QueueItem item) {
    if (xQueueSend(m_queue, &item, 0) != pdTRUE) {
        m_stats.dropped++;
        ESP_LOGW(TAG, "Queue full - dropping operation %u", 
                 static_cast<uint8_t>(item.operation));
        cleanupItem(item);
        return false;
    }
    
    m_stats.enqueued++;
    return true;
}
```

**Result when queue full:**
- New data **DROPPED** (not buffered to NVS automatically)
- Must rely on buffer sync mechanism later
- Why? Prevent infinite queue growth, OOM errors

---

## 7. Complete Flow Diagrams

### 📤 Complete Upload Flow - Online Mode

```
┌─────────────────────────────────────────────────────────────────┐
│  1. LoRa Mesh Receives Sensor Packet                            │
│     - Source Node: 0xCC64                                        │
│     - Counter: 42                                                │
│     - RSSI: -65 dBm, SNR: 8.5                                    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  2. Gateway Receive Task (Core 0)                               │
│     - Parse packet                                               │
│     - Extract sensorData struct                                  │
│     - Check duplicate: lastProcessedCounter[0xCC64] != 42 ✓      │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
            ┌──────────────────────────────────┐
            │ 3. Check Firebase Connection?    │
            └──────────────────────────────────┘
                    │                  │
              ONLINE│                  │OFFLINE
                    ▼                  ▼
      ┌──────────────────────┐  ┌──────────────────────┐
      │ 4A. Attempt Upload   │  │ 4B. Buffer to NVS    │
      └──────────────────────┘  └──────────────────────┘
                    │                       │
                    ▼                       │
#ifdef USE_CELLULAR              │
      ┌──────────────────────┐              │
      │ CellularQueue        │              │
      │ ::enqueueSensorData()│              │
      └──────────────────────┘              │
                    │                       │
#else                                       │
      ┌──────────────────────┐              │
      │ FirebaseClient       │              │
      │ ::queueSensorData()  │              │
      └──────────────────────┘              │
#endif                                      │
                    │                       │
                    ▼                       │
      ┌──────────────────────┐              │
      │ 5A. Queue Item       │              │
      │ Created (Pri 2)      │              │
      └──────────────────────┘              │
                    │                       │
                    ▼                       │
      ┌──────────────────────┐              │
      │ 6A. Worker Task      │              │
      │ Picks up item        │              │
      │ (CPU1, Pri 2/3)      │              │
      └──────────────────────┘              │
                    │                       │
                    ▼                       │
      ┌──────────────────────┐              │
      │ 7A. HTTPS POST       │              │
      │ to Firebase RTDB     │              │
      │ Path: sensor_data/   │              │
      │       {userUID}/     │              │
      │       {nodeId}/      │              │
      │       {timestamp}    │              │
      └──────────────────────┘              │
                    │                       │
                    ▼                       │
      ┌──────────────────────┐              │
      │ 8A. Response         │              │
      │ HTTP 200 OK          │              │
      └──────────────────────┘              │
                    │                       │
                    ▼                       │
      ┌──────────────────────┐              │
      │ 9A. Success          │              │
      │ Counter++            │              │
      │ lastProcessed[]=42   │              │
      └──────────────────────┘              │
                    │                       │
                    └───────────────────────┘
                                │
                                ▼
                    ┌──────────────────────┐
                    │ 10. Complete         │
                    │ ✅ Data in Firebase  │
                    └──────────────────────┘
```

---

### 🔄 Complete Offline → Online Sync Flow

```
┌────────────────────────────────────────────────────────────┐
│  PHASE 1: OFFLINE PERIOD (e.g., 6 hours)                  │
└────────────────────────────────────────────────────────────┘
                        │
        T+0    ┌────────────────────┐
               │ Remove SIM card    │
               │ Network DOWN       │
               └────────────────────┘
                        │
                        ▼
        T+1m   ┌────────────────────┐
               │ Sensor packet #1   │
               │ → Buffer to NVS    │
               │ bufferedCount = 1  │
               └────────────────────┘
                        │
        T+4m   ┌────────────────────┐
               │ Sensor packet #2   │
               │ → Buffer to NVS    │
               │ bufferedCount = 2  │
               └────────────────────┘
                        │
                       ...
                        │
        T+6h   ┌────────────────────┐
               │ Sensor packet #120 │
               │ → Buffer FULL!     │
               │ bufferedCount = 50 │
               │ Overwrite oldest   │
               └────────────────────┘

┌────────────────────────────────────────────────────────────┐
│  PHASE 2: NETWORK RECONNECTION                             │
└────────────────────────────────────────────────────────────┘
                        │
        T+6h   ┌────────────────────┐
               │ Insert SIM card    │
               │ Cellular reconnect │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ CellularConnection │
               │ Service detects    │
               │ Event::CONNECTED   │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Set state:         │
               │ cellularConnected  │
               │ = true             │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ testConnection()   │
               │ to Firebase        │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Set state:         │
               │ firebaseConnected  │
               │ = true             │
               └────────────────────┘

┌────────────────────────────────────────────────────────────┐
│  PHASE 3: AUTOMATIC DATA SYNC                              │
└────────────────────────────────────────────────────────────┘
                        │
      T+6h+1m  ┌────────────────────┐
               │ Main loop() checks │
               │ lastBufferSync     │
               │ 60s elapsed ✓      │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Check buffered:    │
               │ getBufferedCount() │
               │ = 50 samples       │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Get oldest data    │
               │ (FIFO from NVS)    │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Enqueue to         │
               │ CellularFirebase   │
               │ Queue              │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Worker task        │
               │ uploads to         │
               │ Firebase           │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ Success?           │
               └────────────────────┘
                    │       │
              YES   │       │ NO
                    ▼       ▼
        ┌──────────────┐ ┌─────────────┐
        │ Remove from  │ │ Keep in NVS │
        │ NVS buffer   │ │ Retry later │
        │ count = 49   │ └─────────────┘
        └──────────────┘
                    │
                    ▼
      T+6h+2m  ┌────────────────────┐
               │ Sync cycle 2       │
               │ Upload sample #2   │
               │ buffered = 48      │
               └────────────────────┘
                        │
                       ...
                        │
      T+6h+50m ┌────────────────────┐
               │ Sync cycle 50      │
               │ Upload sample #50  │
               │ buffered = 0 ✅    │
               └────────────────────┘
                        │
                        ▼
               ┌────────────────────┐
               │ SYNC COMPLETE      │
               │ All offline data   │
               │ uploaded!          │
               └────────────────────┘
```

---

## 8. Code References

### 📁 Key Files

| File | Purpose | Key Functions |
|------|---------|--------------|
| `gateway_app.cpp` | Main application | `loop()`, `processGatewayPackets()`, `handleCellularEvent()` |
| `offline_data_buffer.h/cpp` | NVS buffer | `addData()`, `getOldestData()`, `removeOldest()` |
| `cellular_firebase_queue.h/cpp` | Cellular queue | `enqueueSensorData()`, `workerTask()`, `processItem()` |
| `firebase_queue.h/cpp` | WiFi queue | `enqueueSensorData()`, `workerTask()`, `processQueueItem()` |
| `cellular_connection_service.cpp` | Cellular network | `connect()`, `update()`, Event callbacks |
| `wifi_service.cpp` | WiFi network | `connect()`, `update()`, Auto-reconnect |

---

### 🔢 Key Code Locations

**Upload Attempt:**
- Line 1235-1257: Cellular mode upload logic
- Line 1246: WiFi mode queue upload

**Buffering:**
- Line 1277-1285: Buffer on upload failure
- Line 1290-1300: Buffer when offline

**Sync Loop:**
- Line 395-460: Buffer sync in main loop
- Line 400: Get buffered count
- Line 413: Get oldest data
- Line 442: Remove after success

**Network Events:**
- Line 1410-1424: Cellular CONNECTED
- Line 1426-1432: Cellular DISCONNECTED

**Queue Workers:**
- `cellular_firebase_queue.cpp:230-260`: Cellular worker task
- `firebase_queue.cpp:210-320`: WiFi worker task

---

## 📊 Summary Table

| Scenario | Network State | Upload Method | Fallback | Sync Time |
|----------|--------------|---------------|----------|-----------|
| **Normal Operation** | Online | Queue (64/100 items) | Direct upload | Immediate |
| **Queue Full** | Online | Direct upload | DROP new data | N/A |
| **Network Lost** | Offline | None | Buffer to NVS (50) | On reconnect |
| **After Reconnect** | Online | Queue from buffer | N/A | 50 minutes |
| **Buffer Full** | Offline | Overwrite oldest | N/A | Lost data |

---

## ⚠️ Known Limitations

1. **Buffer Capacity:** Only 50 samples (~7.5 hours offline window at 9-min interval)
2. **Sync Rate:** 1 sample/minute (slow, but non-blocking)
3. **Queue Full:** New data DROPPED (not auto-buffered)
4. **No Offline Detection for Queue:** If queue full due to network issue, data dropped without buffer

---

## 🔧 Troubleshooting Guide

### Issue: Data not uploading after reconnect

**Check:**
1. `gatewayState.firebaseConnected == true`
2. `OfflineDataBuffer::getBufferedCount() > 0`
3. Logs: `📤 Syncing offline buffer`
4. Queue not full: `uxQueueSpacesAvailable() > 0`

**Solution:**
- Wait 60 seconds for first sync cycle
- Check Firebase credentials
- Verify queue worker task running

---

### Issue: Buffer full, data lost

**Check:**
1. Offline duration > 7.5 hours
2. Logs: `⚠️ Buffer full - overwriting oldest data`

**Solution:**
- Reduce sensor interval (less frequent data)
- Increase MAX_BUFFER_SIZE (requires more NVS space)
- Implement priority-based buffering

---

## 📝 Conclusion

Gateway firmware implements **3-layer data persistence**:

1. **Queue Layer:** 64-100 items in RAM (fast, volatile)
2. **NVS Buffer Layer:** 50 samples in Flash (slow, persistent)
3. **Firebase Layer:** Unlimited in cloud (permanent)

**Data flow priority:**
1. Try queue upload (if online)
2. Buffer to NVS (if queue full or offline)
3. Sync buffer to Firebase (when reconnected)

**Trade-offs:**
- ✅ Survives short outages (up to 7.5 hours)
- ✅ Non-blocking sync (1 sample/min)
- ✅ Persistent across reboots
- ❌ Limited buffer (50 samples)
- ❌ Slow sync (50 minutes total)
- ❌ No queue auto-flush to buffer

---

**Document Version:** 1.0  
**Last Updated:** November 23, 2025  
**Firmware:** LM_LR_MESH_VER_1.5.0
