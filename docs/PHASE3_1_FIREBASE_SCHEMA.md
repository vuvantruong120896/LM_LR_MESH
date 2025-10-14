# Phase 3.1: Firebase Database Schema Design

## ✅ Status: COMPLETED

**Date**: October 14, 2025  
**Duration**: ~30 minutes  
**Result**: Complete Firebase Realtime Database schema designed

---

## 🎯 Design Goals

1. **Efficient Data Structure**: Minimize payload size, optimize for queries
2. **Scalability**: Support 10-100 nodes without performance degradation
3. **Time-Series Ready**: Easy to query sensor data by time range
4. **Real-Time Updates**: Support Firebase listeners for live dashboard
5. **Security-Friendly**: Structure supports Firebase Security Rules

---

## 📊 Database Schema (Firebase Realtime Database)

### **Root Structure**
```
lora-mesh/
├── gateways/
│   └── {gatewayId}/
│       ├── info/
│       ├── status/
│       └── routing_table/
├── nodes/
│   └── {nodeId}/
│       ├── info/
│       └── latest_data/
├── sensor_data/
│   └── {nodeId}/
│       └── {timestamp}/
└── events/
    └── {timestamp}/
```

---

## 🌐 Gateway Data Structure

### **Path**: `/gateways/{gatewayId}/`

**Gateway ID Format**: `GW_<MAC_Address>` (e.g., `GW_240AC4123456`)

### **1. Gateway Info** `/gateways/{gatewayId}/info`
```json
{
  "mac": "24:0A:C4:12:34:56",
  "ip": "192.168.1.100",
  "firmware_version": "1.0.0",
  "created_at": 1697260800,
  "last_seen": 1697347200
}
```

**Fields**:
- `mac` (string): Gateway MAC address
- `ip` (string): Current IP address
- `firmware_version` (string): Firmware version
- `created_at` (timestamp): First registration timestamp (seconds)
- `last_seen` (timestamp): Last update timestamp (seconds)

---

### **2. Gateway Status** `/gateways/{gatewayId}/status`
```json
{
  "connected_nodes": 5,
  "total_packets_received": 12458,
  "total_packets_sent": 1024,
  "wifi_connected": true,
  "wifi_rssi": -45,
  "firebase_connected": true,
  "uptime_seconds": 86400,
  "free_heap": 245000,
  "timestamp": 1697347200
}
```

**Fields**:
- `connected_nodes` (number): Number of active nodes in mesh
- `total_packets_received` (number): Total packets received since boot
- `total_packets_sent` (number): Total packets sent since boot
- `wifi_connected` (boolean): WiFi connection status
- `wifi_rssi` (number): WiFi signal strength (dBm)
- `firebase_connected` (boolean): Firebase connection status
- `uptime_seconds` (number): Gateway uptime in seconds
- `free_heap` (number): Free heap memory in bytes
- `timestamp` (number): Status update timestamp (seconds)

**Update Frequency**: Every 30 seconds

---

### **3. Routing Table** `/gateways/{gatewayId}/routing_table`
```json
{
  "nodes": {
    "0x1234": {
      "address": "0x1234",
      "via": "0x0000",
      "metric": 1,
      "rssi": -65,
      "snr": 8.5,
      "last_seen": 1697347200
    },
    "0x5678": {
      "address": "0x5678",
      "via": "0x1234",
      "metric": 2,
      "rssi": -75,
      "snr": 6.2,
      "last_seen": 1697347195
    }
  },
  "updated_at": 1697347200
}
```

**Fields**:
- `address` (string): Node address (hex format)
- `via` (string): Next hop address (`0x0000` = direct connection)
- `metric` (number): Hop count to reach node
- `rssi` (number): Received Signal Strength Indicator (dBm)
- `snr` (number): Signal-to-Noise Ratio
- `last_seen` (timestamp): Last packet received from this node

**Update Frequency**: Every 60 seconds OR on routing table change

---

## 📡 Node Data Structure

### **Path**: `/nodes/{nodeId}/`

