# 🔐 Assign Netkey Implementation via Firebase

## 📋 Tổng quan

Tài liệu này mô tả việc implement tính năng **cấp netkey từ xa** thông qua Firebase command queue.

## 🎯 Yêu cầu

Gateway chỉ cấp netkey khi nhận được command từ Mobile App qua Firebase, **KHÔNG** tự động cấp netkey khi node mới join.

## 🔄 Luồng hoạt động

```
Mobile App → Firebase → Gateway → Nodes
    ↓           ↓          ↓
  Command   Realtime   Load netkey
  Service   Database   from NVS
```

### Chi tiết từng bước:

1. **App gửi command:**
   - User nhấn nút "Cấp Netkey" trong màn hình Provisioning
   - App gọi `FirebaseCommandService.sendNetkeyCommand()`
   - Command được tạo với type: `"assign_netkey"`
   - Lưu vào Firebase: `users/{uid}/commands/{gatewayMAC}/pending/{commandId}`

2. **Firebase lưu trữ:**
   ```json
   {
     "id": "cmd_1234567890",
     "type": "assign_netkey",
     "params": {},
     "priority": 8,
     "timestamp": 1729319015000,
     "status": "pending"
   }
   ```

3. **Gateway polling:**
   - `FirebaseCommandPoller` polls Firebase mỗi 5 giây
   - Phát hiện command mới trong `pending/` queue
   - Move command sang `processing/`
   - Gọi `GatewayApp::handleCommand()`

4. **Gateway xử lý:**
   - Switch case phát hiện type = `"assign_netkey"`
   - Gọi `handleAssignNetkey(cmd)`
   - **Load netkey từ NVS** (đã lưu khi đăng ký Gateway)
   - Cấp netkey cho tất cả nodes trong routing table
   - Update status → `completed/` hoặc `failed/`

5. **App nhận kết quả:**
   - Stream listener nhận update từ Firebase
   - Hiển thị progress/success/error cho user

## 📝 Code Changes

### 1. Header File: `gateway_app.h`

**Thêm khai báo method:**

```cpp
void handleAssignNetkey(const FirebaseCommandPoller::Command& cmd);
```

**Vị trí:** Sau `handleStopProvisioning()` (line 89)

### 2. Command Handler: `gateway_app.cpp`

**Thêm case xử lý command:**

```cpp
} else if (cmd.type == "assign_netkey") {
    handleAssignNetkey(cmd);
} else {
```

**Vị trí:** Line 392-396 (trong `GatewayApp::run()`)

### 3. Implementation: `gateway_app.cpp`

**Method mới `handleAssignNetkey()`:**

**Vị trí:** Sau `handleStopProvisioning()` (line ~1505)

**Logic:**

```cpp
void GatewayApp::handleAssignNetkey(const FirebaseCommandPoller::Command& cmd) {
    // 1. Load network config từ NVS
    NetworkConfig cfg;
    if (!NVSStorageService::loadNetworkConfig(cfg)) {
        → moveToFailed: "NO_NETKEY"
        return;
    }
    
    // 2. Validate config đã được khởi tạo
    if (!cfg.initialized) {
        → moveToFailed: "NETKEY_NOT_INITIALIZED"
        return;
    }
    
    // 3. Update Gateway's local netkey
    NetkeyDistributionService::updateLocalNetworkKey(...)
    
    // 4. Get all nodes from routing table
    size_t routingTableSize = RoutingTableService::routingTableSize();
    NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
    
    // 5. Distribute netkey to all nodes
    bool success = NetkeyDistributionService::distributeNetkeyToAllNodes(
        cfg.networkKey,
        cfg.authToken,
        cfg.networkId,
        cfg.keyVersion,
        nodes,
        routingTableSize
    );
    
    // 6. Update command status
    if (success) {
        → moveToCompleted: "Netkey distributed to X of Y nodes"
    } else {
        → moveToFailed: "PARTIAL_FAILURE"
    }
}
```

## 🔑 Netkey Storage

**Gateway lưu netkey trong NVS khi:**

1. **Đăng ký Gateway lần đầu:**
   - App gửi UID user
   - Gateway sinh netkey từ: `Hash(UID + MAC_ADDRESS)`
   - Lưu vào NVS: `NVSStorageService::saveNetworkConfig()`

2. **Nhận netkey từ UART:**
   - Admin gửi netkey qua USB cable
   - Gateway lưu vào NVS
   - Callback: `GatewayApp::onNetkeyReceived()`

**Cấu trúc NetworkConfig:**

```cpp
struct NetworkConfig {
    uint8_t networkKey[16];    // AES-128 key
    uint8_t authToken[16];     // Authentication token
    uint16_t networkId;        // Network ID (0x0000 - 0xFFFF)
    uint8_t keyVersion;        // Key version for rotation
    uint32_t timestamp;        // Last update timestamp
    bool initialized;          // Config valid flag
};
```

## 📊 So sánh với UART Flow

