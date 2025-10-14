# Memory Leak Detection & Monitoring

## Vấn đề: Memory Leak vs Stack Overflow

### Dấu hiệu phân biệt

#### Stack Overflow (Fixed)
- ❌ Crash **ngay lập tức** khi upload
- ❌ Lỗi "Stack canary watchpoint triggered"
- ✅ **ĐÃ FIX**: Tăng stack từ 4KB → 8KB

#### Memory Leak (Đang điều tra)
- ⚠️ Gateway chạy **ổn định ban đầu**
- ⚠️ Sau **vài phút/giờ** bắt đầu crash
- ⚠️ Mỗi lần crash, thời gian chạy **ngắn dần**
- ⚠️ Crash khi upload (vì đó là operation tốn RAM nhất)

## Nguyên nhân khả năng cao

### 1. String Fragmentation
```cpp
// Trong uploadSensorData() - tạo nhiều String objects
String nodeIdStr = nodeIdToString(data.nodeId);      // Allocation #1
String jsonData = createSensorDataJson(...);          // Allocation #2
String latestPath = String("nodes/") + nodeIdStr;    // Allocation #3
String timeSeriesPath = String("sensor_data/")...;   // Allocation #4
```

**Vấn đề**: Mỗi lần upload tạo/xóa nhiều String → heap fragmentation → không còn large block → crash khi allocate

### 2. Firebase Client Internal Buffers
- WiFi TCP buffers
- HTTP request/response buffers
- JSON serialization buffers
- Có thể không được giải phóng đúng cách

### 3. Routing Table Vector
```cpp
std::vector<RouteNode> routingTable;
routingTable.reserve(tableSize);
```
Vector tạm thời cho upload - nếu size lớn có thể gây fragmentation

## Giải pháp: Memory Monitoring

### 1. Task-Level Monitoring (Gateway Receive Task)
```cpp
// Track heap usage per packet
uint32_t freeHeapBefore = ESP.getFreeHeap();
// ... process packet ...
uint32_t freeHeapAfter = ESP.getFreeHeap();
int32_t heapDelta = (int32_t)freeHeapAfter - (int32_t)freeHeapBefore;

// Log every 10 packets
if (packetCount % 10 == 0) {
    ESP_LOGI(TAG, "[MEMORY] Packets: %u, Free heap: %u, Delta: %d",
             packetCount, freeHeapAfter, heapDelta);
}
```

### 2. Global Monitoring (Main Loop)
```cpp
// Check every 30 seconds
uint32_t freeHeap = ESP.getFreeHeap();
uint32_t largestBlock = ESP.getMaxAllocHeap();

ESP_LOGI(TAG, "[MEMORY] Free: %u, Largest block: %u, Uptime: %u min",
         freeHeap, largestBlock, uptime);

// Detect fragmentation
if (freeHeap > 50000 && largestBlock < (freeHeap / 2)) {
    ESP_LOGW(TAG, "⚠️ Heap fragmented!");
}
```

## Log Messages để Monitor

### Normal Operation (Healthy)
```
[MEMORY] Free heap: 180000 bytes (min: 175000), Largest block: 110000 bytes, Uptime: 5 min
[MEMORY] Packets: 50, Free heap: 178000 bytes, Delta: -200 bytes
```

### Warning Signs (Possible Leak)
```
⚠️ [MEMORY LEAK?] Heap decreased by 15000 bytes since start!
⚠️ [FRAGMENTATION] Heap fragmented: 80000 bytes free but largest block only 25000 bytes
⚠️ [GATEWAY-TASK] Low stack warning! Only 856 bytes free
```

### Critical (About to Crash)
```
🚨 [CRITICAL] Low heap memory! Only 25000 bytes free!
🚨 [CRITICAL] Low heap in main loop! Only 28000 bytes free!
```

## Thresholds & Actions

### Heap Memory Levels
- **> 100KB**: ✅ Healthy
- **50-100KB**: ⚠️ Monitor closely
- **30-50KB**: 🚨 Warning - leak likely
- **< 30KB**: 💥 Critical - crash imminent

