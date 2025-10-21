# 🎯 QUICK REFERENCE: BLE Offline Sync Development

**Status**: Documentation Complete ✅  
**Start Date**: Oct 21, 2025  
**Duration**: 2 weeks (10 days)  
**Commitment**: Phase 1 Foundation immediately

---

## 📋 YÊU CẦU TÓM TẮT (5 phút read)

### **GATEWAY (ESP32)**
- ✅ Gửi Sensor Data qua BLE (on-demand từ App)
- ✅ Xóa NVS sau app ACK
- ✅ Max 2 retries
- ✅ Quản lý status định kỳ **10s interval**
- ✅ **TRÁNH xung đột** với BLE Provisioning (coexistence design)

### **APP (Mobile)**
- ✅ Long press (3s) → Menu 4 options
- ✅ Sync Data → BLE read + max 2 retry
- ✅ Upload → Firebase like normal sensor

---

## 🏗️ ARCHITECTURE (30s overview)

```
LoRa → Gateway (offline) → NVS Buffer
                              ↓
                         BLE Sync Service
                         (coexistent with Provisioning)
                              ↓
                         Mobile App (long press)
                              ↓
                         Firebase Upload ✅
```

**BLE Services**:
- Service 1: Provisioning (UUID: 550e...)
- Service 2: Data Sync (UUID: 660e...) ← NEW, SEPARATE

**Conflict Prevention**: State flags + early return (no lock)

---

## 📅 TIMELINE (2 weeks)

| Phase | Task | Duration | Blocker |
|-------|------|----------|---------|
| 1 | Gateway Foundation (protocol + service) | 3-4 days | None |
| 2 | App UI + Gateway Integration | 3-4 days | Phase 1 ✓ |
| 3 | Firebase Upload | 2-3 days | Phase 2 ✓ |
| 4 | Testing & Polish | 2 days | Phase 3 ✓ |

**Critical Path**: Phase 1 → Phase 2 (parallel start) → Phase 3 → Phase 4

---

## 🛠️ FILES TO CREATE (Phase 1)

**Gateway**:
```
src/application/app_gateway/
├─ ble_sync_protocol.h       (protocol definitions)
├─ ble_sync_service.h/cpp    (core sync logic)
├─ offline_status_buffer.h/cpp (status tracking)
└─ gateway_app.cpp           (integrate)
```

**App**:
```
lib/features/device_detail/
├─ presentation/widgets/
│  ├─ device_long_press_menu.dart      (NEW)
│  └─ ble_sync_progress_dialog.dart    (NEW)
├─ domain/usecases/
│  ├─ sync_offline_data_usecase.dart   (NEW)
│  └─ upload_synced_data_usecase.dart  (NEW)
└─ data/datasources/
   └─ ble_data_sync_datasource.dart    (NEW)
```

---

## 📡 BLE PROTOCOL (1 min)

### **Request** (App → Gateway)
```
[Command ID] [Flags] [Offset] [Reserved]
0x01 = GET_BUFFERED_DATA
0x02 = CLEAR_BUFFER
```

### **Response** (Gateway → App)
```
[Status] [Count] [Total] [Checksum] [Payload...]
Status: 0x00=OK, 0x01=EOF, 0x02=Error
```

### **Example**
```
App request:  GET_BUFFERED_DATA(offset=0)
Gateway response: 10 items, total=50 items
App request:  GET_BUFFERED_DATA(offset=10)
...
```

---

## ⚠️ CONFLICT PREVENTION (Critical!)

### **Two BLE Services**

```
Current: BLE Provisioning (WiFi + UID)
New:     BLE Data Sync (Sensor Data)
```

**Strategy**: Coexistent (separate services, separate characteristics)

**Conflict Handling**:
```cpp
// In BleSyncService
if (ProvisionManager::isProvisioningActive()) {
    return ERROR;  // Reject sync
}

// In App
if (response.status == 0x02) {
    showMessage("Gateway busy provisioning, try later");
}
```

**Recovery**: Provisioning triggers reboot → auto-recovery

---

## ✅ CHECKLIST: Before Starting

- [ ] All 3 analysis documents reviewed
- [ ] Team confirmed 2-week commitment
- [ ] GitHub issues created for Phase 1
- [ ] Developer assigned (Gateway + App)
- [ ] Test environment setup

---

## 📚 FULL DOCUMENTATION

1. **BLE_OFFLINE_SYNC_SUMMARY.md** ← Start here (this folder)
2. **BLE_OFFLINE_SYNC_DETAILED_PLAN.md** (full tech design)
3. **BLE_COEXISTENCE_ARCHITECTURE.md** (conflict prevention)
4. **OFFLINE_BLE_SYNC_REQUIREMENTS.md** (v1 analysis, reference)

---

## 🚀 NEXT STEPS (TODAY)

1. **Review** this summary + main 3 documents
2. **Confirm** timeline & team
3. **Create** GitHub issues for Phase 1:
   - `[Gateway] Create BLE Sync Protocol & Service`
   - `[App] Create Long Press Menu UI`
4. **Assign** developers
5. **Start** Phase 1

---

## 🎯 SUCCESS CRITERIA

✅ Gateway & App communicate offline data via BLE  
✅ App can upload to Firebase after sync  
✅ No crashes or conflicts with provisioning  
✅ Max 2 retries on failure  
✅ 10s status management  
✅ All tests pass  

---

## 📞 QUESTIONS?

- See **BLE_OFFLINE_SYNC_DETAILED_PLAN.md** for technical Q&A
- See **BLE_COEXISTENCE_ARCHITECTURE.md** for conflict scenarios
- Ask team lead for clarification

---

**Prepared by**: Dev Team  
**Date**: Oct 21, 2025  
**Ready to Code**: ✅ YES

