# Yêu Cầu: Đồng Bộ Dữ Liệu Gateway ↔ App qua BLE Khi Mất Kết Nối Internet

**Ngày**: Oct 21, 2025  
**Trạng Thái**: Phân Tích Yêu Cầu  
**Tác Giả**: Development Team

---

## 📋 Tóm Tắt Yêu Cầu

Khi Gateway **mất kết nối Internet/Firebase**, cần có khả năng đồng bộ dữ liệu giữa Gateway và Mobile App (Kagri App) thông qua **BLE (Bluetooth Low Energy)**, với dữ liệu được lưu trữ trong **NVS (Non-Volatile Storage)** của Gateway.

---

## 🎯 Chi Tiết Yêu Cầu

### 1. **Tình Huống Sử Dụng (Use Cases)**

#### 1.1 Đồng Bộ Dữ Liệu Sensor Offline
```
Tình huống: Gateway mất WiFi/Firebase, các cảm biến LoRa vẫn gửi dữ liệu về
- Gateway nhận dữ liệu từ các node LoRa
- Lưu vào OfflineDataBuffer (NVS) thay vì upload Firebase
- Khi mở Mobile App, kết nối BLE và download dữ liệu
- Dữ liệu được đồng bộ với từng node (0xE764, 0x1234, v.v.)
```

#### 1.2 Đồng Bộ Routing Table
```
Tình huống: Cần cập nhật danh sách các node đã kết nối mesh
- Gateway duy trì Routing Table (danh sách nodes kết nối)
- Khi BLE kết nối, gửi routing table đến App
- App hiển thị các thiết bị hiện đang kết nối mesh
- Người dùng có thể xem trạng thái mesh ngay cả khi offline
```

#### 1.3 Đồng Bộ Gateway Status
```
Tình huống: Cần biết tình trạng hoạt động của Gateway
- Heap usage, WiFi signal, uptime, số packet nhận được
- Lưu status periodically vào NVS
- Gửi qua BLE khi app kết nối
- App hiển thị health status của gateway
```

### 2. **Dữ Liệu Cần Đồng Bộ**

| Dữ Liệu | Nguồn | Lưu Trữ | Kích Thước Ước Tính |
|---------|-------|---------|-------------------|
| **Sensor Data** | Node LoRa → Gateway | OfflineDataBuffer (NVS) | ~50 samples × 44 bytes = ~2.2 KB |
| **Routing Table** | RoutingTableService | Có thể lưu NVS | Max 50 nodes × ~32 bytes = ~1.6 KB |
| **Gateway Status** | GatewayApp state | Có thể lưu NVS | ~256 bytes |
| **Network Config** | BLE Provisioning | NVS | ~512 bytes |

**Tổng NVS dự kiến: ~5-10 KB**

---

## 🔧 Thành Phần Hiện Có (Current Implementation)

### ✅ Đã Có
1. **OfflineDataBuffer** (gateway_app/ folder)
   - Lưu sensor data vào NVS khi offline
   - API: `addData()`, `getOldestData()`, `removeOldest()`
   - Capacity: 50 samples (FIFO circular buffer)
   - Status: Hoạt động, nhưng **chưa có routing table & status**

2. **RoutingTableService**
   - Quản lý danh sách nodes trong mesh
   - API: `routingTableList` (linked list)
   - **Chưa có**: Lưu vào NVS, BLE sync API

3. **BLE Provisioning** (ProvisionManager)
   - Xử lý BLE setup/provisioning
   - **Chưa có**: BLE data sync protocol (chỉ có provisioning)

4. **Firebase Queue System** (firebase_queue.cpp)
   - Non-blocking queue upload
   - **Liên quan**: Cần extend để support BLE sync

### ❌ Cần Thêm
1. **BLE Data Sync Service**
   - Protocol để trao đổi dữ liệu offline qua BLE
   - State management (offline → syncing → synced)

2. **Routing Table Persistence**
   - Lưu/load routing table từ NVS
   - Versioning để detect thay đổi

3. **Status History Buffer**
   - Lưu gateway status snapshots vào NVS
   - Khi offline, app có thể xem lịch sử

4. **BLE Command Protocol**
   - Định nghĩa packet format (request/response)
   - Pagination cho dữ liệu lớn
   - Error handling & retry

---

## 📐 Kiến Trúc Đề Xuất

### A. Offline Data Flow (Khi mất Internet)

