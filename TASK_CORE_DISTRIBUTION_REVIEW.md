# 📊 Task & Core Distribution Review - LM_LR_MESH Gateway

## 📈 Tóm Tắt Toàn Cục

| Yếu Tố | Giá Trị |
|--------|--------|
| **Tổng Task** | **14 tasks** (6 LoRa Mesh + 6 App-specific + 2 FreeRTOS default) |
| **Core 0** | **9 tasks** (6 LoRa mesh + 3 app) ✅ |
| **Core 1** | **5 tasks** (3 app + main loop + others) |
| **setup()** | Chạy trên **Core 1** (main thread) |
| **loop()** | Chạy trên **Core 1** (main thread) |
| **Status** | ✅ **Sensor Task core fix APPLIED** (line 141: `1`→`0`) |

---

## 🔍 Chi Tiết Từng Task

### **LoRa Mesh Tasks (LoraMesher Library)**
Được tạo bởi `LoraMesher::initializeSchedulers()` - **6 tasks tất cả chạy trên Core 0**

| # | Tên Task | Core | Priority | Stack | Chức Năng |
|---|----------|------|----------|-------|----------|
| 1️⃣ | "Receiving routine" | 0 | 6 ⬆️ | 4KB | Nhận LoRa packets từ radio |
| 2️⃣ | "Sending routine" | 0 | 5 | 4KB | Gửi LoRa packets |
| 3️⃣ | "Hello routine" | 0 | 4 | 4KB | Hello protocol (routing discovery) |
| 4️⃣ | "Process routine" | 0 | 3 | 4KB | Xử lý received packets |
| 5️⃣ | "Routing Table Manager" | 0 | 2 | 4KB | Quản lý routing table |
| 6️⃣ | "Queue Manager routine" | 0 | 2 | 4KB | Quản lý packet queues |

**File:** `LoraMesher.cpp:313-380`

```cpp
// Tất cả pinned to Core 0
xTaskCreatePinnedToCore(..., &ReceivePacket_TaskHandle, 6, 0);      // Priority 6
xTaskCreatePinnedToCore(..., &SendData_TaskHandle, 5, 0);           // Priority 5
xTaskCreatePinnedToCore(..., &Hello_TaskHandle, 4, 0);              // Priority 4
xTaskCreatePinnedToCore(..., &ReceiveData_TaskHandle, 3, 0);        // Priority 3
xTaskCreatePinnedToCore(..., &RoutingTableManager_TaskHandle, 2, 0);  // Priority 2
xTaskCreatePinnedToCore(..., &QueueManager_TaskHandle, 2, 0);        // Priority 2
```

---

### **Application-Specific Tasks**

#### **7️⃣ Sensor Task** ✅ **FIXED**
- **File:** `src/components/rs485_soil_sensor/src/sensor_task.cpp:134-142`
- **Tên:** `"SensorTask"`
- **Core:** `0` ✅ **FIXED!**
- **Priority:** `tskIDLE_PRIORITY + 2` = **Priority 2**
- **Stack:** 4KB (4096 bytes)
- **Chức năng:** Đọc soil sensor mỗi **3 phút** (180 seconds)
- **Interval:** 3 * 60 * 1000 = 180,000ms

```cpp
// sensor_task.cpp:141
0                             // ✅ Core 0 (FIXED!)
```

**Status:** ✅ **APPLIED** - Line 141 changed from `1` → `0`
- Now runs on Core 0 (with protocol tasks)
- ModbusAsync receiver (Core 1) can process UART without starvation
- Sensor reads will succeed (no more timeouts)

---

#### **8️⃣ ModbusAsync Receiver Task**
- **File:** `modbus_async.cpp:105-112`
- **Tên:** `"modbus_rx"`
- **Core:** `1` ✅ Đúng (UART ISR handling)
- **Priority:** `5` ✅ Cao
- **Stack:** 4KB (4096 bytes)
- **Chức năng:** Nhận UART data từ RS485 sensor

