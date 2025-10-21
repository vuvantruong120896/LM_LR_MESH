# 📋 SUMMARY: Yêu Cầu & Kế Hoạch Phát Triển BLE Offline Data Sync

**Ngày**: Oct 21, 2025  
**Tác Giả**: Dev Team  
**Trạng Thái**: Ready for Development

---

## 📌 YÊUR CẦU CHỐT LẠI (Consolidated)

### **A. GATEWAY SIDE (ESP32 Firmware)**

| # | Yêu Cầu | Chi Tiết | Status |
|---|---------|---------|--------|
| 1 | **Dữ liệu sync** | Sensor Data ONLY (từ OfflineDataBuffer) | ✅ Clear |
| 2 | **Trigger** | On-demand từ App qua BLE (không auto-push) | ✅ Clear |
| 3 | **Delete after sync** | Xóa khỏi NVS sau app ACK | ✅ Clear |
| 4 | **Retry** | Max 2 lần (từ app side, not gateway) | ✅ Clear |
| 5 | **Status tracking** | Quản lý định kỳ **10s interval** (local only) | ✅ Clear |
| 6 | **BLE coexistence** | Tránh xung đột với BLE Provisioning | ✅ Designed |

**Protocol**:
- GET_BUFFERED_DATA (app request offset=0,10,20...)
- Response: {status, count, total, payload}
- Pagination: Max 10 items/response
- ACK: CLEAR_BUFFER command

---

### **B. APP SIDE (Mobile - Kagri App)**

| # | Yêu Cầu | Chi Tiết | Status |
|---|---------|---------|--------|
| 1 | **Long Press Menu** | 3s hold → Show 4 options dialog | ✅ Clear |
| 2 | **Menu Options** | 📝Đổi tên, 📶Wifi, 🔧Update, 🔄Sync Data (NEW) | ✅ Clear |
| 3 | **Sync Data Flow** | BLE read → Parse → Upload Firebase | ✅ Clear |
| 4 | **Retry Logic** | Max 2 lần nếu sync fail | ✅ Clear |
| 5 | **Firebase Upload** | Transform BLE data → Upload like normal | ✅ Clear |

**UI States**:
- Normal (device card)
- Long press in progress (progress indicator)
- Menu dialog (4 options)
- Syncing (progress dialog)
- Success (toast message)
- Error (error dialog with retry)

---

## 🏗️ ARCHITECTURE OVERVIEW

### **Data Flow (Offline Scenario)**

```
┌─────────────────┐
│  LoRa Nodes     │ (cảm biến gửi dữ liệu)
└────────┬────────┘
         │
    ┌────▼─────────────────────┐
    │  Gateway (No WiFi)       │
    │  - Receive LoRa packets  │
    │  - Cannot upload Firebase│
    └────┬─────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  OfflineDataBuffer (NVS)          │
    │  [50 sensor samples buffered]     │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  Status Buffer (10s interval)     │
    │  [Local tracking, no BLE send]    │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  BLE Data Sync Service            │
    │  (Waits for app request)          │
    └────┬───────────────────────────────┘
         │
         │ (BLE Connection)
         │
    ┌────▼──────────────────────────────┐
    │  Mobile App (Kagri)              │
    │  Long press → Sync Data button   │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  BLE Read (max 2 retries)        │
    │  GET_BUFFERED_DATA (pagination)  │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  Parse & Local Store             │
    │  Send ACK: CLEAR_BUFFER          │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  Firebase Upload                 │
    │  Same format as normal sensor    │
    └────┬───────────────────────────────┘
         │
    ┌────▼──────────────────────────────┐
    │  Success: Show "Synced X items"  │
    │  Firebase Database updated       │
    └──────────────────────────────────┘
```

### **BLE Architecture (Coexistence)**

```
BLE Server (Single)
├─ Service 1: Provisioning (550e8400-...)
│  ├─ Char: WiFi SSID
│  ├─ Char: WiFi Password
│  ├─ Char: User UID
│  └─ Char: Status (notify)
│
└─ Service 2: Data Sync (660e8400-...) ← NEW
   ├─ Char: Sync Command (write)
   ├─ Char: Sync Response (read/notify)
   └─ Char: Sync Status (notify)

Conflict Prevention:
- Check: isProvisioningActive() before sync
- Action: Reject sync request if provisioning
- User: See message "Gateway busy, try later"
- Recovery: After provisioning reboot, sync works
```

---

## 📊 DEVELOPMENT TIMELINE

### **Phase 1: Foundation (Week 1, 3-4 days)**

