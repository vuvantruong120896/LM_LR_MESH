# 📋 BLE Offline Sync - Phase 1 Implementation Kickoff Checklist

**Date**: October 21, 2024  
**Status**: ✅ **READY TO START**  
**Project**: BLE Offline Data Synchronization Feature  
**Duration**: 2 weeks (4 phases)

---

## 📖 ONE-PAGE QUICK SUMMARY

### What We're Building
When gateway loses internet (WiFi down), sensor data syncs to app via BLE on long-press (3-second menu). Data uploads to Firebase when connection restored.

### Why It Matters
- Users can retrieve sensor data even during internet outages
- No data loss (buffered locally until sync)
- Simple UX (long-press menu, familiar interaction pattern)

### How It Works (Simple)
```
Offline
  ↓ (buffer sensor data in NVS)
  ↓
User long-press device (3s)
  ↓ (triggers "Sync Data" menu)
  ↓
BLE read data from gateway
  ↓ (app confirms receipt)
  ↓
Firebase upload when online
```

### Key Numbers
- **Buffer**: 50 sensor records max
- **Sync time**: ~5 seconds for 50 items
- **BLE retry**: Max 2 attempts
- **Status update**: Every 10 seconds
- **Timeline**: 2 weeks (4 phases)
- **Team**: 2 developers (1 gateway, 1 app)

---

## ✅ PHASE 1: GATEWAY FOUNDATION (3-4 Days)

### Files to Create
```
src/ble_sync_protocol.h          ← Protocol constants & structures
src/ble_sync_service.h           ← BleSyncService class interface
src/ble_sync_service.cpp         ← Implementation (~400 lines)
src/offline_status_buffer.h      ← Status tracking interface
src/offline_status_buffer.cpp    ← Implementation (~200 lines)
```

### What Each File Does

**ble_sync_protocol.h** (50 lines)
- Define command IDs: `GET_BUFFERED_DATA`, `CLEAR_BUFFER`
- Define response codes: `OK`, `DENIED`, `ERROR`, `WAITING`
- Define packet structures with checksum

**ble_sync_service.h** (80 lines)
```cpp
class BleSyncService {
  bool init(BleServer* server);
  int handleGetBufferedData(int offset);  // Returns paginated data
  int handleClearBuffer(int ack_count);   // Delete after confirm
  bool canProcessSync();                  // Check provisioning conflict
};
```

**ble_sync_service.cpp** (400 lines)
- Implement paginated reads (max 10 items per packet)
- Implement buffer read from OfflineDataBuffer
- Implement delete confirmation logic
- Implement conflict check before operations

**offline_status_buffer.h** (60 lines)
```cpp
class OfflineStatusBuffer {
  void update();  // Called every 10s, snapshots heap/rssi/uptime
  Status getLatest();  // Return most recent snapshot
};
```

**offline_status_buffer.cpp** (200 lines)
- Store up to 10 status snapshots in heap
- Update every 10 seconds (called from gateway_app loop)
- Manage circular buffer, no persistence

### Success Criteria
- [ ] Code compiles without errors
- [ ] GET_BUFFERED_DATA works with mock data (test with 5, 50 items)
- [ ] Pagination correct (5 packets for 50 items)
- [ ] Status buffer updates every 10s
- [ ] No heap/stack leaks
- [ ] Provisioning service still works (no interference)
- [ ] Unit tests written & passing

### How to Start

**1. Create Feature Branch**
```bash
git checkout -b feature/ble-offline-sync-phase1
```

**2. Scaffold Files**
- Create all 5 files (start with headers)
- Add stubs for all public methods
- Compile to verify structure

**3. Implement Protocol** (Start easiest)
- Implement `ble_sync_protocol.h` (constants, structs)
- Unit test: Verify packet format

**4. Implement OfflineStatusBuffer** (Self-contained)
- Implement circular buffer storage
- Implement time-based snapshot updates
- Unit test: Verify 10 snapshots stored correctly

**5. Implement BleSyncService** (Core logic)
- Implement `canProcessSync()` (check provisioning flag)
- Implement `handleGetBufferedData()` with pagination
- Implement `handleClearBuffer()` with ACK logic
- Integration test: Read from OfflineDataBuffer, paginate, respond

**6. Review Checklist**
- [ ] All public methods have error handling
- [ ] Memory is freed on error paths
- [ ] Provisioning flag checked before BLE operations
- [ ] Pagination tested: 1, 10, 50 items
- [ ] Response format matches protocol spec

---

## 📋 PHASE 1 DEVELOPER TASKS

### Day 1: Setup & Headers
- [ ] Read QUICK_START_BLE_SYNC.md (5 min)
- [ ] Read BLE_OFFLINE_SYNC_DETAILED_PLAN.md (1 hour)
- [ ] Create feature branch
- [ ] Create all 5 header files with interfaces
- [ ] Compile (verify no errors)
- **Commit**: "feat(phase1): Add BLE sync header files and protocol definitions"