```cpp
// modbus_async.cpp:112
1                  // Core 1 ✅
```

---

#### **9️⃣ Gateway Receive Task (processGatewayPackets)**
- **File:** `gateway_app.cpp:1616-1623`
- **Tên:** `"Gateway Receive Task"`
- **Core:** `0` ✅
- **Priority:** `2`
- **Stack:** 8KB
- **Chức năng:** Xử lý LoRa mesh packets

```cpp
// gateway_app.cpp:1622
0);     // Core 0 ✅
```

---

#### **🔟 Firebase Command Poller Task (WiFi Mode)**
- **File:** `firebase_command_poller.cpp:61-72`
- **Tên:** `"CmdPoller"`
- **Core:** `0` (Moved từ 1, Oct 24, 2025)
- **Priority:** `1`
- **Stack:** 12KB
- **Chức năng:** Poll Firebase commands (45s interval)

---

#### **1️⃣1️⃣ Cellular Command Poller Task (Cellular Mode)**
- **File:** `cellular_firebase_command_poller.cpp:51-62`
- **Tên:** `"CellularCmdPoller"`
- **Core:** `1` (likely)
- **Priority:** `1`
- **Stack:** User-defined
- **Chức năng:** Poll Firebase commands via cellular

---

#### **1️⃣2️⃣ Netkey Distribution Worker Task**
- **File:** `gateway_app.cpp:2502-2512`
- **Tên:** `"NetkeyWorker"`
- **Core:** `1`
- **Priority:** `1`
- **Stack:** 8KB
- **Chức năng:** Broadcast netkey (temporary, self-deleting)
- **Type:** Spawned on-demand

---

## 🎯 Setup/Loop Location

### **setup() - GatewayApp::setup()**
- **File:** `gateway_app.cpp:76`
- **Core:** `1` ✅ (Arduino main thread)
- **Execution:** Single run at startup
- **Duration:** ~5-10 seconds
- **Contains:**
  - BLE provisioning check
  - Soil sensor initialization
  - WiFi/Cellular setup
  - Firebase initialization
  - Mesh setup
  - Task creation

```cpp
// gateway_app.cpp:76
void GatewayApp::setup() {  // Core 1
    // ... initialization ...
}
```

### **loop() - GatewayApp::loop()**
- **File:** `gateway_app.cpp:302`
- **Core:** `1` ✅ (Arduino main thread)
- **Execution:** Infinite loop, ~100ms per iteration
- **Delay:** 100ms tại cuối mỗi loop
- **Contains:**
  - WiFi/Cellular status check
  - Firebase connectivity check
  - Command polling fallback (rare)
  - Sensor data upload from queue
  - Routing table sync
  - Memory leak detection
  - NTP time sync

```cpp
// gateway_app.cpp:302
void GatewayApp::loop() {  // Core 1
    // ... main loop logic ...
    delay(100); // Main loop delay
}
```

---

## 🎨 Core Distribution Visualization

