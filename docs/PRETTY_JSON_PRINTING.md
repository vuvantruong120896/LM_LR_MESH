# Pretty JSON Printing - Firebase Upload Debug

## Tính năng

Mỗi lần upload data lên Firebase, JSON payload sẽ được in ra serial monitor theo format đẹp, dễ đọc.

## Output Examples

### 1. Sensor Data Upload (từ Node)

**Khi nhận được sensor data từ node 0xCC64:**

```
[GATEWAY] ☁️ Uploading sensor data from node 0xCC64 to Firebase
[GATEWAY] 🔢 Counter: 1234, 🌡️ Temp: 28.5°C, 💧 Hum: 65.0%, 🔋 Batt: 3.85V, 📡 NodeID: 0xCC64

[Firebase] Sensor data JSON (node 0xCC64):
{
  "counter": 1234,
  "temperature": 28.5,
  "humidity": 65.0,
  "battery": 3.85,
  "timestamp": 12345,
  "rssi": -45,
  "snr": 8.5
}

[Firebase] Sensor data uploaded for node 0x CC64 (RSSI: -45 dBm, SNR: 8.5)
[GATEWAY] ✅ Upload successful (156 bytes)
```

### 2. Gateway Status Upload

**Khi upload gateway status:**

```
[Firebase] Gateway status JSON:
{
  "connected_nodes": 3,
  "total_packets_received": 1250,
  "total_packets_sent": 85,
  "wifi_connected": true,
  "wifi_rssi": -55,
  "firebase_connected": true,
  "uptime_seconds": 3600,
  "free_heap": 175234,
  "timestamp": 12345
}

[Firebase] Gateway status uploaded (3 nodes, 3600 uptime)
```

### 3. Routing Table Upload

**Khi upload routing table (1 node):**

```
[GATEWAY] 📡 Uploading routing table (1 nodes)

[Firebase] Routing table JSON (pretty print):
{
  "nodes": {
    "0xCC64": {
      "address": "0xCC64",
      "via": "0xCC64",
      "metric": 1,
      "role": 1,
      "rssi": -45,
      "snr": 8.5,
      "last_seen": 12345
    }
  },
  "node_count": 1,
  "updated_at": 12345
}

[Firebase] Routing table uploaded (1 nodes)
[GATEWAY] ✅ Routing table uploaded (245 bytes)
```

**Với nhiều nodes:**

```
[Firebase] Routing table JSON (pretty print):
{
  "nodes": {
    "0xCC64": {
      "address": "0xCC64",
      "via": "0xCC64",
      "metric": 1,
      "role": 1,
      "rssi": -45,
      "snr": 8.5,
      "last_seen": 12345
    },
    "0xAB12": {
      "address": "0xAB12",
      "via": "0xCC64",
      "metric": 2,
      "role": 0,
      "last_seen": 12340
    },
    "0x1234": {
      "address": "0x1234",
      "via": "0x1234",
      "metric": 1,
      "role": 0,
      "rssi": -52,
      "snr": 9.2,
      "last_seen": 12338
    }
  },
  "node_count": 3,
  "updated_at": 12345
}
```

## JSON Field Explanations

### Sensor Data Fields
| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `counter` | uint32 | Packet counter từ node | 1234 |
| `temperature` | float | Nhiệt độ (°C) | 28.5 |
| `humidity` | float | Độ ẩm (%) | 65.0 |
| `battery` | float | Điện áp pin (V) | 3.85 |
| `timestamp` | uint32 | Thời gian upload (seconds) | 12345 |
| `rssi` | int8 | Signal strength (dBm) | -45 |
| `snr` | float | Signal-to-noise ratio (dB) | 8.5 |

### Gateway Status Fields
| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `connected_nodes` | uint16 | Số nodes trong routing table | 3 |
| `total_packets_received` | uint32 | Tổng packets nhận được | 1250 |
| `total_packets_sent` | uint32 | Tổng packets gửi đi | 85 |
| `wifi_connected` | bool | WiFi connection status | true |
| `wifi_rssi` | int8 | WiFi signal strength | -55 |
| `firebase_connected` | bool | Firebase connection status | true |
| `uptime_seconds` | uint32 | Uptime (giây) | 3600 |
| `free_heap` | uint32 | Free heap memory (bytes) | 175234 |
| `timestamp` | uint32 | Thời gian upload | 12345 |

