# Quick Start Guide - Smart Provisioning via Firebase

## 🚀 Bắt đầu ngay (30 phút đầu tiên)

### Step 1: Apply Firebase Security Rules (5 phút)

1. Mở Firebase Console: https://console.firebase.google.com
2. Chọn project: `kagri-iot`
3. Vào **Realtime Database** → Tab **Rules**
4. Copy toàn bộ nội dung từ file: `docs/firebase-rules.json`
5. Paste vào editor
6. Click **Publish**
7. ✅ Verify: Không có error

### Step 2: Test Firebase Structure (10 phút)

1. Vào **Realtime Database** → Tab **Data**
2. Click **+** để add manual test data:

```json
{
  "users": {
    "test-uid-123": {
      "profile": {
        "email": "test@example.com",
        "displayName": "Test User"
      },
      "gateways": {
        "AA:BB:CC:DD:EE:FF": {
          "info": {
            "mac": "AA:BB:CC:DD:EE:FF",
            "address": "0xEEFF",
            "name": "Test Gateway"
          },
          "status": {
            "online": true,
            "can_accept_commands": true
          }
        }
      },
      "commands": {
        "AA:BB:CC:DD:EE:FF": {
          "pending": {}
        }
      }
    }
  }
}
```

3. ✅ Save và verify structure tạo thành công

### Step 3: Test Manual Command (10 phút)

1. Trong Firebase Console, navigate đến:
   `users/test-uid-123/commands/AA:BB:CC:DD:EE:FF/pending/`

2. Click **+** add child:
   - Key: `cmd_test_001`
   - Value:
   ```json
   {
     "id": "cmd_test_001",
     "type": "start_provisioning",
     "params": {
       "durationMs": 60000
     },
     "timestamp": 1729234567000,
     "status": "pending",
     "priority": 1
   }
   ```

3. ✅ Verify: Command xuất hiện trong database

4. Thử xóa command manually để test permissions

### Step 4: Prepare Development Environment (5 phút)

#### Firmware:
```bash
cd d:\Projects\Lora\LM_LR_MESH

# Create new service files
mkdir -p src/services
touch src/services/firebase_command_poller.h
touch src/services/firebase_command_poller.cpp
```

#### Mobile App:
```bash
cd e:\mobile_app\kagri_app

# Create new service files
mkdir -p lib/services
touch lib/services/firebase_command_service.dart
touch lib/services/provisioning_session_service.dart

# Create new screens
mkdir -p lib/screens
touch lib/screens/gateway_selection_screen.dart
touch lib/screens/provisioning_progress_screen.dart
```

---

## 📝 Implementation Checklist

### PHASE 1: Firebase (✅ Done in 30 min)
- [x] Firebase rules applied
- [x] Test data created
- [x] Manual command tested
- [x] Dev environment ready

### PHASE 2: Firmware (🔧 1-2 days)
- [ ] Create `FirebaseCommandPoller` service
- [ ] Integrate into `GatewayApp`
- [ ] Add command handlers
- [ ] Test with hardware
- [ ] Verify command polling works
- [ ] Test start/stop provisioning

### PHASE 3: Mobile App (📱 2-3 days)
- [ ] Create `FirebaseCommandService`
- [ ] Create `ProvisioningSessionService`
- [ ] Build Gateway Selection Screen
- [ ] Build Provisioning Progress Screen
- [ ] Update HomeScreen FAB logic
- [ ] Test real-time updates

### PHASE 4: Integration Testing (✅ 1 day)
- [ ] End-to-end: App → Firebase → Gateway → Nodes
- [ ] Test multiple gateways
- [ ] Test multiple nodes
- [ ] Test error cases
- [ ] Polish UI/UX

---

## 🎯 What to Implement First?

### Option A: Firmware First (Recommended)
**Pros:**
- Backend ready, frontend can test immediately
- Can test with Firebase Console manually
- Foundation solid trước khi build UI

**Steps:**
1. ✅ Firebase setup (done)
2. 🔧 Implement `FirebaseCommandPoller` (Day 1)
3. 🔧 Test command polling with manual Firebase writes (Day 1)
4. 🔧 Integrate provisioning handlers (Day 2)
5. 📱 Build Mobile App UI (Day 3-4)
6. ✅ Integration test (Day 5)

### Option B: Mobile App First
**Pros:**
- UI/UX ready, backend sau
- Có thể mock Firebase responses
- Parallel development nếu có team