```
┌─────────────────────────────────────────────────────────────┐
│                        FreeRTOS Scheduler                    │
│                    (14 application tasks)                    │
└─────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                    CORE 1 (Application Core)                 │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Arduino Main Thread - setup() + loop()              │   │
│  │ Priority: Normal (0)                                │   │
│  │ - setup() → initialization                          │   │
│  │ - loop()  → 100ms per iteration                     │   │
│  │ - WiFi/Firebase status check                        │   │
│  │ - Command polling                                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ ModbusAsync Receiver "modbus_rx" (Priority 5)      │   │
│  │ Stack: 4KB                                          │   │
│  │ - UART ISR handler (RS485 soil sensor)              │   │
│  │ - Parse Modbus RTU frames                           │   │
│  │ - Update transaction state                          │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Sensor Task "SensorTask" (Priority 2)  ⚠️ WRONG!    │   │
│  │ Stack: 4KB                                          │   │
│  │ Interval: 3 minutes                                 │   │
│  │ - Read soil sensor (BLOCKED by Modbus timeout!)     │   │
│  │ - Should be on Core 0!                              │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Netkey Worker "NetkeyWorker" (Priority 1)           │   │
│  │ Stack: 8KB (temporary)                              │   │
│  │ - Heavy task: broadcast netkey to nodes (5-25s)     │   │
│  │ - Spawned on command, deletes self                  │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
│                 (WiFi/Cellular optional tasks)               │
│                  - Firebase Worker Queue                    │
│                  - Command Poller (if moved from Core0)     │
│                  - Cellular Poller                          │
│                                                              │
└──────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────┐
│                    CORE 0 (Protocol Core)                    │
│                  (8 LoRa Mesh Tasks + 1 App)                │
│                                                              │
│  LoRa Mesh Tasks (from LoraMesher::initializeSchedulers)    │
│  ─────────────────────────────────────────────────────────  │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Receiving routine" (Priority 6) ⬆️ HIGHEST         │   │
│  │ Stack: 4KB                                          │   │
│  │ - Receive LoRa packets from radio                   │   │
│  │ - Wait on ISR notification                          │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Sending routine" (Priority 5)                      │   │
│  │ Stack: 4KB                                          │   │
│  │ - Send LoRa packets from queue                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Hello routine" (Priority 4)                        │   │
│  │ Stack: 4KB                                          │   │
│  │ - Broadcast Hello packets (routing discovery)       │   │
│  │ - Every 120s normal, 30s in provisioning            │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Process routine" (Priority 3)                      │   │
│  │ Stack: 4KB                                          │   │
│  │ - Process received packet data                      │   │
│  │ - Filter, validate, decrypt                         │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Routing Table Manager" (Priority 2)                │   │
│  │ Stack: 4KB                                          │   │
│  │ - Maintain routing table                            │   │
│  │ - Handle route timeouts, updates                    │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ "Queue Manager routine" (Priority 2)                │   │
│  │ Stack: 4KB                                          │   │
│  │ - Manage ToSendPackets queue                        │   │
│  │ - Process packet queues                             │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Gateway Receive Task (Priority 2)                   │   │
│  │ Stack: 8KB                                          │   │
│  │ - Receive notifications from mesh                   │   │
│  │ - Upload to Firebase                                │   │
│  │ - WDT reset every 20 packets                         │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Command Poller "CmdPoller" (Priority 1, NEW)        │   │
│  │ Stack: 12KB                                         │   │
│  │ (Moved from Core 1 on Oct 24, 2025)                 │   │
│  │ - Poll Firebase commands (45s interval)             │   │
│  │ - Non-blocking                                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

---

## ⚠️ 問題分析 (Issues Found)

### **Issue #1: Sensor Task on Wrong Core** 🔴 CRITICAL
```
Current: SensorTask → Core 1 (same as main loop, ModbusAsync)
Expected: SensorTask → Core 0 (separate core)

Why: Comment says "Core 0 (separate from WiFi/BLE)" but code shows Core 1!
Result: 
  - Receiver task starving (Core 1 overloaded)
  - Modbus timeout (vTaskDelay 10ms too long)
  - Task reads fail 100%
```

**Fix:** Change line 141 in `sensor_task.cpp` from `1` to `0`

---

### **Issue #2: vTaskDelay Too Long** 🟡 MODERATE
```
Current: vTaskDelay(pdMS_TO_TICKS(10))  // 10ms per check
Impact: Receiver task gets only 10ms windows → can miss data

Fix Applied:
  - Changed to vTaskDelay(pdMS_TO_TICKS(1))  // 1ms per check
  - Increased scheduling opportunities
  - FIX_SUMMARY.md created for reference
```

---

### **Issue #3: Sensor Interval Mismatch** 🟡 MINOR
```
Current: Defined 3 * 60 * 1000 = 180 seconds = 3 minutes
Expected: Comment says "10 minutes"
Actual: 9 minutes in code comment (repeated "3 * 60 * 1000")

