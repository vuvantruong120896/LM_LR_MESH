# OFFLINE BUFFER FIX - Loại bỏ Loop và Cải thiện Logic

## 📋 YÊU CẦU

### 1. Gateway - Chỉ lưu NVS khi offline hoàn toàn
- ❌ **TRƯỚC**: Lưu NVS khi upload Firebase thất bại (có thể do lỗi tạm thời)
- ✅ **SAU**: Chỉ lưu NVS khi:
  - Không provisioned, HOẶC
  - Không có WiFi connection, HOẶC
  - Firebase client chưa khởi tạo

### Scenario 2: Node Gateway Lost → Found
```
GIVEN: Node routing table has no gateway (role=GATEWAY)
WHEN: Sensor data generated
THEN: Data buffered to NVS ✅
      No broadcast sent (save power) ✅

GIVEN: Gateway appears in routing table
WHEN: Next sensor cycle
THEN: Send current data ✅

WHEN: Sync interval triggers (every 30s)
THEN: Sync buffered data (max 3/cycle) ✅
      500ms spacing between sends ✅
      Remove after sending ✅
```
- ❌ **TRƯỚC**: Node KHÔNG có cơ chế buffer data vào NVS
- ✅ **SAU**: Node lưu data vào NVS khi không tìm thấy Gateway trong routing table
- Capacity: Tối đa 50 samples (tương tự Gateway)

### 3. Gateway Sync - Fix Loop Issue
- ❌ **TRƯỚC**: Khi sync offline buffer, nếu upload thất bại → add lại vào buffer → loop vô hạn
- ✅ **SAU**: Khi đang sync buffer:
  - Upload thất bại → GIỮ NGUYÊN trong buffer (không remove, không add lại)
  - Upload thành công → Remove khỏi buffer
  - Retry lần sau khi có kết nối tốt hơn

## 🔧 CÁC THAY ĐỔI CẦN THỰC HIỆN

### File 1: `gateway_app.cpp`

#### A. Fix receive sensor data - Chỉ buffer khi offline
```cpp
// TRƯỚC (dòng 663-689):
if (isProvisioned && gatewayState.wifiConnected && firebaseClient) {
    auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
    if (result.success) {
        // Success
    } else {
        // BUFFER DATA - SAI! Lỗi upload không có nghĩa là offline
        OfflineDataBuffer::addData(String(nodeIdStr), *s);
    }
} else {
    // Buffer khi offline - ĐÚNG
    OfflineDataBuffer::addData(String(nodeIdStr), *s);
}

// SAU:
if (isProvisioned && gatewayState.wifiConnected && firebaseClient) {
    auto result = firebaseClient->uploadSensorData(*s, rssi, snr);
    if (result.success) {
        gatewayState.packetsUploaded++;
        led_pattern_message();
    } else {
        gatewayState.uploadErrors++;
        // KHÔNG BUFFER - chỉ log error
        ESP_LOGW(TAG, "❌ Firebase upload failed (will retry on next sample): %s", 
                 result.errorMessage.c_str());
    }
} else {
    // Offline - buffer data
    ESP_LOGD(TAG, "📦 Gateway offline - buffering data from node %s", nodeIdStr);
    if (OfflineDataBuffer::addData(String(nodeIdStr), *s)) {
        uint16_t bufferedCount = OfflineDataBuffer::getBufferedCount();
        ESP_LOGI(TAG, "📦 Data buffered (%u/%u samples)", bufferedCount, OfflineDataBuffer::MAX_BUFFER_SIZE);
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to buffer data - NVS full or error");
    }
}
```

#### B. Fix sync offline buffer - Không add lại khi fail
```cpp
// TRƯỚC (dòng 254-290):
for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
    if (OfflineDataBuffer::getOldestData(nodeId, data)) {
        auto result = firebaseClient->uploadSensorData(data, 0, 0.0f);
        if (result.success) {
            OfflineDataBuffer::removeOldest();  // Remove thành công
        } else {
            break;  // ĐÚNG - stop và retry sau
        }
    }
}

// SAU: GIỮ NGUYÊN logic này - đã đúng!
// Không cần thay đổi gì, logic đã correct:
// - Upload thành công → Remove
// - Upload thất bại → Break (giữ nguyên trong buffer)
```

#### C. Fix gateway sensor upload - Tương tự receive
```cpp
// Áp dụng logic tương tự như receive sensor data (dòng 1360-1392)
```

### File 2: Tạo `node_offline_buffer.h` (NEW)

Node cần cơ chế tương tự Gateway để buffer data khi không có Gateway:

```cpp
#ifndef NODE_OFFLINE_BUFFER_H
#define NODE_OFFLINE_BUFFER_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "../common/mesh_utils.h"

/**
 * @brief Service for Node to buffer sensor data when Gateway unavailable
 * 
 * Similar to Gateway's OfflineDataBuffer, but used by Node devices.
 * Stores data when no gateway is found in routing table.
 * 
 * Capacity: 50 samples × 44 bytes = ~2.2 KB data + ~2.5 KB metadata = ~4.7 KB total
 */
class NodeOfflineBuffer {
public:
    static const uint16_t MAX_BUFFER_SIZE = 50;
    static const uint8_t CURRENT_SCHEMA_VERSION = 1;
    
    static bool initialize();
    static bool addData(const sensorData& data);
    static uint16_t getBufferedCount();
    static bool getOldestData(sensorData& data);
    static bool removeOldest();
    static bool clearAll();
    static bool isFull();
    static void getStats(uint16_t& count, uint16_t& maxSize, uint8_t& percentFull);

private:
    static const char* NVS_NAMESPACE;  // "node_buf" (khác với "offline_buf" của Gateway)
    static const char* KEY_HEAD;
    static const char* KEY_TAIL;
    static const char* KEY_COUNT;
    static const char* KEY_VERSION;
    
    static nvs_handle_t nvsHandle;
    static bool initialized;
    
    static String getDataKey(uint16_t index);
    static uint16_t readUint16(const char* key, uint16_t defaultValue);
    static void writeUint16(const char* key, uint16_t value);
};

#endif
```

