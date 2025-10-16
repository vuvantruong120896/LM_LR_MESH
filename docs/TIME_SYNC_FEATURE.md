# Time Synchronization Feature

## Tổng quan

Hệ thống đồng bộ thời gian cho phép các Node nhận thời gian thực tế từ Gateway (có kết nối WiFi/4G) mà không cần truy cập internet trực tiếp.

## Kiến trúc

### 1. Gateway (có WiFi/4G)
- **Sync NTP**: Gateway kết nối với NTP server (`pool.ntp.org`) để lấy thời gian UTC chính xác
- **Internal RTC**: Duy trì thời gian nội bộ dựa trên NTP + millis()
- **Broadcast Time**: Định kỳ phát sóng time sync packet đến tất cả nodes (mỗi 5 phút)
- **Re-sync NTP**: Tự động sync lại với NTP mỗi 1 giờ để đảm bảo độ chính xác

### 2. Node (không có WiFi/4G)
- **Receive Time Sync**: Nhận broadcast từ Gateway
- **Local RTC**: Duy trì thời gian nội bộ sau khi nhận sync
- **Fallback**: Sử dụng millis() nếu chưa nhận được time sync
- **Timestamp**: Gắn Unix timestamp thực tế vào sensor data

## Time Sync Packet Structure

```cpp
struct TimeSyncPacket {
    uint32_t timestamp;      // Unix timestamp (giây từ 1970-01-01)
    uint16_t milliseconds;   // Thành phần milliseconds (0-999)
    uint8_t  flags;          // Bit 0: NTP synced, Bits 1-7: Reserved
    uint8_t  reserved;       // Dự trữ cho tương lai
};
// Tổng: 8 bytes
```

## Luồng hoạt động

### Gateway Boot Sequence:
1. Kết nối WiFi
2. Sync với NTP server
3. Lưu timestamp + boot offset
4. Broadcast time ngay lập tức
5. Định kỳ broadcast mỗi 5 phút
6. Re-sync NTP mỗi 1 giờ

### Node Boot Sequence:
1. Khởi tạo TimeSyncService (mode: Node)
2. Tạo Time Sync Receive Task
3. Chờ broadcast từ Gateway
4. Nhận time → cập nhật local RTC
5. Sử dụng synced time cho sensor data

### Sensor Data Flow:
1. Node tạo sensor data
2. Kiểm tra `TimeSyncService::isTimeSynced()`
   - **Nếu có sync**: `timestamp = TimeSyncService::getCurrentTimestamp()` (Unix time)
   - **Nếu chưa sync**: `timestamp = millis() / 1000` (fallback)
3. Gửi packet đến Gateway
4. Gateway upload lên Firebase với timestamp chính xác

## API Reference

### TimeSyncService

#### Gateway APIs
```cpp
// Khởi tạo (isGateway = true)
bool TimeSyncService::initialize(true);

// Sync với NTP
bool TimeSyncService::syncWithNTP(
    const char* ntpServer = "pool.ntp.org",
    long gmtOffsetSec = 0,           // UTC
    int daylightOffsetSec = 0
);

// Tạo packet để broadcast
void TimeSyncService::createTimeSyncPacket(TimeSyncPacket& packet);
```

#### Node APIs
```cpp
// Khởi tạo (isGateway = false)
bool TimeSyncService::initialize(false);

// Xử lý packet nhận được
void TimeSyncService::processTimeSyncPacket(const TimeSyncPacket& packet);

// Lấy timestamp hiện tại
uint32_t TimeSyncService::getCurrentTimestamp();

// Kiểm tra trạng thái sync
bool TimeSyncService::isTimeSynced();

// Thời gian kể từ lần sync cuối
uint32_t TimeSyncService::getTimeSinceLastSync();
```

### GatewayApp

```cpp
// Setup time sync (trong setup())
void GatewayApp::setupTimeSync();

// Broadcast time đến nodes
void GatewayApp::broadcastTimeSync();
```

### NodeApp

```cpp
// Setup time sync (trong setup())
void NodeApp::setupTimeSync();

// Xử lý time sync packet nhận được
void NodeApp::handleTimeSyncPacket(AppPacket<TimeSyncPacket>* packet);
```

## Configuration