**Node ID Format**: `0x<4-digit-hex>` (e.g., `0x1234`)

### **1. Node Info** `/nodes/{nodeId}/info`
```json
{
  "address": "0x1234",
  "name": "Temperature Sensor #1",
  "type": "sensor",
  "firmware_version": "1.0.0",
  "created_at": 1697260800,
  "last_seen": 1697347200
}
```

**Fields**:
- `address` (string): Node address (hex format)
- `name` (string): Human-readable node name
- `type` (string): Node type (`sensor`, `relay`, `actuator`)
- `firmware_version` (string): Firmware version
- `created_at` (timestamp): First registration timestamp
- `last_seen` (timestamp): Last data received timestamp

---

### **2. Latest Data** `/nodes/{nodeId}/latest_data`
```json
{
  "counter": 12458,
  "temperature": 25.5,
  "humidity": 60.2,
  "battery": 3.7,
  "timestamp": 1697347200
}
```

**Fields**:
- `counter` (number): Sequence number (for duplicate detection)
- `temperature` (number): Temperature in Celsius
- `humidity` (number): Relative humidity (%)
- `battery` (number): Battery voltage (V)
- `timestamp` (number): Sensor reading timestamp (seconds)

**Update Frequency**: On every sensor data packet received

---

## 📈 Sensor Data (Time-Series)

### **Path**: `/sensor_data/{nodeId}/{timestamp}`

**Purpose**: Store historical sensor data for charts/analytics

**Example**: `/sensor_data/0x1234/1697347200`
```json
{
  "counter": 12458,
  "temperature": 25.5,
  "humidity": 60.2,
  "battery": 3.7,
  "rssi": -65,
  "snr": 8.5
}
```

**Fields**:
- `counter` (number): Sequence number
- `temperature` (number): Temperature in Celsius
- `humidity` (number): Relative humidity (%)
- `battery` (number): Battery voltage (V)
- `rssi` (number): Signal strength at gateway (dBm)
- `snr` (number): Signal-to-Noise Ratio

**Storage Strategy**:
- **Retention**: Keep last 7 days (configurable)
- **Indexing**: Timestamp as key (allows range queries)
- **Cleanup**: Cloud Function deletes data older than 7 days

**Query Examples**:
```javascript
// Get last 24 hours of data
ref.child('sensor_data/0x1234')
   .orderByKey()
   .startAt(String(Date.now()/1000 - 86400))
   .limitToLast(100)
```

---

## 📝 Events Log

### **Path**: `/events/{timestamp}`

**Purpose**: Log important system events for debugging/monitoring

**Example**: `/events/1697347200`
```json
{
  "type": "node_joined",
  "gateway_id": "GW_240AC4123456",
  "node_id": "0x1234",
  "details": {
    "rssi": -65,
    "via": "0x0000"
  },
  "timestamp": 1697347200
}
```

**Event Types**:
- `node_joined`: New node joined mesh network
- `node_left`: Node left mesh network (timeout)
- `gateway_started`: Gateway booted
- `gateway_stopped`: Gateway shutdown
- `wifi_disconnected`: WiFi connection lost
- `wifi_reconnected`: WiFi reconnected
- `firebase_error`: Firebase upload error
- `routing_update`: Routing table changed

**Storage Strategy**:
- **Retention**: Keep last 30 days
- **Indexing**: Timestamp as key
- **Cleanup**: Cloud Function (automatic)

---

## 🔐 Firebase Security Rules

```javascript
{
  "rules": {
    // Gateway data - write only by authenticated gateway
    "gateways": {
      "$gatewayId": {
        ".read": true,
        ".write": "auth != null && auth.uid == $gatewayId"
      }
    },
    
    // Node data - write only by gateway
    "nodes": {
      "$nodeId": {
        ".read": true,
        ".write": "auth != null"
      }
    },
    
    // Sensor data - write only by gateway
    "sensor_data": {
      "$nodeId": {
        ".read": true,
        ".write": "auth != null",
        ".indexOn": [".key"]
      }
    },
    
    // Events - write only by gateway
    "events": {
      ".read": true,
      ".write": "auth != null",
      ".indexOn": [".key"]
    }
  }
}
```