### File 3: `node_offline_buffer.cpp` (NEW)

Implementation tương tự `offline_data_buffer.cpp` nhưng đơn giản hơn (không cần nodeId):

### File 4: `node_app.cpp`

#### A. Thêm include và initialize
```cpp
#include "node_offline_buffer.h"

// Trong NodeApp::init()
if (NodeOfflineBuffer::initialize()) {
    uint16_t count, maxSize, percentFull;
    NodeOfflineBuffer::getStats(count, maxSize, percentFull);
    ESP_LOGI(LM_TAG, "📦 Node offline buffer ready: %u/%u samples (%u%% full)", 
             count, maxSize, percentFull);
} else {
    ESP_LOGW(LM_TAG, "⚠️ Failed to initialize node offline buffer");
}
```

#### B. Modify sensor send logic
```cpp
// TRƯỚC (dòng 266-280):
uint16_t dst = findGatewayAddress();
if (dst == 0xFFFF) {
    ESP_LOGW(LM_TAG, "⚠️ No gateway found, using broadcast");
}
// Gửi data...

// SAU:
uint16_t dst = findGatewayAddress();
if (dst == 0xFFFF) {
    // Không có Gateway → Buffer data vào NVS
    ESP_LOGW(LM_TAG, "⚠️ No gateway found - buffering sensor data");
    if (NodeOfflineBuffer::addData(nodePacket->data)) {
        uint16_t count = NodeOfflineBuffer::getBufferedCount();
        ESP_LOGI(LM_TAG, "📦 Data buffered (%u/%u samples)", count, NodeOfflineBuffer::MAX_BUFFER_SIZE);
    } else {
        ESP_LOGW(LM_TAG, "⚠️ Failed to buffer data");
    }
    return;  // KHÔNG GỬI broadcast
}
// Gửi data đến Gateway...
```

#### C. Thêm sync task (optional - nếu muốn tự động gửi lại)
```cpp
// Trong NodeApp::update() hoặc task riêng
if (dst != 0xFFFF) {  // Có Gateway
    uint16_t bufferedCount = NodeOfflineBuffer::getBufferedCount();
    if (bufferedCount > 0) {
        ESP_LOGI(LM_TAG, "📤 Syncing node buffer: %u samples pending", bufferedCount);
        
        // Upload tối đa 5 samples mỗi cycle
        for (uint16_t i = 0; i < 5 && bufferedCount > 0; i++) {
            sensorData data;
            if (NodeOfflineBuffer::getOldestData(data)) {
                // Gửi đến Gateway
                bool sent = sendSensorDataToGateway(dst, &data);
                if (sent) {
                    NodeOfflineBuffer::removeOldest();
                    bufferedCount--;
                } else {
                    break;  // Retry sau
                }
            }
        }
    }
}
```

## 📊 KIỂM TRA

### 1. Gateway Offline Behavior
```
SCENARIO: Gateway mất WiFi
✅ Sensor data → Buffer to NVS
✅ Gateway data → Buffer to NVS
✅ Không upload Firebase

SCENARIO: Gateway có WiFi, Firebase lỗi tạm thời
✅ Sensor data → Thử upload, fail → LOG ERROR (không buffer)
✅ Sample tiếp theo → Thử upload lại
❌ KHÔNG add vào buffer (tránh lãng phí NVS)

SCENARIO: Gateway online trở lại
✅ Sync offline buffer → Upload từng sample
✅ Upload success → Remove from buffer
✅ Upload fail → Giữ trong buffer, retry sau
```

### 2. Node Offline Behavior
```
SCENARIO: Node không tìm thấy Gateway
✅ Sensor data → Buffer to NVS (không gửi broadcast)
✅ Không tốn năng lượng gửi broadcast vô ích

SCENARIO: Node tìm thấy Gateway
✅ Gửi sensor data hiện tại
✅ Sync buffered data (nếu có)
```

## 🎯 LỢI ÍCH

1. **Tiết kiệm NVS**: Chỉ buffer khi thực sự offline, không buffer khi lỗi tạm thời
2. **Không loop**: Sync không add lại data vào buffer
3. **Tiết kiệm năng lượng Node**: Không broadcast khi không có Gateway
4. **Data integrity**: Buffered data được gửi lại khi có kết nối
5. **Predictable behavior**: Logic rõ ràng, dễ debug

## 📝 IMPLEMENTATION NOTES

- Gateway và Node dùng NVS namespace khác nhau ("offline_buf" vs "node_buf")
- Capacity đều là 50 samples (khoảng 8-12 phút offline)
- Schema version = 1 cho cả hai (hỗ trợ migration sau này)
- Circular buffer (FIFO) - data cũ bị ghi đè khi full