### Routing Table Fields
| Field | Type | Description | Example |
|-------|------|-------------|---------|
| `address` | string | Node address (hex) | "0xCC64" |
| `via` | string | Next hop address | "0xCC64" |
| `metric` | uint8 | Hop count (1=direct) | 1 |
| `role` | uint8 | Node role (0=node, 1=gateway) | 1 |
| `rssi` | int8 | Signal strength (metric==1 only) | -45 |
| `snr` | float | SNR (metric==1 only) | 8.5 |
| `last_seen` | uint32 | Last update timestamp | 12345 |
| `node_count` | int | Total nodes in table | 3 |
| `updated_at` | uint32 | Table update timestamp | 12345 |

## Benefits

### 1. **Easy Debugging**
- Xem chính xác data được gửi lên Firebase
- Verify JSON structure trước khi upload
- Detect data anomalies (null, wrong values, etc.)

### 2. **Development & Testing**
- Check field names và types
- Verify conditional fields (rssi, snr chỉ có khi metric==1)
- Compare local data vs Firebase data

### 3. **Troubleshooting**
- Debug upload failures
- Verify data format matches Firebase Rules
- Check timestamp values
- Verify node addresses

## Usage Tips

### Filter logs in Serial Monitor
- Search `[Firebase]` để xem chỉ Firebase logs
- Search `JSON:` để xem chỉ JSON outputs
- Search `node 0xCC64` để xem data từ node cụ thể

### Disable for Production
Nếu muốn tắt pretty print để tiết kiệm serial bandwidth:

```cpp
// Comment out serializeJsonPretty lines in:
// - createSensorDataJson()
// - createGatewayStatusJson()  
// - createRoutingTableJson()
```

### Performance Impact
- **Minimal**: Chỉ thêm ~50-100ms cho serial output
- **No network impact**: Không ảnh hưởng upload speed
- **No memory impact**: Print directly to Serial, không dùng thêm RAM

## Example Debug Session

```
[190000][I][gateway_app.cpp:312] uploadToFirebase(): [GATEWAY] ☁️ Uploading sensor data from node 0xCC64 to Firebase
[190005][I][gateway_app.cpp:314] uploadToFirebase(): [GATEWAY] 🔢 Counter: 1234, 🌡️ Temp: 28.5°C, 💧 Hum: 65.0%, 🔋 Batt: 3.85V

[Firebase] Sensor data JSON (node 0xCC64):
{
  "counter": 1234,
  "temperature": 28.5,
  "humidity": 65.0,
  "battery": 3.85,
  "timestamp": 190,
  "rssi": 0,
  "snr": 0
}

[190250][I][firebase_client.cpp:123] uploadSensorData(): [Firebase] Sensor data uploaded for node 0xCC64 (RSSI: 0 dBm, SNR: 0.0)
[190255][I][gateway_app.cpp:325] uploadToFirebase(): [GATEWAY] ✅ Upload successful (124 bytes)
```

**Observations from this log:**
- ✅ Temperature: 28.5°C - normal
- ✅ Humidity: 65.0% - normal
- ✅ Battery: 3.85V - healthy
- ⚠️ RSSI: 0 - **Not available** (AppPacket doesn't include signal quality)
- ⚠️ SNR: 0 - **Not available**
- ℹ️ Timestamp: 190 seconds = ~3 minutes uptime

## Build Info

- **Flash**: 1,267,561 bytes (80.6%) - tăng 196 bytes cho pretty print
- **RAM**: 48,008 bytes (14.7%) - không thay đổi
- **Functions modified**:
  - `createSensorDataJson()` - added pretty print
  - `createGatewayStatusJson()` - added pretty print  
  - `createRoutingTableJson()` - already had pretty print

## Related Documents

- `STACK_OVERFLOW_FIX.md` - Stack size increase to 8KB
- `MEMORY_LEAK_DETECTION.md` - Memory monitoring implementation
