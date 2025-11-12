# Cellular Firebase & NTP Fixes

## 🐛 Vấn đề được báo cáo

Từ log gateway chạy cellular (log 38.txt), có 2 lỗi chính:

1. **Firebase HTTPS connection failed**: `No response received`
2. **NTP sync failed**: `Failed to convert modem time`

Gateway chạy WiFi hoạt động bình thường, nhưng cellular mode gặp vấn đề.

## 🔍 Phân tích root cause

### 1. Firebase HTTPS Timeout Issue
- **Vấn đề**: Cellular network có latency cao hơn WiFi
- **Code**: Timeout chỉ 10s trong `cellular_firebase_https_client.cpp` không đủ
- **Impact**: Request gửi thành công nhưng không nhận được response

### 2. NTP Sync Issue  
- **Vấn đề**: Cellular mode chỉ thử sync time từ modem, không có fallback NTP
- **Code**: WiFi mode có full NTP sync, cellular mode thiếu logic này
- **Impact**: Gateway không sync được thời gian, ảnh hưởng đến timestamp

## ✅ Giải pháp đã implement

### Fix 1: Tăng timeout cho Firebase HTTPS (cellular_firebase_https_client.cpp)

```cpp
// Tăng timeout từ 10s lên 30s
while (millis() - readStart < 30000) {  // từ 10000

// Tăng backoff delay cho cellular
uint32_t backoffMs = 200;  // từ 100ms
if (backoffMs < 1000) backoffMs += 200; // từ 500ms

// Tăng receive timeout
int received = m_sslClient->receive(buffer, CHUNK, 2000);  // từ 1500ms

// Tăng settle delay
delay(500);  // từ 300ms
```

**Lý do**: Cellular network cần thời gian xử lý lâu hơn WiFi do:
- Latency cao hơn (thường >200ms vs <50ms WiFi)
- Packet processing phức tạp hơn ở base station
- Connection setup time lâu hơn

### Fix 2: Thêm NTP sync fallback cho cellular mode (gateway_app.cpp)

```cpp
// Nếu modem time sync thất bại, thử NTP over cellular
if (cellularService->syncTimeFromNetwork()) {
    // Success with modem time
} else {
    // FALLBACK: Try NTP sync over cellular network (like WiFi mode)
    if (TimeSyncService::syncWithNTP("pool.ntp.org", 25200, 0)) {
        gatewayState.ntpSynced = true;
        ESP_LOGI(TAG, "✅ Time synchronized via NTP over cellular network");
    }
}
```

**Logic flow**:
1. **First priority**: Sync from cellular modem time (nhanh)
2. **Fallback**: Nếu fail, sync NTP qua cellular connection (như WiFi mode)
3. **Periodic**: Re-sync NTP mỗi giờ như WiFi mode

### Fix 3: Cải thiện SSL client reliability (cellular_ssl_client.cpp)

```cpp
// Tăng URC wait time
const uint32_t urcWaitMs = min<uint32_t>(timeoutMs, 2000); // từ 1500ms

// Tăng processing delay
delay(30);  // từ 20ms

// Tăng TLS handshake delay
delay(1500);  // từ 1200ms

// Tăng backoff cho polling
if (backoffMs < 800) backoffMs += 150;  // từ maxBackoff 500ms
```

## 🧪 Expected results

Sau khi áp dụng fixes:

### Firebase Connection
```
[FB_HTTPS] Testing Firebase HTTPS connection...
[FB_HTTPS] Connecting to kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app:443
[SSL_CLIENT] Opening HTTPS connection to kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app:443
[SSL_CLIENT] ✅ AT+CCHOPEN returned OK, assuming session ID = 0
[FB_HTTPS] Sending GET request to /.json
[SSL_CLIENT] Sending 181 bytes via HTTPS
[SSL_CLIENT] HTTPS send successful
[FB_HTTPS] Received 1024 bytes in 5234 ms  // Success with longer time
[FB_HTTPS] ✅ Firebase HTTPS connection OK
```

### NTP Sync
```
[CELLULAR_CONN] Failed to convert modem time  // Modem time still fails
[GATEWAY] ⚠️ Could not sync time from modem; will try NTP fallback
[TimeSync] Syncing time with NTP server: pool.ntp.org
[TimeSync] ✅ NTP sync successful! Current time: ...
[GATEWAY] ✅ Time synchronized via NTP over cellular network
```

## 📊 Performance impact

### Timeout increases:
- **Firebase request**: 10s → 30s (3x increase)
- **SSL handshake**: 1.2s → 1.5s (+25%)
- **Response processing**: More resilient to network jitter

### Memory impact: 
- **Minimal**: Chỉ tăng timeout values, không allocate thêm memory
- **CPU**: Slightly higher due to longer backoff delays

### Network usage:
- **NTP fallback**: Thêm ~1KB data cho NTP sync nếu modem time fail
- **Overall**: Negligible impact

## ✅ Test checklist

- [ ] Firebase connection successful trên cellular
- [ ] Response data nhận đầy đủ (không bị truncate)
- [ ] NTP sync qua cellular network
- [ ] Time broadcast đến nodes hoạt động
- [ ] Sensor data có timestamp chính xác
- [ ] Performance không bị impact đáng kể

## 🔧 Files modified

1. `src/components/cellular/src/cellular_firebase_https_client.cpp`
   - Increased timeouts and delays for cellular network reliability
   
2. `src/application/app_gateway/gateway_app.cpp`
   - Added NTP fallback when modem time sync fails
   - Unified NTP sync logic between WiFi and cellular modes
   
3. `src/components/cellular/src/cellular_ssl_client.cpp`
   - Enhanced SSL client with longer delays for cellular processing
