# Firebase Command Queue Architecture

## Tổng quan

Document này mô tả kiến trúc command queue cho việc điều khiển Gateway từ Mobile App thông qua Firebase Realtime Database. Command queue cho phép Mobile App gửi các lệnh như Start Provisioning, Stop Provisioning, và Set Network Key đến Gateway một cách bất đồng bộ.

## Luồng dữ liệu

```
Mobile App → Firebase Commands → Gateway polls → Execute → Firebase Results → Mobile App
```

## Database Schema

### 1. Commands Queue Structure

Path: `/commands/{gatewayId}/`

```json
{
  "commands": {
    "0xABCD": {
      "pending": {
        "cmd_1697500000001": {
          "type": "start_provisioning",
          "params": {
            "durationMs": 600000,
            "maxSessions": 4
          },
          "timestamp": 1697500000001,
          "status": "pending",
          "priority": 1
        },
        "cmd_1697500060001": {
          "type": "stop_provisioning",
          "params": {},
          "timestamp": 1697500060001,
          "status": "pending",
          "priority": 2
        },
        "cmd_1697500120001": {
          "type": "set_netkey",
          "params": {
            "networkKey": "0102030405060708090A0B0C0D0E0F10",
            "authToken": "1112131415161718",
            "networkId": 4660,
            "keyVersion": 2
          },
          "timestamp": 1697500120001,
          "status": "pending",
          "priority": 0
        }
      },
      "processing": {
        "cmd_1697499999001": {
          "type": "start_provisioning",
          "params": {
            "durationMs": 300000,
            "maxSessions": 2
          },
          "timestamp": 1697499999001,
          "status": "processing",
          "startedAt": 1697500000000
        }
      },
      "completed": {
        "cmd_1697499900001": {
          "type": "set_netkey",
          "params": {
            "keyVersion": 1
          },
          "result": "success",
          "message": "Network key distributed to 5 nodes",
          "timestamp": 1697499900001,
          "completedAt": 1697499910001,
          "executionTimeMs": 10000
        },
        "cmd_1697499800001": {
          "type": "start_provisioning",
          "result": "failed",
          "message": "Already in provisioning mode",
          "timestamp": 1697499800001,
          "completedAt": 1697499801001,
          "executionTimeMs": 1000
        }
      },
      "failed": {
        "cmd_1697499700001": {
          "type": "unknown_command",
          "result": "failed",
          "message": "Unknown command type",
          "timestamp": 1697499700001,
          "failedAt": 1697499701001,
          "errorCode": "UNKNOWN_COMMAND"
        }
      }
    }
  }
}
```

### 2. Command Results (Current Status)

Path: `/command_results/{gatewayId}/`

```json
{
  "command_results": {
    "0xABCD": {
      "last_command_id": "cmd_1697500000001",
      "last_command_type": "start_provisioning",
      "status": "success",
      "message": "Fast discovery mode activated for 600000ms",
      "timestamp": 1697500000001,
      "gateway_online": true,
      "last_poll": 1697500005000
    }
  }
}
```

### 3. Gateway Status (for Command Eligibility)

Path: `/gateway_status/{gatewayId}/`

```json
{
  "gateway_status": {
    "0xABCD": {
      "online": true,
      "provisioning_active": false,
      "connected_nodes": 5,
      "can_accept_commands": true,
      "last_heartbeat": 1697500005000,
      "current_operation": null
    }
  }
}
```

## Command Types

### 1. Start Provisioning

**Command Type:** `start_provisioning`

**Purpose:** Kích hoạt fast discovery mode để cho phép nodes mới join network

**Parameters:**
```json
{
  "type": "start_provisioning",
  "params": {
    "durationMs": 600000,        // Duration in milliseconds (default: 600000 = 10 minutes)
    "maxSessions": 4             // Max concurrent provisioning sessions (default: 4)
  },
  "timestamp": 1697500000001,
  "status": "pending",
  "priority": 1                  // 0=high, 1=normal, 2=low
}
```

**Expected Result:**
```json
{
  "result": "success",
  "message": "Fast discovery mode activated for 600000ms",
  "details": {
    "mode": "fast_discovery",
    "hello_interval": 30,        // seconds
    "duration_remaining_ms": 600000,
    "nodes_discovered": 0
  }
}
```

**Gateway Actions:**
1. Activate fast discovery mode on Gateway
2. Broadcast `HELLO_MODE_FAST_DISCOVERY` to all nodes
3. Start discovery timer
4. Update status to Firebase