**Steps:**
1. ✅ Firebase setup (done)
2. 📱 Build UI screens (Day 1-2)
3. 📱 Implement Firebase services (Day 2-3)
4. 🔧 Implement Firmware (Day 3-4)
5. ✅ Integration test (Day 5)

### Option C: Parallel (Nếu có 2 người)
**Person A - Firmware:**
- Day 1-2: Command polling
- Day 3: Integration

**Person B - Mobile:**
- Day 1-2: UI screens
- Day 3: Integration

**Total: 3-4 days**

---

## 🧪 Testing Strategy

### Unit Testing (During Development)

**Firmware:**
```cpp
// Test command polling
1. Manually create command in Firebase
2. Wait for Gateway to poll (10s)
3. Verify serial log: "Processing command: ..."
4. Verify command moved to processing/
```

**Mobile App:**
```dart
// Test command sending
1. Click "Add Node"
2. Select gateway
3. Verify command created in Firebase
4. Check Firebase Console: commands/{mac}/pending/
```

### Integration Testing (After Both Done)

**Full Flow:**
```
1. Open app
2. Click FAB (+)
3. Select "Add Node"
4. Choose gateway
5. Start provisioning
6. Turn on physical node
7. Wait for node to join
8. Verify:
   - Node appears in routing table
   - Node data uploads to Firebase
   - UI updates real-time
```

---

## 📊 Progress Tracking

### Day 1 (Today)
- [x] Firebase structure created
- [x] Security rules applied
- [x] Test data added
- [ ] Start coding FirebaseCommandPoller (if choose Option A)
- [ ] OR start coding Mobile App UI (if choose Option B)

### Day 2
- [ ] Complete command polling service
- [ ] Test with manual Firebase writes
- [ ] Integrate into GatewayApp

### Day 3
- [ ] Add provisioning handlers
- [ ] Test start/stop commands
- [ ] Begin Mobile App UI

### Day 4
- [ ] Complete Mobile App screens
- [ ] Implement Firebase services
- [ ] Connect UI to Firebase

### Day 5
- [ ] Integration testing
- [ ] Bug fixes
- [ ] Polish UI/UX

### Day 6 (Buffer)
- [ ] Final testing
- [ ] Documentation
- [ ] Git commit & push

---

## 🚦 Decision Point

**Bạn muốn bắt đầu implement gì đầu tiên?**

### A. Firmware - Command Polling Service
Tôi sẽ:
1. Tạo file `firebase_command_poller.h` và `.cpp`
2. Implement polling logic
3. Add vào `gateway_app.cpp`
4. Guide bạn test với hardware

### B. Mobile App - UI Screens
Tôi sẽ:
1. Tạo Firebase services
2. Build Gateway Selection screen
3. Build Provisioning Progress screen
4. Update HomeScreen logic

### C. Tôi tự làm, chỉ cần document
OK! Tất cả document đã ready:
- `IMPLEMENTATION_PLAN.md` - Chi tiết từng bước
- `FIREBASE_STRUCTURE_MULTI_USER.md` - Database schema
- `firebase-rules.json` - Security rules
- Code examples trong IMPLEMENTATION_PLAN

---

## 💡 Tips for Success

1. **Test Frequently:**
   - Mỗi tính năng nhỏ → test ngay
   - Đừng code nhiều rồi test một lượt

2. **Use Firebase Console:**
   - Monitor real-time data changes
   - Manually create test commands
   - Verify structure đúng

3. **Serial Monitor:**
   - Luôn mở serial monitor khi test firmware
   - Check logs để debug

4. **Git Commits:**
   - Commit sau mỗi feature nhỏ
   - Easy to rollback nếu có bug

5. **Backup:**
   - Export Firebase data trước khi test
   - Có thể restore nếu cần

---

## 📞 Need Help?

Nếu gặp vấn đề:

1. **Firebase Issues:**
   - Check Security Rules
   - Verify user UID đúng
   - Check Firebase Console logs

2. **Firmware Issues:**
   - Check serial logs
   - Verify WiFi connected
   - Verify Firebase connected
   - Check heap memory

3. **Mobile App Issues:**
   - Check Firebase Auth
   - Verify user logged in
   - Check real-time listeners
   - Test with Firebase Console manually

---

## ✅ Ready to Start?

Bạn đã hoàn thành Phase 1 (Firebase Setup)!

**Next:** Chọn implementation strategy và bắt đầu code! 🚀