### Day 2: Protocol & Status Buffer
- [ ] Implement `ble_sync_protocol.h` constants & structs
- [ ] Implement `offline_status_buffer.cpp` (circular buffer, 10s updates)
- [ ] Write unit tests for both
- [ ] Run unit tests (should pass)
- **Commit**: "feat(phase1): Implement protocol and status buffer"

### Day 3: BLE Sync Service Core
- [ ] Implement `ble_sync_service.cpp` (pagination, buffer reads, ACK logic)
- [ ] Implement conflict check (`canProcessSync()`)
- [ ] Write integration tests (mock data, verify pagination)
- [ ] Test with 1, 10, 50 items
- **Commit**: "feat(phase1): Implement BleSyncService with pagination"

### Day 4: Integration & Review
- [ ] Compile with full gateway app (no breaking changes)
- [ ] Verify provisioning service still works
- [ ] Code review: Check error handling, memory, resource cleanup
- [ ] Fix review comments
- [ ] Merge to develop branch
- **Commit**: "test(phase1): Add E2E tests and integration verification"
- **Tag**: `v1.0.0-phase1-complete`

---

## 🎯 PHASE 2: GATEWAY INTEGRATION (1-2 Days - Parallel Track)

### What's Modified
```
src/gateway_app.cpp      ← Add BleSyncService init + status updates
src/provision_manager.cpp ← Add state flags, GATT characteristics
```

### Key Changes
1. In `gateway_app.cpp` setupFirebase():
   ```cpp
   BleSyncService::init(bleServer);  // Initialize sync service
   ```

2. In `gateway_app.cpp` loop():
   ```cpp
   if (millis() - lastStatusUpdate > 10000) {
     OfflineStatusBuffer::update();  // Every 10s
     lastStatusUpdate = millis();
   }
   ```

3. In `provision_manager.cpp`:
   - Add flag: `bool provisioning_active = false;`
   - Set to true when provisioning starts
   - Set to false when provisioning completes
   - Pass BLE server to BleSyncService

### Success Criteria
- [ ] BleSyncService integrated into main loop
- [ ] Status updates every 10 seconds (verified with logs)
- [ ] Conflict check working (provisioning denies sync)
- [ ] No new warnings/errors
- [ ] No interference with existing features

---

## 🎯 PHASE 2B: MOBILE APP (2-3 Days - Parallel Track)

### What's New (Kagri App)
```
lib/widgets/device_long_press_menu.dart
lib/widgets/ble_sync_progress_dialog.dart
lib/datasources/ble_data_sync_datasource.dart
lib/repositories/offline_sync_repository.dart
```

### Core Features
1. **Long-press Detection**: 3-second hold on device triggers menu
2. **Menu Options**: 
   - Rename Device
   - Configure WiFi
   - Check Updates
   - **Sync Data** ← NEW
3. **BLE Sync UI**: Progress dialog showing item count, retry status
4. **Retry Logic**: Max 2 attempts on BLE failure with user notification

### Key Implementation Points
- Use `GestureDetector` with `onLongPress` callback
- Implement `Duration(seconds: 3)` threshold
- BLE read with max 2 retries
- Handle pagination (offset tracking)
- Show progress: "Synced 15/50 items..."

---

## 📞 DOCUMENTATION REFERENCE

### For Developers
1. **QUICK_START_BLE_SYNC.md** (5 min) - Start here
2. **BLE_OFFLINE_SYNC_DETAILED_PLAN.md** (1 hour) - Deep dive on implementation
3. **BLE_COEXISTENCE_ARCHITECTURE.md** (30 min) - Conflict prevention details

### For PM/Tech Lead
1. **BLE_OFFLINE_SYNC_PROJECT_STATUS.md** - This comprehensive roadmap
2. **ANALYSIS_COMPLETE_FINAL_SUMMARY.md** - Complete requirements

### For Code Review
1. **BLE_COEXISTENCE_ARCHITECTURE.md** - Know what to check
2. **BLE_OFFLINE_SYNC_DETAILED_PLAN.md** "Implementation Strategy" section

---

## 🚀 IMMEDIATE ACTIONS (This Week)

### Monday (Oct 21) - TODAY ✅
- [ ] All team reviews QUICK_START_BLE_SYNC.md
- [ ] Gateway developer reviews BLE_OFFLINE_SYNC_DETAILED_PLAN.md
- [ ] Schedule Phase 1 kickoff meeting (30 min)

### Tuesday (Oct 22) - START PHASE 1
- [ ] Create feature branch
- [ ] Scaffold 5 header files
- [ ] Start implementation (Day 1 tasks)

### Wednesday (Oct 23) - Midpoint Check
- [ ] Review progress (should be 50% through Day 2-3)
- [ ] No blockers? Continue
- [ ] Any issues? 1-hour huddle with tech lead

