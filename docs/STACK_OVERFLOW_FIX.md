# Stack Overflow Fix - Gateway Receive Task

## Vấn đề

### Triệu chứng
```
Guru Meditation Error: Core 0/1 panic'ed (Unhandled debug exception).
Debug exception reason: Stack canary watchpoint triggered (Gateway Receive)
```

### Nguyên nhân
1. **Stack size quá nhỏ**: Gateway Receive Task chỉ có **4096 bytes** (4KB)
2. **WiFi TCP connection**: Sử dụng ~2KB stack cho socket operations
3. **Firebase client**: Sử dụng ~2KB stack cho JSON serialization, HTTP requests
4. **Nested function calls**: uploadToFirebase() → firebaseClient->uploadSensorData() → WiFiClient → TCP
5. **Tổng cộng**: ~5-6KB stack usage → **Stack overflow** với 4KB stack

### Khi nào xảy ra?
- Ngay sau khi log `[WiFiClientImpl.h:317] tcpConnect(): Starting socket`
- Khi uploading sensor data từ node lên Firebase
- Xảy ra **rất thường xuyên** vì mỗi packet từ node đều trigger upload

## Giải pháp

### 1. Tăng Stack Size (CRITICAL FIX)
```cpp
// TRƯỚC (4KB - quá nhỏ)
int res = xTaskCreate(
    processGatewayPackets,
    "Gateway Receive Task",
    4096,  // ❌ Stack overflow!
    ...
);

// SAU (8KB - đủ cho WiFi + Firebase)
int res = xTaskCreate(
    processGatewayPackets,
    "Gateway Receive Task",
    8192,  // ✅ Đủ cho tất cả operations
    ...
);
```

### 2. Thêm Stack Monitoring
```cpp
void GatewayApp::processGatewayPackets(void* parameter) {
    // Check stack usage
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Initial stack: %d bytes free", stackHighWaterMark);
    
    // ... process packets ...
    
    // Check after Firebase upload
    stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (stackHighWaterMark < 1024) {
        ESP_LOGW(TAG, "⚠️ Low stack! Only %d bytes free", stackHighWaterMark);
    }
}
```

### 3. Stack Usage Breakdown (8KB Total)
```
Total:                  8192 bytes (8KB)
├─ WiFi TCP:           ~2000 bytes (socket, buffers)
├─ Firebase Client:    ~2000 bytes (JSON, HTTP)
├─ Function calls:     ~1000 bytes (call stack)
├─ Local variables:    ~1000 bytes
└─ Safety margin:      ~2192 bytes (26%)
```

## Kết quả

### Trước khi fix
- ❌ Crash thường xuyên khi upload Firebase
- ❌ "Stack canary watchpoint triggered"
- ❌ Gateway reboot liên tục

### Sau khi fix
- ✅ Stack đủ cho WiFi TCP + Firebase operations
- ✅ Không còn stack overflow
- ✅ Gateway hoạt động ổn định
- ✅ Stack monitoring cảnh báo sớm nếu có vấn đề

## Monitoring & Debug

### Xem stack usage trong log
```
[GATEWAY-TASK] Initial stack high water mark: 7123 bytes free
[GATEWAY-TASK] Stack high water mark after upload: 2456 bytes free
⚠️ [GATEWAY-TASK] Low stack warning! Only 856 bytes free  // Nếu < 1KB
```

### Stack High Water Mark
- **> 2KB free**: ✅ Rất tốt
- **1-2KB free**: ⚠️ Cảnh báo (monitor thêm)
- **< 1KB free**: 🚨 Nguy hiểm (risk của overflow)

## Best Practices

### 1. Task Stack Size Guidelines
- **Simple tasks**: 2048-4096 bytes
- **Network tasks**: 4096-8192 bytes
- **WiFi + Cloud**: **8192+ bytes** (như Gateway Receive Task)
- **Complex processing**: 16384+ bytes

### 2. Stack Monitoring
- Luôn check `uxTaskGetStackHighWaterMark()` sau operations nặng
- Log warning khi free stack < 1KB
- Tăng stack nếu thường xuyên < 2KB

### 3. Tránh Stack Overflow
- ❌ Không tạo large buffers trên stack (char buf[4096])
- ❌ Không deep recursion
- ✅ Dùng heap cho large data (malloc/new)
- ✅ Minimize function call depth
- ✅ Tăng stack size cho network/cloud tasks

## Firmware Info

- **File**: `gateway_app.cpp`
- **Task**: Gateway Receive Task
- **Stack size**: 8192 bytes (tăng từ 4096)
- **Priority**: 2
- **Core**: Auto-assigned by FreeRTOS
- **Build size**: 1,263,949 bytes (80.4% flash)
- **RAM usage**: 47,992 bytes (14.6%)

## Related Issues

1. Stack overflow khi upload routing table → Cùng nguyên nhân
2. WiFi reconnect làm tăng stack usage → Đã fix với 8KB
3. Firebase retry operations → Stack đủ cho 3 retries

## Tham khảo

- [ESP-IDF Task Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)
- [Stack Overflow Debug](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/fatal-errors.html#stack-overflow)
- [FreeRTOS Task Creation](https://www.freertos.org/a00125.html)