| Aspect | UART Flow | Firebase Flow |
|--------|-----------|---------------|
| **Trigger** | Admin cắm USB cable | User nhấn nút trong App |
| **Netkey source** | Nhận từ UART | Load từ NVS |
| **Command delivery** | Serial protocol | Firebase Realtime DB |
| **Response** | UART confirm message | Firebase status update |
| **Use case** | Initial setup, testing | Remote provisioning |

**Common logic:** Cả 2 đều sử dụng `NetkeyDistributionService::distributeNetkeyToAllNodes()`

## ✅ Testing Checklist

### Pre-conditions:
- [ ] Gateway đã được đăng ký (netkey có trong NVS)
- [ ] Gateway đã kết nối WiFi + Firebase
- [ ] Đã chạy `start_provisioning` và phát hiện nodes
- [ ] Có ít nhất 1 node trong routing table

### Test Steps:
1. [ ] Mở App → màn hình Gateway Selection
2. [ ] Chọn Gateway → màn hình Provisioning
3. [ ] Nhấn "Thêm Nodes" → đợi 5 phút
4. [ ] Xác nhận có nodes trong danh sách
5. [ ] Nhấn "Cấp Netkey"
6. [ ] Kiểm tra logs:
   ```
   ╔════════════════════════════════════════════════════════════╗
   ║  ASSIGN NETKEY via Firebase Command                       ║
   ╚════════════════════════════════════════════════════════════╝
   ✅ Loaded netkey from NVS
   ✅ Gateway local network key updated successfully
   Found X nodes in routing table, distributing netkey...
   Network-wide netkey distribution: SUCCESS
   ╔════════════════════════════════════════════════════════════╗
   ║  Netkey Distribution Completed                             ║
   ║  Nodes: X/X successful                                     ║
   ╚════════════════════════════════════════════════════════════╝
   ```

### Expected Results:
- [ ] App hiển thị "Cấp netkey thành công"
- [ ] Firebase command status = "completed"
- [ ] Gateway logs show successful distribution
- [ ] Nodes nhận được netkey (kiểm tra node logs)

### Error Cases:

#### 1. No netkey in NVS
```
❌ Failed to load network config from NVS - Gateway not registered?
→ Firebase: status="failed", error="NO_NETKEY"
```

#### 2. No nodes in routing table
```
⚠️  No nodes in routing table - nothing to provision
→ Firebase: status="failed", error="NO_NODES"
```

#### 3. Partial failure
```
Network-wide netkey distribution: PARTIAL/FAILED
→ Firebase: status="failed", error="PARTIAL_FAILURE", 
           message="Netkey distributed to X of Y nodes"
```

## 🐛 Debugging

### Enable verbose logging:

```cpp
// In gateway_app.cpp
#define TAG "GatewayApp"
esp_log_level_set(TAG, ESP_LOG_DEBUG);
```

### Check Firebase command:

```bash
# Firebase Console → Realtime Database
users/{uid}/commands/{gatewayMAC}/pending/
users/{uid}/commands/{gatewayMAC}/processing/
users/{uid}/commands/{gatewayMAC}/completed/
users/{uid}/commands/{gatewayMAC}/failed/
```

### Monitor serial output:

```bash
platformio device monitor -e esp32-gateway
```

## 📚 Related Files

- **Gateway Firmware:**
  - `src/application/app_gateway/gateway_app.h` (line 88-90)
  - `src/application/app_gateway/gateway_app.cpp` (line 392-396, 1505-1637)
  
- **Mobile App:**
  - `lib/services/firebase_command_service.dart` (sendNetkeyCommand)
  - `lib/screens/provisioning_progress_screen.dart` (UI + listener)
  
- **Services:**
  - `src/components/lora_mesh_manager/src/services/NetkeyDistributionService.cpp`
  - `src/application/app_gateway/firebase_command_poller.cpp`

## 🎓 Architecture Notes

### Why load from NVS instead of Firebase?

1. **Security:** Netkey không nên truyền qua Firebase (even encrypted)
2. **Performance:** Tránh latency của Firebase query
3. **Reliability:** Netkey vẫn có nếu Firebase offline
4. **Simplicity:** App không cần quản lý netkey

### Why not auto-assign?

1. **User control:** Admin kiểm soát khi nào cấp netkey
2. **Network planning:** Có thể scan nodes trước khi provision
3. **Security:** Tránh cấp nhầm netkey cho rogue devices
4. **Debugging:** Dễ debug khi có bước riêng biệt

## 📈 Future Improvements

- [ ] Selective netkey assignment (chỉ cấp cho 1 số nodes)
- [ ] Netkey rotation/update command
- [ ] Progress feedback during distribution (X/Y nodes done)
- [ ] Retry failed nodes automatically
- [ ] Batch size configuration (cấp từng nhóm thay vì all-at-once)

---

**Last Updated:** 2025-10-19  
**Author:** GitHub Copilot  
**Version:** 1.0.0