```
┌─────────────────────────────────────────────────────────────┐
│                    LoRa Node                                │
└──────────────────────┬──────────────────────────────────────┘
                       │ Sensor Data
                       ▼
      ┌────────────────────────────────────┐
      │   Gateway LoRa Receiver Task       │
      │ (processGatewayPackets)            │
      └────────────────────┬───────────────┘
                           │
        ┌──────────────────▼──────────────────┐
        │   Check: WiFi + Firebase OK?       │
        └──────────────────┬──────────────────┘
                           │
        ┌──────┬───────────┴───────────┬──────────┐
        │      │                       │          │
      YES     NO (Offline)           PARTIAL    TEMP ERROR
        │      │                       │          │
        │      ▼                       │          │
        │  ┌─────────────────────────┐ │      [Retry]
        │  │ OfflineDataBuffer.add() │ │
        │  │ (Save to NVS)           │ │
        │  └────┬────────────────────┘ │
        │       │ Stored               │
        │       ▼                       │
        │  ┌─────────────────────────┐ │
        │  │  NVS: offline_buf       │ │
        │  │  [sensor_0] {data}      │ │
        │  │  [sensor_1] {data}      │ │
        │  │  ...                    │ │
        │  └─────────────────────────┘ │
        │                               │
        └───────────┬───────────────────┘
                    │
                    ▼
        ┌─────────────────────────────────┐
        │  Firebase Queue (Offline Mode)  │
        │  - No upload (Firebase offline) │
        │  - Local buffering only         │
        └─────────────────────────────────┘
```

### B. BLE Sync Flow (App kết nối)

```
┌──────────────────┐
│  Mobile App      │
│  (Kagri App)     │
└────────┬─────────┘
         │ BLE Connect
         ▼
┌──────────────────────────────────────┐
│        BLE Provisioning Manager      │
│ (Has existing BLE connection)        │
└────────────┬─────────────────────────┘
             │
      ┌──────▼──────┐
      │ Authenticate│
      └──────┬──────┘
             │
   ┌─────────┴────────────────┐
   │                          │
   ▼                          ▼
[Request]              [Response]
GET_BUFFERED_DATA  ←→  [Sensor Data]
GET_ROUTING_TABLE  ←→  [Routing Table]
GET_STATUS_HISTORY ←→  [Status Array]
CLEAR_BUFFER_AFTER ←→  [Success]

   │                          │
   └─────────────────────┬────┘
                         │
                  ┌──────▼──────────┐
                  │  Update App UI  │
                  │  - Pending data │
                  │  - Node list    │
                  │  - Status       │
                  └─────────────────┘
```

### C. Data Structures (NVS Schema)

```cpp
// ============ OfflineDataBuffer (Existing) ============
Namespace: "offline_buf"
Keys:
  - head, tail, count, version
  - [data_0..49] (sensor data)
  - [node_0..49] (node IDs)

// ============ Routing Table (New) ============
Namespace: "route_table"
Keys:
  - version (incremented on changes)
  - count (number of nodes)
  - [route_0..N] (serialized RouteNode)

// ============ Status History (New) ============
Namespace: "status_hist"
Keys:
  - index (0-9, circular buffer)
  - [status_0..9] (last 10 status snapshots, 5 min each)
  - timestamp_[0..9] (when each status was recorded)

// ============ BLE Sync State (New) ============
Namespace: "ble_sync"
Keys:
  - last_sync_time (millis when last sync happened)
  - pending_count (how many samples pending)
  - sync_status (idle, syncing, completed)
```

---

## 🔌 BLE Sync Protocol (Proposal)

### Command Format (GATT Characteristic)

```
Request (App → Gateway):
┌────────┬────────┬─────────┬──────────┐
│ CMD ID │ Flags  │ Offset  │ Reserved │
│ 1 byte │ 1 byte │ 2 bytes │ 4 bytes  │
└────────┴────────┴─────────┴──────────┘

Response (Gateway → App):
┌────────┬───────┬──────────┬─────────────┐
│ STATUS │ COUNT │ Total    │ Payload     │
│ 1 byte │1 byte │ 2 bytes  │ Variable    │
└────────┴───────┴──────────┴─────────────┘

Command IDs:
0x01 = GET_BUFFERED_DATA
0x02 = GET_ROUTING_TABLE
0x03 = GET_STATUS_HISTORY
0x04 = CLEAR_BUFFER
0x05 = GET_SYNC_INFO
```

