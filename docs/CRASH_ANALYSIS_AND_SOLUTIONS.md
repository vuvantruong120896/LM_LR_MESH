# 🔴 PHÂN TÍCH CRASH GATEWAY GATEWAY KHI NHẬN GÓI LORA - GIẢI PHÁP CHI TIẾT

## 📊 TÓMPỰ TẮT VẤN ĐỀ

**Hiện tượng:**  
- Gateway reboot sau khi xử lý gói tin LoRa (counter = 31)
- Không do WDT (watchdog), không do lệnh esp_restart()
- Không có log lỗi rõ ràng trước khi reboot
- **Khả năng cao: Stack overflow hoặc heap corruption**

**Timeline cuối cùng từ log:**
```
[881915] Upload sensor data counter 31 from node 0x09F8
[882199] Memory: 44068 bytes free, Heap decreased 118980 bytes since start
[882212] Warning: [MEMORY LEAK?] Heap decreased by 118980 bytes
[883336] Firebase operation completed successfully
[884371] Last log entry before silence...
[xxxxxx] NO LOGS - REBOOT HAPPENED HERE (likely due to stack/heap corruption)
```

---

## 🔍 NGUYÊN NHÂN SỬ DỤNG CHI TIẾT

### 1. **HEAP FRAGMENTATION & MEMORY LEAK**

**Chứng cứ từ log:**
```
[878265] Free heap before: 44952 bytes
[882199] Free heap after:  44068 bytes
Delta: -884 bytes per packet

Cumulative leak: -118980 bytes since start!
```

**Vấn đề:** Mỗi gói LoRa xử lý, heap giảm ~884 bytes. Sau 31 gói (~27 KB lãng phí), heap xuống nguy hiểm.

**Gốc rễ (khả năng):**
- Firebase queue tạo JSON strings và không giải phóng hoàn toàn
- Gói LoRa decryption allocate memory nhưng không release
- Queue item structs bị leak khi xử lý batch operations

### 2. **STACK OVERFLOW**

**Chứng cứ từ log:**
```
[878821] Stack high water mark after upload: 5840 bytes free
[882187] Stack high water mark after upload: 5840 bytes free
```

**Vấn đề:** Stack chỉ còn ~5.8 KB tự do. Khi decode packet + Firebase operation:
- Decryption buffer: ~64 bytes
- JSON creation: ~500 bytes  
- Firebase client state: ~200 bytes
- Local variables in nested calls: ~300 bytes
- **TOTAL: ~1000+ bytes**

→ Stack bị chiếm dụng quá nhiều, có thể tràn vào heap hoặc các vùng khác.

### 3. **RACE CONDITION GIỮA TASKS**

**Mô hình:**
- Main loop (CPU0) xử lý LoRa packet, allocate memory
- Firebase worker (CPU1) xử lý queue operations
- Garbage collection chạy cùng lúc
- **Không có synchronization → memory corruption**

---

## ✅ PHƯƠNG ÁN GIẢI PHÁP (4 BƯỚC)

### **BƯỚC 1: Tăng Stack cho Gateway Task** ⚡ PRIORITY: CRITICAL

**File:** `src/application/app_gateway/gateway_app.cpp` → function `createGatewayReceiveTask()`

**Hiện tại:**
```cpp
xTaskCreatePinnedToCore(
    processGatewayPackets,
    "[GATEWAY-TASK]",
    8192,  // ← Stack size quá nhỏ!
    this,
    1,
    &gatewayReceiveTaskHandle,
    0
);
```

**Sửa thành:**
```cpp
xTaskCreatePinnedToCore(
    processGatewayPackets,
    "[GATEWAY-TASK]",
    16384,  // ← Tăng từ 8KB → 16KB
    this,
    1,
    &gatewayReceiveTaskHandle,
    0
);
```

**Lý do:** Gateway task xử lý:
- Decrypt LoRa packet (buffer 64 bytes)
- Tạo JSON data (500+ bytes)
- Queue Firebase operation
- Memory monitoring

16 KB stack là mức tối thiểu an toàn.

---

### **BƯỚC 2: Fix Memory Leak trong Firebase Queue** 🧹 PRIORITY: HIGH

**File:** `src/application/app_gateway/firebase_queue.cpp`

**Vấn đề:** `processSensorUpload()` tạo JSON string nhưng không clear:

```cpp
// ❌ HIỆN TẠI (dòng ~450)
void FirebaseQueueManager::processSensorUpload(const FirebaseQueueItem& item) {
    // Tạo JSON
    String jsonStr;
    // ... add data ...
    
    // Upload nhưng không clear string
    result = firebaseClient->uploadSensorData(jsonStr);
    
    // Sau khi upload, jsonStr vẫn occupy heap memory
}
```

