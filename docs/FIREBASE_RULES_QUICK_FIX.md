# Firebase Rules - Quick Fix

## TÓM TẮT NHANH

### Vấn đề
Lỗi: `[firebase_database/permission-denied]`

### Nguyên nhân
Rules cũ dùng path `/users/{uid}/nodes/` nhưng app thực tế dùng `/nodes/{uid}/`

### Giải pháp
1. Mở Firebase Console: https://console.firebase.google.com
2. Chọn project → Realtime Database → Rules
3. Copy file `docs/firebase-rules.json` đã sửa
4. Paste vào editor và click **Publish**
5. Reload app

## DATABASE STRUCTURE MỚI

```
/users/{uid}/                    ← User data
  ├─ profile/
  ├─ gateways/{mac}/
  ├─ commands/{mac}/
  └─ command_results/{mac}/

/nodes/{uid}/{mac}/{nodeId}/     ← Node data (CHÍNH)
  ├─ info/
  └─ latest_data/

/sensor_data/{uid}/{nodeId}/     ← Historical data

/gateways/{uid}/{mac}/           ← Gateway status
  ├─ status/
  └─ routing_table/
```

## KEY CHANGES

### Trước (❌ SAI)
```json
"users": {
  "$uid": {
    "nodes": {  ← Sai! nodes nằm TRONG users
      ...
    }
  }
}
```

### Sau (✅ ĐÚNG)
```json
"nodes": {  ← Đúng! nodes NGANG HÀNG với users
  "$uid": {
    "$gatewayMAC": {
      "$nodeId": {
        ...
      }
    }
  }
}
```

## TEST RULES

### ✅ PASS - User đọc nodes của mình
```
auth.uid = "user123"
READ: /nodes/user123/AA:BB:CC:DD:EE:FF/0xCC64/info
```

### ❌ FAIL - User đọc nodes của người khác
```
auth.uid = "user123"
READ: /nodes/user456/AA:BB:CC:DD:EE:FF/0xCC64/info
```

### ✅ PASS - Gateway ghi sensor data
```
auth != null (Gateway authenticated)
WRITE: /nodes/user123/AA:BB:CC:DD:EE:FF/0xCC64/latest_data
```

## PERMISSIONS

| Path | User Read | User Write | Gateway Write |
|------|-----------|------------|---------------|
| `/users/{uid}/profile` | ✅ Own only | ✅ Own only | ❌ |
| `/users/{uid}/gateways` | ✅ Own only | ✅ Own only | ✅ Status |
| `/users/{uid}/commands` | ✅ Own only | ✅ Pending | ✅ Process |
| `/nodes/{uid}/` | ✅ Own only | ✅ Own only | ✅ Latest |
| `/sensor_data/{uid}/` | ✅ Own only | ❌ | ✅ |
| `/gateways/{uid}/` | ✅ Own only | ❌ | ✅ Status |

## VALIDATION

Commands phải có: `type`, `timestamp`
Sensor data phải có: `temperature`, `humidity`, `timestamp`
Node info phải có: `nodeId`, `gatewayMAC`

## ĐỌC THÊM
Xem file đầy đủ: `docs/FIREBASE_RULES_UPDATE_GUIDE.md`