### Gateway Config (gateway_app.cpp)
```cpp
const uint32_t NTP_RESYNC_INTERVAL = 3600000;      // 1 giờ
const uint32_t TIME_BROADCAST_INTERVAL = 300000;   // 5 phút
```

### NTP Server
- Mặc định: `pool.ntp.org`
- Timezone: UTC (GMT+0)
- Có thể thay đổi trong `setupTimeSync()`

## Memory Usage

### Gateway
- TimeSyncService: ~40 bytes (static)
- NTP sync: ~2KB stack (tạm thời)
- Broadcast: 8 bytes/packet

### Node
- TimeSyncService: ~40 bytes (static)
- Time Sync Task: 3KB stack
- Receive buffer: 8 bytes/packet

## Độ chính xác

- **NTP sync**: ±10ms (phụ thuộc network latency)
- **Local RTC drift**: ±1s/hour (phụ thuộc ESP32 clock)
- **Re-sync frequency**: 1 giờ → sai số tối đa ~1-2s

## Error Handling

### Gateway không sync được NTP
- Log error: "❌ NTP sync failed"
- Retry: Mỗi 1 giờ (theo chu kỳ re-sync)
- Fallback: Gateway vẫn hoạt động bình thường, nodes dùng millis()

### Node không nhận được time sync
- Log: "Using fallback timestamp (boot time)"
- Behavior: Sử dụng `millis() / 1000` làm timestamp
- Auto-recover: Khi nhận được broadcast từ Gateway

### Packet loss
- Gateway broadcast mỗi 5 phút → tối đa 5 phút để node sync
- Node có thể miss 1-2 broadcasts mà vẫn duy trì time (dùng local RTC)

## Testing

### Test Gateway NTP Sync
```cpp
// Check logs:
[TimeSync] Syncing time with NTP server: pool.ntp.org
[TimeSync] ✅ NTP sync successful! Current time: ...
[GATEWAY] ✅ NTP sync successful - Gateway time synchronized
```

### Test Time Broadcast
```cpp
// Gateway logs (mỗi 5 phút):
[GATEWAY] ⏰ Periodic time sync broadcast to nodes
[GATEWAY] 📡 Broadcasting time sync to all nodes: 1729094400.123
```

### Test Node Receive
```cpp
// Node logs:
[NodeApp] ⏰ Time synchronized from Gateway 0xE764: 1729094400.123
[NodeApp] Time sync age: 0 seconds
[NodeApp] Using synced timestamp: 1729094400
```

### Test Sensor Data
```json
{
  "counter": 27,
  "temperature": 39.6,
  "humidity": 64.9,
  "battery": 3.96,
  "timestamp": 1729094400  // Unix timestamp thực tế thay vì 306
}
```

## Future Improvements

1. **Timezone support**: Cho phép cấu hình GMT offset từ Firebase
2. **DST handling**: Tự động điều chỉnh daylight saving time
3. **Multi-gateway**: Node chọn gateway với time sync quality tốt nhất
4. **Sync quality metric**: Đánh giá độ chính xác của time sync
5. **Persist last sync**: Lưu timestamp vào NVS để khôi phục sau reboot

## Files Modified

### New Files
- `src/components/lora_mesh_manager/src/services/TimeSyncService.h`
- `src/components/lora_mesh_manager/src/services/TimeSyncService.cpp`

### Modified Files
- `src/application/app_gateway/gateway_app.h` - Added time sync methods
- `src/application/app_gateway/gateway_app.cpp` - NTP sync + broadcast
- `src/application/app_node/node_app.h` - Added time sync receiver
- `src/application/app_node/node_app.cpp` - Handle time sync, use real timestamp

## Commit Message
```
feat(timesync): implement NTP-based time synchronization

Gateway:
- Sync with NTP server on boot and every 1 hour
- Broadcast time to all nodes every 5 minutes
- Maintain internal RTC using NTP + millis()

Node:
- Receive time sync broadcasts from Gateway
- Maintain local RTC after sync
- Use real Unix timestamp in sensor data
- Fallback to millis() if not synced

Benefits:
- Accurate timestamps for sensor data
- No internet required on nodes
- Automatic recovery from packet loss
- Low overhead (8 bytes/packet, 5 min interval)
```
