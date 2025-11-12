# Gateway Sensor Upload Interval - Change Summary

**Date:** November 12, 2025  
**Change:** Reduced sensor upload interval from 10 minutes → 1 minute

---

## 1. Hiện Tại Là Bao Lâu?

### ❌ **CŨ:** 10 phút (600,000 ms)

**File:** `src/application/app_gateway/gateway_config.h`  
**Line 71 (cũ):**
```cpp
#define GATEWAY_SENSOR_INTERVAL        600000  // Gateway sensor reading interval: 10 minutes
```

### ✅ **MỚI:** 1 phút (60,000 ms)

**File:** `src/application/app_gateway/gateway_config.h`  
**Line 71 (mới):**
```cpp
#define GATEWAY_SENSOR_INTERVAL        60000   // Gateway sensor reading interval: 1 minute
```

---

## 2. Cách Hoạt Động

### Flow Upload Sensor:

```cpp
// gateway_app.cpp, loop() function, around line 440-470

// Upload immediately on first connection after boot
if (gatewayState.firebaseConnected && !firstUploadDone) {
    ESP_LOGI(TAG, "📊 Initial Gateway sensor data upload (post-reboot)");
    uploadGatewaySensorData();  // ← Gửi sensor ngay lập tức
    firstUploadDone = true;
}

// Then continue with periodic uploads every GATEWAY_SENSOR_INTERVAL
else if (gatewayState.firebaseConnected &&
    (currentTime - lastSensorUpload >= GATEWAY_SENSOR_INTERVAL)) {  // ← Kiểm tra interval
    
    ESP_LOGI(TAG, "📊 Periodic Gateway sensor data collection");
    uploadGatewaySensorData();  // ← Gửi sensor
    lastSensorUpload = currentTime;
}
```

### Chu Kỳ Gửi:

| Giai Đoạn | Thời Gian | Hành Động |
|-----------|-----------|----------|
| **Boot** | Ngay lập tức | Gửi sensor lần 1 |
| **Lần 2** | +60 giây | Gửi sensor |
| **Lần 3** | +60 giây | Gửi sensor |
| **Lần 4** | +60 giây | Gửi sensor |
| ... | ... | ... |

**Trước đây:** Lần 2 là +600 giây (10 phút)

---

## 3. Những Gì Được Gửi

### Dữ Liệu Gateway Sensor:

```cpp
// uploadGatewaySensorData() function, around line 1880-1920

Firebase endpoint: gateways/{userUID}/{gatewayMAC}/sensor

Data gửi:
{
  "timestamp": <unix_timestamp>,
  "gatewaySignal": <RSSI_dBm>,           // Signal strength of gateway
  "snr": <signal_to_noise_ratio>,         // SNR value
  "uptime": <milliseconds>,               // Gateway uptime
  "freeHeap": <bytes>,                    // Free heap memory
  "meshNodes": <count>,                   // Number of nodes in routing table
  "totalPackets": <count>,                // Total packets received
  "successfulUploads": <count>,           // Successful Firebase uploads
  "failedUploads": <count>                // Failed Firebase uploads
}
```

---

## 4. Các Interval Khác Trong Gateway

| Tính Năng | Interval | File | Ghi Chú |
|-----------|----------|------|---------|
| **Sensor Upload** | **60 giây** (mới) | `gateway_config.h:71` | **VỪA THAY ĐỔI** |
| Routing Table Upload | 300 giây (5 phút) | `gateway_config.h:69` | Backup upload |
| Status Upload | 300 giây (5 phút) | `gateway_config.h:72` | Gateway status |
| NTP Resync | 3,600,000 ms (1 giờ) | `gateway_app.cpp:385` | Time sync |
| Time Broadcast | 300,000 ms (5 phút) | `gateway_app.cpp:386` | Broadcast time to nodes |

---

## 5. Thay Đổi Chi Tiết

### File Thay Đổi:
```
d:\Projects\Lora\LM_LR_MESH\src\application\app_gateway\gateway_config.h
```

### Dòng Thay Đổi:
```
Line 71: 600000 → 60000
```

### Conversion:
- 600,000 ms = 600 seconds = 10 minutes ❌
- 60,000 ms = 60 seconds = 1 minute ✅

---

## 6. Tác Động

### ✅ Lợi Ích:
- 📊 Sensor data real-time hơn (up-to-date mỗi phút)
- 📈 Monitoring tốt hơn
- 🔍 Debug dễ hơn (data không quá cũ)

### ⚠️ Tác Động Tiêu Cực:
- 📡 Upload **10x nhiều hơn** (mỗi phút thay vì mỗi 10 phút)
- 📉 Bandwidth tăng 10x
- 🔋 Pin tiêu hao nhanh hơn
- 💾 Firebase database lớn hơn nhanh hơn
- 💰 Firebase cost tăng (nếu tính theo operations)

### 📊 Ước Tính Dữ Liệu:

**Trước:**
```
1 sensor upload mỗi 10 phút
= 6 uploads/giờ
= 144 uploads/ngày
= ~43 KB/ngày (mỗi upload ~300 bytes)
```

**Sau:**
```
1 sensor upload mỗi 1 phút
= 60 uploads/giờ
= 1440 uploads/ngày
= ~432 KB/ngày (mỗi upload ~300 bytes)
```

---

## 7. Cách Revert (Nếu Cần)

Nếu muốn quay lại 10 phút, thay đổi lại:

```cpp
#define GATEWAY_SENSOR_INTERVAL        600000  // Back to 10 minutes
```

Hoặc thay đổi thành interval khác theo nhu cầu:

| Nhu Cầu | Interval (ms) | Chi Tiết |
|---------|---------------|---------|
| Real-time | 30000 | 30 giây |
| Quick monitoring | 60000 | 1 phút |
| Balanced | 300000 | 5 phút |
| Low bandwidth | 600000 | 10 phút (cũ) |
| Very low | 1800000 | 30 phút |
| Ultra low | 3600000 | 1 giờ |

---

## 8. Build & Upload

Sau khi thay đổi, cần build lại:

```bash
cd d:\Projects\Lora\LM_LR_MESH
pio run -e esp32-gateway -t clean
pio run -e esp32-gateway -t upload
```

---

## 9. Verify Change

### Kiểm Tra Logs:

```
Trước: [📊 Periodic Gateway sensor data collection] → mỗi 10 phút
Sau:  [📊 Periodic Gateway sensor data collection] → mỗi 1 phút
```

**Monitor logs:**
```bash
pio device monitor -e esp32-gateway --baud 115200
```

**Dòng log cần tìm:**
```
[GATEWAY] 📊 Periodic Gateway sensor data collection
```

Nếu thấy dòng này mỗi phút (thay vì 10 phút) → Thay đổi thành công!

---

## 10. Tóm Tắt

| Aspect | Cũ | Mới |
|--------|-----|-----|
| Interval | 10 phút | 1 phút |
| ms | 600,000 | 60,000 |
| Uploads/giờ | 6 | 60 |
| Uploads/ngày | 144 | 1,440 |
| Bandwidth/ngày | 43 KB | 432 KB |
| File | gateway_config.h | gateway_config.h |
| Line | 71 | 71 |

---

## 11. Next Steps

1. ✅ Thay đổi đã hoàn tất (`gateway_config.h` line 71)
2. ⏭️ Build firmware: `pio run -e esp32-gateway`
3. ⏭️ Upload: `pio run -e esp32-gateway -t upload`
4. ⏭️ Monitor logs để verify mỗi phút gửi 1 lần
5. ⏭️ Check Firebase console để xem dữ liệu update mỗi phút