### Thursday-Friday (Oct 24-25)
- [ ] Complete Phase 1 implementation
- [ ] Code review & fixes
- [ ] Merge to develop
- [ ] Tag release

---

## 🔧 TECHNICAL SETUP

### Prerequisites
- ESP-IDF 4.4.x or 5.x
- PlatformIO configured
- FreeRTOS knowledge (task creation, queue usage)
- Understanding of existing OfflineDataBuffer

### Tools Needed
- Visual Studio Code + PlatformIO extension
- Git client
- BLE debugger (optional, for Phase 2 testing)

### Project Structure
```
LM_LR_MESH/
├── src/
│   ├── gateway_app.cpp/h         (main app, to be modified in Phase 2)
│   ├── provision_manager.cpp/h   (WiFi provisioning, to be modified in Phase 2)
│   ├── offline_data_buffer.cpp/h (existing, read-only in Phase 1)
│   ├── ble_sync_protocol.h       (CREATE - Phase 1)
│   ├── ble_sync_service.cpp/h    (CREATE - Phase 1)
│   └── offline_status_buffer.cpp/h (CREATE - Phase 1)
├── test/
│   ├── test_ble_sync_protocol.cpp (CREATE - Phase 1)
│   ├── test_ble_sync_service.cpp (CREATE - Phase 1)
│   └── test_offline_status_buffer.cpp (CREATE - Phase 1)
├── docs/
│   ├── BLE_OFFLINE_SYNC_DETAILED_PLAN.md (READ THIS)
│   ├── BLE_COEXISTENCE_ARCHITECTURE.md (READ THIS)
│   ├── QUICK_START_BLE_SYNC.md (READ THIS)
│   └── BLE_OFFLINE_SYNC_PROJECT_STATUS.md (You are here)
└── platformio.ini
```

---

## ⚠️ COMMON PITFALLS & SOLUTIONS

| Pitfall | Solution |
|---------|----------|
| Data loss if delete fails | Use ACK-based deletion, 2-retry on CLEAR_BUFFER |
| BLE packet too large (>240B) | Implement pagination (max 10 items/packet) |
| Provisioning interferes with sync | Check `isProvisioningActive()` before BLE ops |
| Memory leak in BleSyncService | Call cleanup in destructor, test with valgrind |
| Status buffer fills up | Implement circular buffer, overwrite oldest entry |
| App tries to sync twice | Disable menu while sync in progress |
| Firebase upload fails after sync | Store data locally with metadata, retry later |

---

## 📊 PROGRESS TRACKING

### Weekly Update Template (every Friday)
```
Week 1 Status:
✅ Phase 1 Progress: [50%|70%|100%]
- Completed: [list done items]
- In Progress: [current task]
- Blockers: [none|describe issue]
- Estimate to Completion: [day]
- Notes: [anything important]

Week 2 Status:
✅ Phase 2A Progress: [%]
✅ Phase 2B Progress: [%]
- Next: Phase 3 (Firebase upload)
```

---

## 🎉 DEFINITION OF DONE (Phase 1)

- [ ] All 5 files created and compiling
- [ ] All methods implemented with error handling
- [ ] Unit tests written and passing
- [ ] Integration tests verify pagination works (1, 10, 50 items)
- [ ] No memory leaks detected
- [ ] No warnings/errors in build log
- [ ] Code review completed and approved
- [ ] Merged to develop branch
- [ ] Tagged `v1.0.0-phase1-complete`
- [ ] Demo: BleSyncService responding to BLE reads with paginated data
- [ ] Documented: Added comments explaining conflict prevention logic

---

## 📞 SUPPORT & QUESTIONS

**For Technical Questions**:
- Gateway: Check BLE_OFFLINE_SYNC_DETAILED_PLAN.md section "Implementation Strategy"
- App: Check BLE_OFFLINE_SYNC_DETAILED_PLAN.md section "App Implementation"
- Conflicts: Check BLE_COEXISTENCE_ARCHITECTURE.md section "Conflict Prevention"

**For Blockers**:
- Escalate to Tech Lead immediately (1-hour response target)
- Document issue, timeline impact, proposed solutions

**For Scope Changes**:
- Escalate to Product Manager same day
- No scope changes without approval + timeline adjustment

---

## 🏁 SUCCESS VISION (Oct 31)

By the end of Week 2:
- ✅ Gateway: BleSyncService complete, integrated, tested
- ✅ App: Long-press menu working, BLE sync with retry, UI complete
- ✅ Firebase: Offline-synced data uploading with metadata
- ✅ Tests: E2E test passing, 50-item stress test passing
- ✅ Ready: Feature ready for beta user testing

---

**Remember**: This is a team effort. Communicate early, collaborate often, ask questions without hesitation. We're building something cool that solves a real user problem! 🚀

---

*Last Updated: October 21, 2024*  
*All documentation available in `/docs/` folder*  
*Questions? Start with QUICK_START_BLE_SYNC.md*