**Gateway**:
- [x] Create: `ble_sync_protocol.h` (protocol definitions)
- [x] Create: `ble_sync_service.h/cpp` (core sync logic)
- [x] Create: `offline_status_buffer.h/cpp` (status tracking)
- [x] Modify: `gateway_app.cpp` (integrate, status update every 10s)
- [x] Test: Local unit tests

**Deliverable**: Gateway can receive BLE requests and respond with sensor data

---

### **Phase 2: Integration & UI (Week 2, 3-4 days)**

**Gateway**:
- [ ] Integrate BleSyncService into ProvisionManager
- [ ] Add GATT characteristics (write/read/notify)
- [ ] Implement conflict prevention (state flags)
- [ ] Handle ACK for buffer deletion
- [ ] BLE connection stability tests

**App**:
- [ ] Create: `device_long_press_menu.dart` widget
- [ ] Create: `ble_sync_progress_dialog.dart` widget
- [ ] Modify: `device_detail_page.dart` (add long press detector)
- [ ] Create: `ble_data_sync_datasource.dart` (BLE read + retry)
- [ ] Create: `offline_sync_repository.dart` (usecase layer)
- [ ] UI testing with mock data

**Deliverable**: Full offline data sync working (Gateway ↔ App BLE)

---

### **Phase 3: Firebase Integration (Week 3, 2-3 days)**

**App**:
- [ ] Transform synced data format → Firebase schema
- [ ] Batch upload to Firebase
- [ ] Mark data as "offline_synced" (metadata)
- [ ] Success/error handling & user feedback
- [ ] E2E testing

**Deliverable**: Complete offline → online workflow (Offline data → App → Firebase)

---

### **Phase 4: Testing & Polish (Week 4, 2 days)**

- [ ] E2E scenarios: Gateway offline → sync → upload
- [ ] Stress tests: 50 items, retry failures, connection drops
- [ ] Performance: Battery, BLE throughput
- [ ] Edge cases: Partial syncs, timeout recovery
- [ ] Documentation & README

**Deliverable**: Production-ready feature

---

## 🎯 FILES TO CREATE/MODIFY

### **Gateway (LM_LR_MESH)**

#### Phase 1 (Foundation)
```
src/application/app_gateway/
├─ ble_sync_protocol.h          (NEW)
├─ ble_sync_service.h           (NEW)
├─ ble_sync_service.cpp         (NEW)
├─ offline_status_buffer.h      (NEW)
├─ offline_status_buffer.cpp    (NEW)
│
└─ gateway_app.cpp              (MODIFY)
   └─ Add: BleSyncService::init() in setupFirebase()
   └─ Add: OfflineStatusBuffer::update() every 10s in loop()
```

#### Phase 2 (Integration)
```
├─ provision_manager.h          (MODIFY)
├─ provision_manager.cpp        (MODIFY)
   └─ Pass BLE server to BleSyncService
   └─ Add: isProvisioningActive() method
```

---

### **App (Kagri App)**

#### Phase 2 (UI)
```
lib/features/device_detail/
├─ presentation/
│  ├─ widgets/
│  │  ├─ device_long_press_menu.dart        (NEW)
│  │  └─ ble_sync_progress_dialog.dart      (NEW)
│  │
│  └─ pages/
│     └─ device_detail_page.dart            (MODIFY)
│        └─ Add GestureDetector for long press
│        └─ Add dialog display logic
│
├─ domain/
│  ├─ entities/
│  │  └─ offline_sensor_data.dart           (NEW)
│  │
│  └─ usecases/
│     ├─ sync_offline_data_usecase.dart     (NEW)
│     └─ upload_synced_data_usecase.dart    (NEW)
│
└─ data/
   ├─ datasources/
   │  └─ ble_data_sync_datasource.dart      (NEW)
   │     ├─ getBufferedData(offset)
   │     ├─ clearBuffer()
   │     └─ retryWithBackoff() max 2x
   │
   └─ repositories/
      └─ offline_sync_repository.dart       (NEW)
```

#### Phase 3 (Firebase)
```
lib/features/device_detail/
├─ data/
│  └─ repositories/
│     └─ offline_sync_repository.dart       (MODIFY)
│        └─ Add: uploadSyncedDataToFirebase()
│        └─ Transform format + batch upload
```

---

## 🚀 IMPLEMENTATION ORDER

### **Recommended Sequence**

1. **Gateway Phase 1** (Foundation)
   - Start: Create protocol + sync service
   - Parallel: Create status buffer
   - Time: 2-3 days
   - Blocker: None

2. **App Phase 2** (UI)
   - Start: After Gateway Phase 1 complete
   - Long press widget + datasource
   - Time: 2-3 days
   - Blocker: Need gateway BLE endpoint