---

## 📊 Data Flow

```
Sensor Node
    ↓ LoRa packet
Gateway (receives sensorData struct)
    ↓ parse & enrich with metadata
FirebaseClient
    ↓ upload 3 locations:
    1. /nodes/{nodeId}/latest_data          ← Dashboard live view
    2. /sensor_data/{nodeId}/{timestamp}    ← Historical data
    3. /gateways/{gatewayId}/status         ← Gateway status
```

---

## 💾 Payload Size Analysis

### **Sensor Data Upload**
```json
{
  "counter": 12458,
  "temperature": 25.5,
  "humidity": 60.2,
  "battery": 3.7,
  "timestamp": 1697347200
}
```
**Size**: ~120 bytes (JSON)

### **Gateway Status Upload**
```json
{
  "connected_nodes": 5,
  "total_packets_received": 12458,
  "wifi_connected": true,
  "wifi_rssi": -45,
  "uptime_seconds": 86400,
  "timestamp": 1697347200
}
```
**Size**: ~180 bytes (JSON)

### **Routing Table Upload** (5 nodes)
```json
{
  "nodes": {
    "0x1234": {...},
    "0x5678": {...},
    ...
  }
}
```
**Size**: ~400 bytes (5 nodes × ~80 bytes/node)

### **Total Bandwidth Estimate**
- Sensor data: 120 bytes × 5 nodes × 12 samples/hour = **7,200 bytes/hour**
- Gateway status: 180 bytes × 120 updates/hour = **21,600 bytes/hour**
- Routing table: 400 bytes × 60 updates/hour = **24,000 bytes/hour**

**Total**: ~52 KB/hour = ~1.25 MB/day = **38 MB/month**

**Firebase Free Tier**: 10 GB/month ✅ **Plenty of headroom!**

---

## 🚀 Optimization Strategies

### **1. Batch Uploads**
Instead of uploading each sensor reading immediately, batch multiple readings:
```cpp
// Buffer up to 10 readings
std::vector<sensorData> buffer;
buffer.push_back(data);

if (buffer.size() >= 10 || timeout) {
    uploadBatch(buffer);
    buffer.clear();
}
```
**Benefit**: Reduce HTTP overhead, fewer connections

---

### **2. Delta Encoding**
Only upload changed fields:
```json
// Previous: {"temperature": 25.5, "humidity": 60.2}
// Current:  {"temperature": 25.5, "humidity": 60.5}
// Upload:   {"humidity": 60.5}  ← Only changed field
```
**Benefit**: Reduce payload size by 50-70%

---

### **3. Compression**
For routing table (large payload), use compression:
```cpp
String json = serializeRoutingTable();
String compressed = gzip(json);
uploadCompressed(compressed);
```
**Benefit**: Reduce bandwidth by 60-80%

---

### **4. Sampling Rate**
Adaptive sampling based on change rate:
```cpp
if (abs(temperature - lastTemperature) > 0.5) {
    upload();  // Significant change
} else if (timeSinceLastUpload > 300) {
    upload();  // Max 5 minutes without update
}
```
**Benefit**: Reduce uploads during stable periods

---

## 📈 Query Examples (Web Dashboard)

### **Get Latest Data for All Nodes**
```javascript
firebase.database().ref('nodes').once('value', (snapshot) => {
  snapshot.forEach((node) => {
    const latestData = node.child('latest_data').val();
    console.log(latestData);
  });
});
```

### **Get Last 24 Hours of Sensor Data**
```javascript
const yesterday = Math.floor(Date.now() / 1000) - 86400;
firebase.database()
  .ref('sensor_data/0x1234')
  .orderByKey()
  .startAt(String(yesterday))
  .once('value', (snapshot) => {
    // Plot chart
  });
```

### **Real-Time Gateway Status**
```javascript
firebase.database()
  .ref('gateways/GW_240AC4123456/status')
  .on('value', (snapshot) => {
    updateDashboard(snapshot.val());
  });
```

