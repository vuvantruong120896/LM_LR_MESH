# 🔍 Nguyên Nhân Gốc: performStartupSequence() Thất Bại ở sensorTaskFunction

## Tóm Tắt Vấn Đề

**Hiện tượng:** 
- ✅ `performStartupSequence()` + `performMeasurementTrigger()` **THÀNH CÔNG** khi gọi từ `setup()` trên **Core 1**
- ❌ `readData()` **LUÔN THẤT BẠI** khi gọi từ `sensorTaskFunction()` trên **Core 0**

---

## 🎯 Nguyên Nhân Gốc: SỰ KHÔNG TƯƠNG THÍCH VỀ CORE VÀ ĐỒNG BỘ

### 1. **Kiến Trúc Hiện Tại (Có Vấn Đề)**

```
┌─────────────────────────────────────────────────────┐
│                    CORE 1 (Main)                    │
├─────────────────────────────────────────────────────┤
│ setup()                                             │
│  ├─ SoilSensorService::initialize()                │
│  ├─ performStartupSequence() ✅ SUCCESS             │
│  ├─ performMeasurementTrigger() ✅ SUCCESS          │
│  └─ SensorTaskManager::initialize()                │
│      └─ Tạo task trên CORE 0                      │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                    CORE 0 (Sensor)                  │
├─────────────────────────────────────────────────────┤
│ sensorTaskFunction()                                │
│  └─ SoilSensorService::readData()                  │
│     ├─ readSoilParameters()                        │
│     └─ ModbusAsync requests ❌ TIMEOUT/FAIL        │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│                  CORE 1 (Receiver)                  │
├─────────────────────────────────────────────────────┤
│ ModbusAsync::receiverTask() (Priority 5)            │
│  └─ Xử lý UART RX interrupt                        │
│     └─ parseModbusFrame()                          │
│        └─ updateTransaction() 🔒 MUTEX LOCK        │
└─────────────────────────────────────────────────────┘
```

### 2. **Vấn Đề: Chuyển Đổi Core + Deadlock Mutex**

**Khi gọi từ Core 1 (`setup`):**
- Task `performStartupSequence()` chạy trên **Core 1**
- ModbusAsync receiver task cũng chạy trên **Core 1** (Priority 5, cao hơn main)
- Khi Core 1 bận chờ response, OS cho phép receiver task chạy trên Core 1 → nhận được data ✅

**Khi gọi từ Core 0 (`sensorTaskFunction`):**
- Task `readData()` chạy trên **Core 0** (Priority 2)
- Gửi Modbus request → chờ response (vòng lặp chặn)
- **NHƯNG** ModbusAsync receiver task trên **Core 1** có thể bị chặn bởi:
  - 🔒 **Mutex lock** từ `m_mutex` khi cố cập nhật transaction
  - 📡 **Khác core:** Core 0 chờ data, nhưng Core 1 không có cơ hội chạy
  - ⏱️ **Timeout:** timeout `1000ms` → transaction fail

### 3. **Chi Tiết Kỹ Thuật: Mutex Contention**

**File: `modbus_async.cpp:180-195`**
```cpp
uint32_t ModbusAsync::writeSingleRegisterAsync(...) {
    xSemaphoreTake(m_mutex, portMAX_DELAY);  // 🔒 Lock
    
    uint32_t txId = ++m_transactionIdCounter;
    Transaction& txn = m_transactions[txId];
    // Cập nhật transaction...
    
    xSemaphoreGive(m_mutex);  // 🔓 Unlock
    
    // Gửi frame...
    return txId;
}
```

**File: `modbus_async.cpp:470-480`** (Receiver task)
```cpp
void ModbusAsync::processReceivedFrame() {
    xSemaphoreTake(m_mutex, portMAX_DELAY);  // 🔒 Lock - Receiver task chạy ở đây
    
    // Cập nhật transaction state
    auto& txn = m_transactions[matchedTxId];
    txn.state = Transaction::COMPLETED;
    txn.responseLength = frameLen;
    
    xSemaphoreGive(m_mutex);  // 🔓 Unlock
}
```

---

## 📊 Dòng Thời Gian (Timeline) - Thất Bại