### 2. Stop Provisioning

**Command Type:** `stop_provisioning`

**Purpose:** Dừng fast discovery mode và quay về normal operation mode

**Parameters:**
```json
{
  "type": "stop_provisioning",
  "params": {},                  // No parameters needed
  "timestamp": 1697500060001,
  "status": "pending",
  "priority": 2
}
```

**Expected Result:**
```json
{
  "result": "success",
  "message": "Returned to normal operation mode",
  "details": {
    "mode": "normal",
    "hello_interval": 120,       // seconds
    "nodes_provisioned": 3,      // Total nodes discovered during session
    "session_duration_ms": 60000
  }
}
```

**Gateway Actions:**
1. Stop fast discovery mode
2. Broadcast `HELLO_MODE_NORMAL` to all nodes
3. Reset hello interval to 120s
4. Update status to Firebase

### 3. Set Network Key

**Command Type:** `set_netkey`

**Purpose:** Cập nhật và phân phối network key mới cho toàn bộ nodes

**Parameters:**
```json
{
  "type": "set_netkey",
  "params": {
    "networkKey": "0102030405060708090A0B0C0D0E0F10",  // 16 bytes hex string
    "authToken": "1112131415161718",                   // 8 bytes hex string
    "networkId": 4660,                                 // uint16_t
    "keyVersion": 2                                    // uint8_t
  },
  "timestamp": 1697500120001,
  "status": "pending",
  "priority": 0                   // High priority
}
```

**Expected Result:**
```json
{
  "result": "success",
  "message": "Network key distributed to 5 nodes",
  "details": {
    "key_version": 2,
    "network_id": 4660,
    "nodes_updated": 5,
    "nodes_failed": 0,
    "distribution_time_ms": 8500,
    "failed_nodes": []
  }
}
```

**Gateway Actions:**
1. Validate network key format (16 bytes)
2. Save to NVS storage
3. Update local MeshSecurityService
4. Distribute to all nodes via `NetkeyDistributionService`
5. Wait for confirmations
6. Update status to Firebase

## Command Status Flow

### Status Transitions

```
pending → processing → completed (success)
                    → failed (error)
```

### 1. Pending State

Command được Mobile App tạo và đang chờ Gateway xử lý.

```json
{
  "status": "pending",
  "timestamp": 1697500000001
}
```

### 2. Processing State

Gateway đang thực thi command.

```json
{
  "status": "processing",
  "startedAt": 1697500001000,
  "progress": 50                // Optional: percentage complete
}
```

### 3. Completed State (Success)

Command thực thi thành công.

```json
{
  "status": "completed",
  "result": "success",
  "message": "Command executed successfully",
  "completedAt": 1697500010000,
  "executionTimeMs": 9000
}
```

### 4. Failed State

Command thất bại hoặc không thể thực thi.

```json
{
  "status": "failed",
  "result": "failed",
  "message": "Gateway not ready for provisioning",
  "errorCode": "NOT_READY",
  "failedAt": 1697500002000,
  "executionTimeMs": 1000
}
```

## Firebase Security Rules

### Read/Write Permissions

```json
{
  "rules": {
    "commands": {
      "$gatewayId": {
        "pending": {
          ".read": "auth != null",
          ".write": "auth != null"
        },
        "processing": {
          ".read": "auth != null",
          ".write": "auth != null"
        },
        "completed": {
          ".read": "auth != null",
          ".write": "auth != null"
        },
        "failed": {
          ".read": "auth != null",
          ".write": "auth != null"
        }
      }
    },
    "command_results": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null"
      }
    },
    "gateway_status": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null"
      }
    }
  }
}
```

## Command Priority System

Commands có thể được ưu tiên xử lý theo mức độ quan trọng:

- **Priority 0 (High):** Set Network Key, Emergency commands
- **Priority 1 (Normal):** Start Provisioning, Configuration changes
- **Priority 2 (Low):** Status queries, Non-critical operations

Gateway sẽ xử lý commands theo thứ tự priority (thấp → cao), sau đó theo timestamp.

## Error Codes