### Stack Levels (8KB total)
- **> 2KB free**: ✅ Safe
- **1-2KB free**: ⚠️ Monitor
- **< 1KB free**: 🚨 Danger

### Fragmentation Ratio
```
Ratio = LargestBlock / FreeHeap

> 0.7:  ✅ Good
0.5-0.7: ⚠️ Some fragmentation
< 0.5:  🚨 Heavy fragmentation
```

## Test Plan

### Phase 1: Short-term test (15 minutes)
1. Upload firmware
2. Monitor logs every minute
3. Check for:
   - Initial free heap (~180KB expected)
   - Heap delta per packet (should be ~0 after GC)
   - Stack high water mark (should stay > 2KB)

### Phase 2: Long-term test (2+ hours)
1. Let gateway run continuously
2. Monitor for:
   - Gradual heap decrease (leak indicator)
   - Increasing fragmentation
   - Time to first crash (if any)
3. Collect logs before crash

### Phase 3: Analysis
```
# Calculate leak rate
LeakRate = (InitialHeap - FinalHeap) / Uptime
Example: (180000 - 50000) / 120min = ~1083 bytes/min

# Estimate time to crash
TimeToCrash = FinalHeap / LeakRate
Example: 50000 / 1083 = ~46 minutes until crash
```

## Expected Logs After Upload

### Startup (First few seconds)
```
[GATEWAY-TASK] Gateway packet processing task started
[GATEWAY-TASK] Initial stack high water mark: 7123 bytes free
[GATEWAY-TASK] Initial free heap: 182456 bytes
[MEMORY] Free heap: 180234 bytes (min: 180234), Largest block: 113688 bytes, Uptime: 0 min
```

### Normal Operation (After 5 minutes)
```
[MEMORY] Free heap: 178456 bytes (min: 175234), Largest block: 110688 bytes, Uptime: 5 min
[MEMORY] Packets: 50, Free heap: 177890 bytes (min: 175234), Delta: -45 bytes
[GATEWAY-TASK] Stack high water mark after upload: 3456 bytes free
```

### If Memory Leak Detected (After 30 minutes)
```
⚠️ [MEMORY LEAK?] Heap decreased by 25000 bytes since start!
[MEMORY] Free heap: 155456 bytes (min: 155456), Largest block: 78234 bytes, Uptime: 30 min
⚠️ [FRAGMENTATION] Heap fragmented: 155456 bytes free but largest block only 78234 bytes
```

### Before Crash (If leak exists)
```
🚨 [CRITICAL] Low heap in main loop! Only 28456 bytes free!
🚨 [CRITICAL] Low heap memory! Only 25234 bytes free!
[MEMORY] Free heap: 25234 bytes (min: 25234), Largest block: 12456 bytes, Uptime: 90 min
Guru Meditation Error... (crash)
```

## Debug Strategy

### If logs show memory leak:
1. **Identify leak source**:
   - Check packet count vs heap decrease correlation
   - Check if leak in sensor upload or routing table upload
   - Monitor largest block for fragmentation

2. **Potential fixes**:
   - Pre-allocate String buffers (avoid repeated allocation)
   - Use `char[]` arrays instead of `String` where possible
   - Call `WiFi.disconnect(); WiFi.reconnect();` periodically to clear WiFi buffers
   - Force GC: `heap_caps_check_integrity_all(true);`

3. **Nuclear option**:
   - Add periodic reboot after X hours: `ESP.restart();`
   - This ensures gateway never runs out of memory

## Firmware Info

- **Build size**: 1,265,625 bytes (80.5% flash) - increased 1,676 bytes for monitoring
- **RAM usage**: 48,008 bytes (14.7%) - increased 16 bytes for static vars
- **Stack size**: 8192 bytes (8KB) for Gateway Receive Task
- **Monitoring interval**: 30 seconds (main loop), per packet (task)

## Next Steps

1. ✅ Upload firmware với memory monitoring
2. ⏳ Observe logs for 2+ hours
3. 📊 Analyze heap trend:
   - Stable → No leak, original issue was stack overflow only ✅
   - Decreasing → Memory leak detected, need to fix 🔧
4. 📝 Report findings với logs