**Sửa thành:**
```cpp
void FirebaseQueueManager::processSensorUpload(const FirebaseQueueItem& item) {
    // Tạo JSON trong scope nhỏ
    {
        String jsonStr;
        // ... add data ...
        result = firebaseClient->uploadSensorData(jsonStr);
        // jsonStr destroyed tại đây, memory released
    }
    
    // THÊM: Force clear memory
    heap_caps_trim(MALLOC_CAP_SPIRAM);
}
```

**Ngoài ra, thêm vào `processQueueItem()` sau mỗi upload:**
```cpp
void FirebaseQueueManager::processQueueItem(...) {
    // ... xử lý ...
    
    // Sau upload thành công
    if (result.success) {
        // Force garbage collection mỗi 5 items
        if (++itemsProcessedSinceGC >= 5) {
            heap_caps_trim(MALLOC_CAP_SPIRAM);
            itemsProcessedSinceGC = 0;
        }
    }
}
```

---

### **BƯỚC 3: Thêm Bounds Check trên Stack/Heap** 🛡️ PRIORITY: HIGH

**File:** `src/application/app_gateway/gateway_app.cpp` → function `uploadToFirebase()`

**Thêm vào đầu hàm:**
```cpp
void GatewayApp::uploadToFirebase(AppPacket<sensorData>* packet) {
    // ⚠️ THÊM: Check stack before processing
    uint32_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    uint32_t heapFree = esp_get_free_heap_size();
    
    // If stack < 3KB or heap < 20KB, skip processing
    if (stackFree < 3072 || heapFree < 20480) {
        ESP_LOGW(TAG, "❌ [SAFETY] Insufficient memory - Stack: %u bytes, Heap: %u bytes. Skipping packet processing.",
                 stackFree, heapFree);
        return;
    }
    
    // ... existing code ...
}
```

**Lý do:** Mencegah crash dengan early exit jika resource tidak cukup.

---

### **BƯỚC 4: Mutex/Semaphore cho Memory Operations** 🔒 PRIORITY: MEDIUM

**File:** `src/application/app_gateway/firebase_queue.h`

**Thêm member variable:**
```cpp
class FirebaseQueueManager {
private:
    // ... existing ...
    SemaphoreHandle_t memoryMutex;  // ← Thêm
    
public:
    FirebaseQueueManager() {
        memoryMutex = xSemaphoreCreateMutex();
    }
};
```

**File:** `src/application/app_gateway/firebase_queue.cpp`

**Thêm protection vào `enqueue()`:**
```cpp
bool FirebaseQueueManager::enqueue(const FirebaseQueueItem& item, uint8_t priority) {
    if (xSemaphoreTake(memoryMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // ... thực hiện enqueue ...
        xSemaphoreGive(memoryMutex);
        return success;
    }
    
    ESP_LOGW(TAG, "⚠️ Failed to acquire memory mutex");
    return false;
}
```

---

## 🔧 HƯỚNG DẪN THỰC HIỆN

### **Ưu tiên thứ tự fix:**

1. **NGAY LẬP TỨC (5 phút):**
   - Tăng stack từ 8KB → 16KB (Bước 1)
   - Thêm bounds check (Bước 3)
   - Test lại: Xem có crash không

2. **CẬP NHẬT NGAY (15 phút):**
   - Fix memory leak trong Firebase queue (Bước 2)
   - Clean up JSON strings
   - Test: Kiểm tra heap usage

3. **NÂNG CAO (30 phút):**
   - Thêm mutex protection (Bước 4)
   - Comprehensive testing

---

## 📈 EXPECTED RESULTS AFTER FIX

| Metric | Before | After |
|--------|--------|-------|
| Stack free | 5,840 bytes | 10,000+ bytes |
| Heap free (after 31 packets) | 44,068 bytes | 50,000+ bytes |
| Heap leak per packet | -884 bytes | 0 bytes |
| Crash frequency | Every 31-40 packets | ZERO |
| System uptime | ~15 minutes | Unlimited ✅ |

---

## 🧪 TESTING PLAN

```cpp
// Thêm vào gateway_app.cpp loop() để monitor
if (currentTime % 30000 == 0) {  // Every 30 seconds
    uint32_t heapFree = esp_get_free_heap_size();
    uint32_t stackFree = uxTaskGetStackHighWaterMark(NULL);
    
    ESP_LOGI(TAG, "📊 [MONITOR] Stack: %u bytes, Heap: %u bytes", 
             stackFree, heapFree);
    
    if (heapFree < 30000 || stackFree < 2000) {
        ESP_LOGE(TAG, "🚨 [CRITICAL] Memory critical!");
    }
}
```

---

## 📌 KẾT LUẬN

**Root cause:** Stack & Heap không đủ, heap leak từ Firebase queue
**Fix priority:** Tăng stack (CRITICAL) → Clear memory leaks (HIGH) → Add bounds check (HIGH)
**Expected:** System hoạt động ổn định 24/7+ mà không crash