| Error Code | Description | Retry Possible |
|------------|-------------|----------------|
| `UNKNOWN_COMMAND` | Command type không được hỗ trợ | No |
| `INVALID_PARAMS` | Parameters không hợp lệ | No |
| `NOT_READY` | Gateway chưa sẵn sàng | Yes |
| `ALREADY_RUNNING` | Operation đã đang chạy | Yes (after completion) |
| `NETWORK_ERROR` | Lỗi kết nối mạng | Yes |
| `TIMEOUT` | Command execution timeout | Yes |
| `PERMISSION_DENIED` | Không có quyền thực thi | No |
| `RESOURCE_BUSY` | Tài nguyên đang được sử dụng | Yes |

## Command Expiration

Commands tự động expire sau khoảng thời gian nhất định:

- **Pending commands:** Expire sau 5 phút nếu không được xử lý
- **Processing commands:** Timeout sau 2 phút nếu không hoàn thành
- **Completed/Failed commands:** Tự động xóa sau 24 giờ

## Best Practices

### For Mobile App Developers

1. **Check Gateway Status** trước khi gửi command
2. **Use unique command IDs** (timestamp + random)
3. **Listen for results** real-time để cập nhật UI
4. **Handle errors gracefully** với retry logic
5. **Clean up old commands** định kỳ

### For Gateway Developers

1. **Poll frequently** (5-10 seconds) để responsive
2. **Process commands atomically** tránh race conditions
3. **Update status immediately** khi bắt đầu/kết thúc
4. **Validate parameters** trước khi thực thi
5. **Handle network failures** với retry logic

## Example Usage Flow

### Mobile App → Firebase

```javascript
// 1. Check gateway online
const gatewayStatus = await firebase.database()
  .ref(`gateway_status/0xABCD`)
  .once('value');

if (!gatewayStatus.val().online) {
  showError("Gateway offline");
  return;
}

// 2. Create command
const commandId = `cmd_${Date.now()}`;
await firebase.database()
  .ref(`commands/0xABCD/pending/${commandId}`)
  .set({
    type: "start_provisioning",
    params: {
      durationMs: 600000,
      maxSessions: 4
    },
    timestamp: Date.now(),
    status: "pending",
    priority: 1
  });

// 3. Listen for result
firebase.database()
  .ref(`command_results/0xABCD`)
  .on('value', (snapshot) => {
    const result = snapshot.val();
    if (result.last_command_id === commandId) {
      showNotification(result.status, result.message);
    }
  });
```

### Gateway → Firebase

```cpp
// 1. Poll for pending commands
String path = String("commands/") + gatewayId + "/pending";
if (Firebase.RTDB.getJSON(&fbdo, path.c_str())) {
    // Parse and process commands
}

// 2. Move to processing
Firebase.RTDB.setJSON(&fbdo, processingPath, &cmdData);
Firebase.RTDB.deleteNode(&fbdo, pendingPath);

// 3. Execute command
bool success = executeCommand(cmd);

// 4. Update result
FirebaseJson result;
result.set("result", success ? "success" : "failed");
result.set("message", message);
result.set("completedAt", getCurrentTimestamp());
Firebase.RTDB.setJSON(&fbdo, completedPath, &result);

// 5. Update command_results
Firebase.RTDB.setJSON(&fbdo, resultPath, &statusJson);
```

## Testing Checklist

- [ ] Mobile App can write commands to Firebase
- [ ] Gateway polls and detects commands within 5 seconds
- [ ] Commands execute correctly (start/stop/netkey)
- [ ] Status updates appear in command_results
- [ ] Failed commands show proper error messages
- [ ] Expired commands are cleaned up automatically
- [ ] Priority system works correctly
- [ ] Network failures are handled gracefully
- [ ] Multiple concurrent commands are serialized
- [ ] Real-time listeners update Mobile UI

## Migration Notes

### From UART to Firebase

Các UART commands cũ mapping sang Firebase commands:

| UART Command | Firebase Command | Changes |
|--------------|------------------|---------|
| `UART_CMD_START_PROVISIONING` (0x15) | `start_provisioning` | Same params |
| `UART_CMD_STOP_PROVISIONING` (0x16) | `stop_provisioning` | No params |
| `UART_CMD_SET_NETKEY` (0x14) | `set_netkey` | Hex string format |

## Related Documentation

- [Firebase Integration](FIREBASE_INTEGRATION.md)
- [Provisioning UART Commands](PROVISIONING_UART_COMMANDS.md) (Legacy)
- [Network Key Distribution](NETKEY_DISTRIBUTION.md)

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-10-17 | Initial command queue architecture |