### **Monitor New Events**
```javascript
firebase.database()
  .ref('events')
  .orderByKey()
  .limitToLast(10)
  .on('child_added', (snapshot) => {
    showNotification(snapshot.val());
  });
```

---

## 🔄 Migration from UART Protocol

### **Old UART Protocol**
```
Gateway → UART → External ESP32 → Firebase
          (binary packet, 24 bytes)
```

### **New Direct Upload**
```
Gateway → WiFi → Firebase
          (JSON, ~120 bytes)
```

**Advantages**:
- ✅ No external ESP32 needed (cost savings)
- ✅ Fewer failure points (higher reliability)
- ✅ Direct JSON format (easier to query)
- ✅ Rich metadata (RSSI, SNR, via, etc.)
- ✅ Real-time updates (Firebase listeners)

**Trade-offs**:
- ⚠️ Slightly larger payloads (24 bytes → 120 bytes)
- ⚠️ More WiFi bandwidth usage
- ✅ Still acceptable (<40 MB/month)

---

## 📋 Firebase Configuration

### **Firebase Project Setup**

**Required Services**:
1. **Realtime Database**: Store sensor data and gateway status
2. **Authentication**: Anonymous auth for gateway (or custom token)
3. **Cloud Functions** (optional): Auto-cleanup old data

### **Configuration Values Needed**

```cpp
// Firebase credentials (store in NVS!)
#define FIREBASE_HOST "your-project.firebaseio.com"
#define FIREBASE_AUTH "your-database-secret"

// Or use API key + email/password
#define FIREBASE_API_KEY "AIzaSy..."
#define FIREBASE_USER_EMAIL "gateway@example.com"
#define FIREBASE_USER_PASSWORD "..."
```

### **Setup Steps**

1. **Create Firebase Project**:
   - Go to https://console.firebase.google.com/
   - Click "Add Project"
   - Enter project name: `lora-mesh-network`

2. **Enable Realtime Database**:
   - In Firebase Console, go to "Realtime Database"
   - Click "Create Database"
   - Start in "Test Mode" (development) or "Locked Mode" (production)

3. **Get Database URL**:
   - Copy URL: `https://your-project.firebaseio.com`
   - Will be used as `FIREBASE_HOST`

4. **Get Authentication Credentials**:
   - **Option A**: Database Secret (legacy, easier for testing)
     - Go to Project Settings → Service Accounts → Database Secrets
     - Copy secret token
   
   - **Option B**: API Key (recommended for production)
     - Go to Project Settings → General
     - Copy "Web API Key"
     - Create email/password user in Authentication

5. **Set Security Rules**:
   - Copy rules from section above
   - Paste in Database → Rules tab
   - Publish rules

---

## ✅ Schema Validation Checklist

- [x] Gateway info structure defined
- [x] Gateway status structure defined
- [x] Routing table structure defined
- [x] Node info structure defined
- [x] Node latest data structure defined
- [x] Sensor data time-series structure defined
- [x] Events log structure defined
- [x] Security rules defined
- [x] Query examples provided
- [x] Bandwidth analysis completed
- [x] Optimization strategies documented
- [x] Configuration steps documented

---

## 🎯 Next Steps: Phase 3.2

**Task**: Create FirebaseClient Service

**Implementation**:
- Location: `src/application/app_gateway/firebase_client.h/cpp`
- Methods:
  - `initialize()` - Setup Firebase connection
  - `uploadSensorData()` - Upload sensor reading
  - `uploadGatewayStatus()` - Upload gateway status
  - `uploadRoutingTable()` - Upload routing table
  - `uploadEvent()` - Log system event
  - `isConnected()` - Check Firebase connection
- Dependencies:
  - WiFiConnectionService (for connectivity)
  - ArduinoJson (for JSON serialization)
  - Firebase ESP32 Client (for Firebase API)

**Estimated Time**: 2-3 hours

---

**Phase 3.1 Complete!** ✅

Firebase database schema fully designed and documented.