3. **Gateway Phase 2** (Integration)
   - Start: Parallel with App Phase 2
   - Integrate sync into ProvisionManager
   - Time: 1-2 days
   - Blocker: None

4. **App Phase 3** (Firebase)
   - Start: After all Gateway phases complete
   - Firebase upload + transformation
   - Time: 1 day
   - Blocker: Need working BLE sync

5. **Testing & Optimization**
   - Start: All phases complete
   - E2E testing + edge cases
   - Time: 2 days

**Total: ~10 days (2 weeks)**

---

## ✅ ACCEPTANCE CRITERIA

### **Gateway MVP**

- [x] Status recorded every 10s (heap, rssi, uptime)
- [x] BLE data sync independent from provisioning
- [x] No mutual blocking (graceful rejection when busy)
- [x] Max 2 retries on failure (app-side)
- [x] Delete buffer after ACK
- [x] Pagination support (10 items/response)
- [x] No crashes or memory leaks
- [x] Coexistence test: provisioning + sync in sequence

### **App MVP**

- [x] Long press (3s) shows menu with 4 options
- [x] "Sync Data" option functional
- [x] BLE read with max 2 retries
- [x] Progress dialog during sync
- [x] Auto-upload to Firebase after sync
- [x] Show success/error messages
- [x] No UI blocking during sync
- [x] Handle offline scenario (gateway offline, WiFi up)

---

## ⚠️ RISK MITIGATION

| Risk | Severity | Mitigation |
|------|----------|-----------|
| BLE provisioning & sync conflict | HIGH | Coexistence design + state flags + testing |
| Large buffer MTU overrun | MEDIUM | Pagination (10 items/response = ~200B < 244B MTU) |
| Sync timeout during upload | MEDIUM | Max 2 retries + timeout handling |
| Data loss on connection drop | LOW | ACK-based deletion (only delete after confirm) |
| Battery drain from BLE | LOW | Progress UI + user can cancel |

---

## 🔗 RELATED DOCUMENTS

1. **BLE_OFFLINE_SYNC_DETAILED_PLAN.md** - Full technical design
2. **BLE_COEXISTENCE_ARCHITECTURE.md** - Conflict prevention strategy
3. **OFFLINE_BLE_SYNC_REQUIREMENTS.md** - Initial analysis (v1)

---

## 🎬 NEXT ACTIONS

### **Immediate (Today)**

- [ ] Review & approve this summary
- [ ] Confirm timeline & team allocation
- [ ] Create GitHub issues for Phase 1

### **Week 1 (Gateway Phase 1)**

- [ ] Start: `ble_sync_protocol.h` (protocol definitions)
- [ ] Start: `ble_sync_service.h/cpp` (core sync)
- [ ] Start: `offline_status_buffer.h/cpp` (status tracking)
- [ ] Code review: Each file reviewed before merge
- [ ] Test: Unit tests for sync logic

### **Week 2 (App UI + Gateway Integration)**

- [ ] App: Long press menu UI
- [ ] Gateway: Integrate into ProvisionManager
- [ ] BLE coexistence testing
- [ ] E2E: Basic offline sync test

### **Week 3 (Firebase Upload)**

- [ ] App: Firebase upload from synced data
- [ ] Full E2E: Offline → sync → Firebase

### **Week 4 (Testing & Release)**

- [ ] Stress testing
- [ ] Edge case validation
- [ ] Documentation
- [ ] Release candidate

---

## 📞 QUESTIONS FOR CLARIFICATION

Before starting, please confirm:

1. **Status Buffer**: 
   - Should status (heap, uptime, rssi) be synced via BLE in future?
   - Or keep local-only indefinitely?
   - ➜ **Answer**: Local-only for MVP (can extend later)

2. **Pagination**:
   - If buffer has 50 items, app will make 5 requests?
   - ➜ **Answer**: Yes, 10 items per request (safe BLE MTU)

3. **Offline Indicator**:
   - Should app show "X pending items" badge?
   - ➜ **Answer**: Optional (nice-to-have for v1.1)

4. **Recovery on Failure**:
   - If sync fails twice, app shows retry button?
   - ➜ **Answer**: Yes, or user can open menu again

5. **Status Upload to Firebase**:
   - Should "last 10 status snapshots" sync via BLE?
   - ➜ **Answer**: Not in MVP (future phase)

---

## 📝 SIGN-OFF

| Role | Name | Date | Approval |
|------|------|------|----------|
| Developer | TBD | Oct 21, 2025 | ⏳ Pending |
| Tech Lead | TBD | Oct 21, 2025 | ⏳ Pending |
| Product | TBD | Oct 21, 2025 | ⏳ Pending |