```
T=0ms    | Core 0 (Sensor Task Pri=2)     | Core 1 (Receiver Task Pri=5)
─────────┼────────────────────────────────┼──────────────────────
T=100ms  | readData()                     | 
         | ├─ xQueue.receive()            | 
         | └─ vTaskDelay(10ms) → YIELD    | 
T=110ms  | readSoilParameters()           | ← OS: Switch to Core 1?
         | ├─ sendRequest(REG_MOISTURE)   |   (Không! Receiver task đang chờ data)
         | ├─ while(!isComplete) {        |
         |     vTaskDelay(10ms)  ← CHẶN   |
T=120ms  | }                              | 📡 UART RX: Data đến!
         | ← TIMEOUT! (1000ms vượt)       |    (Nhưng Core 0 không cho Core 1 chạy)
T=1100ms | readSoilParameters() = FALSE   | ← Data mất! Task chết
         |                                |
```

---

## 🔓 Giải Pháp: Thêm `vTaskDelay()` ở Receiver Task

**Problem Root Cause:**
Receiver task trên Core 1 (Priority 5) không được chance chạy vì Core 0 task (Priority 2) đang chặn bằng vòng lặp `while(!isComplete)`.

**Solution:**
Thêm `vTaskDelay()` **nhỏ** trong vòng lặp chờ để cho phép scheduler chuyển task:

```cpp
// File: soil_sensor_service.cpp - readDeviceVersion()
uint32_t startWait = millis();
while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
    vTaskDelay(pdMS_TO_TICKS(5));  // ← Thêm dòng này
    if (millis() - startWait > 1000) return -1;
}
```

**Dòng thời gian - Sửa Chữa:**
```
T=0ms    | Core 0 (Sensor Task Pri=2)     | Core 1 (Receiver Task Pri=5)
─────────┼────────────────────────────────┼──────────────────────
T=100ms  | readData()                     | 
         | ├─ sendRequest(REG_MOISTURE)   | 
         | └─ while(!isComplete) {        | 
T=105ms  |    vTaskDelay(5ms) ← YIELD!    | ← OS: Switch to Core 1 ✅
T=115ms  | }                              | 📡 UART RX: Data đến!
         |                                | ├─ parseFrame()
T=120ms  | isComplete = TRUE ✅           | ├─ updateTransaction()
         | getResult() = SUCCESS          | └─ OK! ✅
         |                                |
```

---

## 📋 Tệp Cần Sửa

1. **`soil_sensor_service.cpp:114-119`** - `readDeviceVersion()`
2. **`soil_sensor_service.cpp:125-153`** - `readSensorID()` (2 lần)
3. **`soil_sensor_service.cpp:200-223`** - `performMeasurementTrigger()`
4. **`soil_sensor_service.cpp:302-320`** - `readSoilParameters()` (lặp lại cho tất cả requests)

---

## ✅ Kết Luận

| Yếu Tố | Chi Tiết |
|--------|----------|
| **Nguyên Nhân Gốc** | Receiver task trên Core 1 (Pri=5) bị chặn vì Core 0 task (Pri=2) chạy vòng lặp chặn mà không yield |
| **Tại Sao `setup()` Hoạt Động** | Main loop chạy trên Core 1, receiver task cũng Core 1 → OS cho phép chuyển task trong cùng core |
| **Tại Sao Task Thất Bại** | Task chạy ở Core 0, receiver ở Core 1 → Os không thể schedule receiver (deadlock mutex/starve) |
| **Giải Pháp** | Thêm `vTaskDelay(5ms)` trong vòng lặp chờ → cho phép scheduler chuyển sang receiver task |
| **Độ Ưu Tiên Fix** | 🔴 **CRITICAL** - Hệ thống không thể đọc sensor từ task |

---

## 🛠️ Fix Pattern

```cpp
// ❌ BEFORE: Không yield, receiver task bị starve
uint32_t startWait = millis();
while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
    if (millis() - startWait > 1000) return -1;
}

// ✅ AFTER: Yield cho phép receiver task chạy
uint32_t startWait = millis();
while (!ModbusAsync::getInstance().isTransactionComplete(txId)) {
    vTaskDelay(pdMS_TO_TICKS(5));  // Yield + check every 5ms
    if (millis() - startWait > 1000) return -1;
}
```