### Command: GET_BUFFERED_DATA

```
Request:
  - offset: Start index (0, 5, 10, ...)  [Pagination]

Response:
  - status: 0=OK, 1=EOF, 2=ERROR
  - count: Number of entries in this response
  - total: Total entries available
  - payload: Array of {timestamp, nodeId, sensorData}
```

### Command: GET_ROUTING_TABLE

```
Response:
  - status: 0=OK, 2=ERROR
  - count: Number of nodes
  - payload: Array of {nodeId, address, rssi, nextHop, ...}
```

---

## 🛠️ Implementation Plan

### **Phase 1: Foundation** (Week 1)
- [ ] Create `ble_sync_service.h/cpp` (BLE data sync API)
- [ ] Create `offline_status_buffer.h/cpp` (Status history storage)
- [ ] Extend OfflineDataBuffer to include metadata (timestamps, sync state)
- [ ] Add routing table persistence to NVS

### **Phase 2: Gateway Side** (Week 2)
- [ ] Integrate BLE sync service into ProvisionManager
- [ ] Add status history collection in gateway_app.cpp
- [ ] Implement routing table serialization/deserialization
- [ ] Add BLE GATT characteristics for data sync

### **Phase 3: Mobile App Side** (Week 3)
- [ ] Create BLE data sync UI screen in Kagri App
- [ ] Implement command request/response handling
- [ ] Add pagination for large datasets
- [ ] Display sync progress (pending/synced indicators)

### **Phase 4: Testing & Optimization** (Week 4)
- [ ] E2E testing (offline → BLE sync → upload)
- [ ] Stress testing (1000+ pending samples)
- [ ] Performance profiling (BLE bandwidth, battery)
- [ ] Edge case handling (partial sync, connection drop)

---

## 📊 Data Sync Strategy

### **Sync Decision Matrix**

| Scenario | Action | Priority |
|----------|--------|----------|
| WiFi lost, BLE connect | Sync all pending | High |
| WiFi lost, BLE disconnect | Keep buffering | Low |
| WiFi restored | Stop BLE sync, resume Firebase | High |
| Buffer full (50 samples) | Start dropping oldest | Emergency |
| App opens while offline | Show pending count | Immediate |

### **Collision Handling**

```cpp
// Scenario: WiFi restored while BLE syncing
if (wifiConnected && bleConnected) {
    // Stop BLE sync (don't interrupt)
    // Resume Firebase upload (high priority)
    // Complete BLE request gracefully (send ACK)
}
```

---

## 🔐 Security Considerations

1. **Authentication**
   - BLE Provisioning already handles pairing
   - Reuse same security context

2. **Data Integrity**
   - Checksums on each packet (CRC16)
   - Version number to detect format changes

3. **Privacy**
   - Sensor data stays local until user sync
   - No automatic background sync without consent

---

## 💾 Storage Budget

```
Typical NVS Partition: 16 KB - 32 KB

Allocation:
- OfflineDataBuffer:   4.7 KB (50 × 44B + metadata)
- RoutingTable:        1.6 KB (50 × 32B + metadata)
- StatusHistory:       1.0 KB (10 × 100B + metadata)
- BLE Sync State:      0.5 KB
- Reserved:            1.0 KB

Total Used:           ~9 KB
Headroom:             ~7-23 KB ✅
```

---

## 🚀 Quick Start Questions for Clarification

Before we start coding, please confirm:

1. **Sync Frequency**: 
   - Should app automatically sync when connecting BLE?
   - Or manual "sync now" button?

2. **Data Retention**:
   - Delete data after successful sync?
   - Or keep last N sync batches?

3. **Status Priority**:
   - Should status be synced with every sensor data?
   - Or separate status polling?

4. **Routing Table Updates**:
   - Should app get real-time updates on node joins/leaves?
   - Or snapshot-based (sync on demand)?

5. **Offline Indicator**:
   - Should app show "pending items" indicator?
   - Should gateway show "data buffering" LED pattern?

6. **Failure Handling**:
   - Retry policy if BLE sync fails?
   - How long to keep retrying?

---

## 📝 Next Steps

1. **Review & Confirm** this analysis with your requirements
2. **Clarify** the questions above
3. **Create Implementation PR** for Phase 1
4. **Code Review** & merge
5. **Start Phase 2** (Gateway integration)

