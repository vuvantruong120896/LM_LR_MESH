# Hướng dẫn Cập nhật Firebase Rules

## Vấn đề
Khi thêm firebase-rules.json vào Firebase, gặp lỗi:
```
[firebase_database/permission-denied] Client doesn't have permission to access the desired data.
```

## Nguyên nhân
Firebase Rules cũ cấu trúc path `/users/{uid}/nodes/...` nhưng app và firmware đang dùng path `/nodes/{uid}/...`. Database structure thực tế là:

```
/users/{uid}/
  - profile/
  - gateways/{mac}/
  - commands/{mac}/
  - command_results/{mac}/
  - events/
  
/nodes/{uid}/{gatewayMAC}/{nodeId}/
  - info/
  - latest_data/
  
/sensor_data/{uid}/{nodeId}/{timestamp}/

/gateways/{uid}/{gatewayMAC}/
  - status/
  - routing_table/
```

## Giải pháp - Áp dụng Firebase Rules mới

### Bước 1: Mở Firebase Console
1. Truy cập https://console.firebase.google.com
2. Chọn project của bạn
3. Vào **Realtime Database** từ menu bên trái
4. Chọn tab **Rules**

### Bước 2: Copy Rules mới
Copy toàn bộ nội dung file `firebase-rules.json` đã được sửa

### Bước 3: Paste vào Firebase Console
1. Xóa toàn bộ rules cũ trong editor
2. Paste rules mới vào
3. Click **Publish**

### Bước 4: Test
1. Chạy app trên điện thoại
2. Login với tài khoản
3. Kiểm tra Home Screen có hiển thị devices không
4. Không còn lỗi permission-denied

## Firebase Rules Structure Chi tiết

### 1. Users Path (`/users/{uid}/`)
**Các collection:**
- `profile` - Thông tin user (email, displayName)
- `gateways/{mac}/info` - Thông tin Gateway của user
- `gateways/{mac}/status` - Gateway có thể ghi
- `gateways/{mac}/routing_table` - Gateway có thể ghi
- `commands/{mac}/pending` - User tạo commands
- `commands/{mac}/processing` - Gateway có thể ghi
- `commands/{mac}/completed` - Gateway có thể ghi
- `commands/{mac}/failed` - Gateway có thể ghi
- `command_results/{mac}` - Gateway ghi, User đọc
- `provisioning_sessions/{mac}` - User đọc, Gateway ghi
- `events` - User và Gateway đều ghi được

**Rules:**
```json
".read": "$uid === auth.uid",
".write": "$uid === auth.uid || auth != null"
```

### 2. Nodes Path (`/nodes/{uid}/`)
**Structure:** `/nodes/{uid}/{gatewayMAC}/{nodeId}/`
- `info/` - Thông tin node (nodeId, gatewayMAC, name, etc.)
- `latest_data/` - Sensor data mới nhất

**Rules:**
```json
".read": "$uid === auth.uid",
".write": "$uid === auth.uid || auth != null"
```

**Lưu ý:**
- User chỉ đọc được nodes của chính mình
- Gateway (authenticated) có thể ghi latest_data
- Gateway có thể tạo node info khi provisioning

### 3. Sensor Data Path (`/sensor_data/{uid}/`)
**Structure:** `/sensor_data/{uid}/{nodeId}/{timestamp}/`
- Lưu trữ dữ liệu lịch sử
- Timestamp là milliseconds since epoch

**Rules:**
```json
".read": "$uid === auth.uid",
".write": "$uid === auth.uid || auth != null",
".validate": "newData.hasChildren(['temperature', 'humidity', 'timestamp'])"
```

### 4. Gateways Path (`/gateways/{uid}/`)
**Structure:** `/gateways/{uid}/{gatewayMAC}/`
- `status/` - Online status, last_seen, firmware version
- `routing_table/` - Mesh network routing information

**Rules:**
```json
".read": "$uid === auth.uid",
".write": "$uid === auth.uid || auth != null"
```

## Quyền truy cập

### User (Mobile App)
- ✅ **Đọc**: Profile, gateways, commands, command_results của chính mình
- ✅ **Ghi**: Profile, gateways/info, commands/pending
- ✅ **Đọc**: Nodes và sensor_data của chính mình
- ❌ **Không đọc được**: Dữ liệu của user khác

### Gateway (ESP32)
- ✅ **Đọc**: commands/pending của owner
- ✅ **Ghi**: 
  - commands/processing, completed, failed
  - command_results
  - nodes/{uid}/{gatewayMAC}/{nodeId}/latest_data
  - sensor_data/{uid}/{nodeId}/{timestamp}
  - gateways/{uid}/{gatewayMAC}/status
  - gateways/{uid}/{gatewayMAC}/routing_table
- ✅ **Ghi**: node info khi provisioning

### Anonymous (Not logged in)
- ❌ **Không có quyền truy cập**

## Testing Rules

### Test 1: User đọc nodes của mình
```javascript
// PASS
auth.uid = "user123"
/nodes/user123/AA:BB:CC:DD:EE:FF/0xCC64/info  // READ
```

### Test 2: User đọc nodes của người khác
```javascript
// FAIL - Permission Denied
auth.uid = "user123"
/nodes/user456/AA:BB:CC:DD:EE:FF/0xCC64/info  // READ
```

### Test 3: Gateway ghi sensor data
```javascript
// PASS
auth != null (Gateway authenticated)
/nodes/user123/AA:BB:CC:DD:EE:FF/0xCC64/latest_data  // WRITE
```

### Test 4: Gateway ghi command result
```javascript
// PASS
auth != null
/users/user123/command_results/AA:BB:CC:DD:EE:FF/cmd_12345  // WRITE
```

## Validation Rules

### Command trong pending queue phải có:
- `type` - Loại command (start_provisioning, stop_provisioning, etc.)
- `timestamp` - Thời điểm tạo command

### Sensor data phải có:
- `temperature`
- `humidity`
- `timestamp`

### Node info phải có:
- `nodeId`
- `gatewayMAC`

### User profile phải có:
- `email`
- `displayName`

## Troubleshooting

### Lỗi: "Permission denied" khi đọc nodes
**Nguyên nhân:** Rules không khớp với path structure
**Giải pháp:** Đảm bảo path trong code là `/nodes/{uid}/...` và rules đã cập nhật

### Lỗi: Gateway không ghi được sensor data
**Nguyên nhân:** Gateway chưa authenticated với Firebase
**Giải pháp:** Kiểm tra Firebase credentials trong firmware

### Lỗi: User đọc được data của user khác
**Nguyên nhân:** Rules quá mở (dùng `auth != null` cho read)
**Giải pháp:** Thay bằng `$uid === auth.uid` cho user data

## Security Best Practices

1. **Multi-tenancy**: Mỗi user chỉ truy cập được data của mình
2. **Gateway Authentication**: Gateway phải authenticate trước khi ghi
3. **Validation**: Validate cấu trúc data trước khi cho phép ghi
4. **Read/Write riêng biệt**: Không dùng chung rule cho read và write
5. **Least Privilege**: Gateway chỉ có quyền ghi, không đọc user profile

## Notes
- Rules này support cả BLE provisioning và Firebase remote provisioning
- Gateway có thể ghi vào multiple user paths khi serving multiple users
- Command results có timestamp để cleanup old data
- Sensor data có thể được archived hoặc deleted sau một thời gian

## Version History
- v1.0 (Oct 2024) - Initial rules với `/users/{uid}/nodes/`
- v2.0 (Dec 2024) - Updated rules với `/nodes/{uid}/` structure
- v2.1 (Dec 2024) - Added validation rules và command queue support