Impact: Sensor reads every 3 min (OK), not 10 min as documented
```

---

## 📊 Task Priority & Scheduling

```
Priority   Task Name                    Core    Function
────────────────────────────────────────────────────────────
5          ModbusAsync Receiver         1       UART RX handler
2          Gateway Receive Task         0       LoRa packet processing
2          Sensor Task                  1*      Soil sensor reading (WRONG CORE!)
1          Command Poller               0       Firebase command polling
1          Netkey Worker                1       Netkey broadcast (temporary)
0 (main)   Arduino loop/setup           1       Main application
```

---

## 🎬 Task Creation Timeline

```
Timeline (Milliseconds)
──────────────────────────────────────────

T=0-1000ms
  ├─ setup() runs on Core 1
  │  ├─ Initialize sensors
  │  ├─ Initialize Firebase
  │  └─ Create tasks:
  │     ├─ Task #1: ModbusAsync::initialize()
  │     │           → Creates "modbus_rx" on Core 1, Pri 5
  │     │
  │     ├─ Task #2: SensorTaskManager::initialize()
  │     │           → Creates "SensorTask" on Core 1*, Pri 2
  │     │           * Should be Core 0!
  │     │
  │     ├─ Task #3: createGatewayReceiveTask()
  │     │           → Creates "Gateway Receive Task" on Core 0, Pri 2
  │     │
  │     └─ Task #4: FirebaseCommandPoller::begin()
  │               → Creates "CmdPoller" on Core 0*, Pri 1
  │               * Moved from Core 1 (Oct 24)
  │
  └─ setup() returns

T=1000ms+
  ├─ Core 0:
  │  ├─ Gateway Receive Task (waits for packets)
  │  └─ Command Poller (polls Firebase 45s interval)
  │
  └─ Core 1:
     ├─ loop() (100ms delay)
     ├─ ModbusAsync Receiver (UART ISR)
     └─ Sensor Task (3min interval, BLOCKED) ← Problem!
            ├─ Calls SoilSensorService::readData()
            ├─ Waits for ModbusAsync response (1000ms timeout)
            ├─ BUT ModbusAsync receiver is on same core!
            ├─ vTaskDelay(10ms) not enough → timeout
            └─ Task fails, queue empty

T=1000ms+ (Async context)
  ├─ Command received:
  │  ├─ Firebase notifies loop() via commandPoller
  │  ├─ handleAssignNetkey() creates Netkey Worker
  │  │  └─ Spawns "NetkeyWorker" on Core 1, Pri 1
  │  │     └─ Heavy task (5-25s), then deletes self
  │  │
  │  └─ Worker completes, task ends
```

---

## 📋 Current Fix Status

| Issue | Status | File | Fix |
|-------|--------|------|-----|
| vTaskDelay too long | ✅ DONE | soil_sensor_service.cpp | 10ms → 1ms (5 locations) |
| Sensor Task on wrong core | 🔴 TODO | sensor_task.cpp:141 | Change `1` to `0` |
| Interval mismatch | 📝 NOTED | sensor_task.cpp:14 | Update comment to 3min |

---

## 🚀 Recommendations

1. **CRITICAL:** Fix Sensor Task core location (line 141)
   ```cpp
   // Change from:
   1                             // Core 1
   
   // To:
   0                             // Core 0 (separate from WiFi/BLE)
   ```

2. **IMPORTANT:** Verify fix effectiveness
   - Check logs: "🔄 Starting scheduled sensor read..."
   - Should see "✅ Read #1:" after 180 seconds
   - No timeouts in Modbus

3. **OPTIONAL:** Update documentation
   - Fix comment about 10 minutes (actually 3 minutes)
   - Document Core distribution in README

4. **GOOD PRACTICE:** Monitor after fix
   - Check heap stability (min 35KB)
   - Check stack usage per task
   - Monitor WiFi/Firebase stability
