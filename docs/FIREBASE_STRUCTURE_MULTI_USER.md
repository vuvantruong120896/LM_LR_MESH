# Firebase Structure - Multi-User with Command Queue

## Tổng quan

Document này mô tả cấu trúc Firebase Realtime Database đầy đủ cho hệ thống LoRa Mesh với hỗ trợ:
- Multi-user (mỗi user có gateways và nodes riêng)
- Command queue (Mobile App điều khiển Gateway qua Firebase)
- Provisioning nodes via Firebase (không cần BLE trực tiếp)

## Database Structure

```
firebase-root/
├── users/
│   └── {userUID}/                    // User ID from Firebase Auth
│       ├── profile/                  // User profile info
│       ├── gateways/                 // Gateways owned by user
│       ├── nodes/                    // Nodes in user's network
│       ├── commands/                 // Commands sent to gateways
│       ├── command_results/          // Command execution results
│       └── provisioning_sessions/    // Active provisioning sessions
│
├── sensor_data/                      // Historical sensor data (shared)
└── system/                           // System-wide data
```

---

## 1. User Profile

**Path:** `users/{userUID}/profile/`

```json
{
  "email": "user@example.com",
  "displayName": "John Doe",
  "createdAt": 1729234567000,
  "lastActive": 1729234567000
}
```

---

## 2. Gateways

**Path:** `users/{userUID}/gateways/{gatewayMAC}/`

### 2.1 Gateway Info

**Path:** `users/{userUID}/gateways/{gatewayMAC}/info/`

```json
{
  "mac": "AA:BB:CC:DD:EE:FF",
  "ip": "192.168.1.100",
  "firmware_version": "1.0.0",
  "address": "0xEEFF",
  "name": "Gateway Kitchen",        // User-defined name
  "location": "Kitchen",            // User-defined location
  "addedAt": 1729234567000
}
```

### 2.2 Gateway Status

**Path:** `users/{userUID}/gateways/{gatewayMAC}/status/`

```json
{
  "online": true,
  "wifi_connected": true,
  "wifi_rssi": -52,
  "firebase_connected": true,
  "uptime_seconds": 3600,
  "free_heap": 189456,
  "connected_nodes": 3,
  "total_packets_received": 1523,
  "total_packets_sent": 45,
  "timestamp": 1729234567000,
  
  // NEW: Command capability status
  "provisioning_active": false,
  "can_accept_commands": true,
  "current_operation": null,        // null | "provisioning" | "updating"
  "last_heartbeat": 1729234567000
}
```

### 2.3 Routing Table

**Path:** `users/{userUID}/gateways/{gatewayMAC}/routing_table/`

```json
{
  "node_count": 3,
  "updated_at": 1729234567000,
  "nodes": {
    "0xCC64": {
      "address": "0xCC64",
      "via": "0xCC64",
      "metric": 1,
      "role": 1,
      "rssi": -45,
      "snr": 10.5
    },
    "0x4F70": {
      "address": "0x4F70",
      "via": "0x4F70",
      "metric": 1,
      "role": 1,
      "rssi": -52,
      "snr": 9.2
    }
  }
}
```

---

## 3. Nodes

**Path:** `users/{userUID}/nodes/{nodeId}/`

### 3.1 Latest Data (Real-time)

**Path:** `users/{userUID}/nodes/{nodeId}/latest_data/`

```json
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1729234567,
  "rssi": -45,
  "snr": 10.5
}
```

### 3.2 Node Info (Metadata)

**Path:** `users/{userUID}/nodes/{nodeId}/info/`

```json
{
  "nodeId": "0xCC64",
  "name": "Sensor Living Room",     // User-defined
  "location": "Living Room",        // User-defined
  "type": "temperature_humidity",
  "gatewayMAC": "AA:BB:CC:DD:EE:FF", // Which gateway this node belongs to
  "addedAt": 1729234567000,
  "lastSeen": 1729234567000
}
```

---

## 4. Commands (NEW)

**Path:** `users/{userUID}/commands/{gatewayMAC}/`

### 4.1 Command Structure

Commands được tổ chức theo trạng thái:

```
commands/
└── {gatewayMAC}/
    ├── pending/       // Commands chờ Gateway xử lý
    ├── processing/    // Commands đang được thực thi
    ├── completed/     // Commands thành công
    └── failed/        // Commands thất bại
```

### 4.2 Command Types

#### A. Start Provisioning

**Path:** `users/{userUID}/commands/{gatewayMAC}/pending/{commandId}/`

```json
{
  "id": "cmd_1729234567001",
  "type": "start_provisioning",
  "params": {
    "durationMs": 600000,           // 10 minutes
    "maxNodes": 10                  // Max nodes to provision (0 = unlimited)
  },
  "timestamp": 1729234567001,
  "status": "pending",
  "priority": 1,                    // 0=high, 1=normal, 2=low
  "createdBy": "mobile_app",
  "deviceInfo": "iPhone 13 - iOS 16"
}
```

#### B. Stop Provisioning

```json
{
  "id": "cmd_1729234567002",
  "type": "stop_provisioning",
  "params": {},
  "timestamp": 1729234567002,
  "status": "pending",
  "priority": 0,                    // High priority to stop immediately
  "createdBy": "mobile_app"
}
```

#### C. Set Network Key (Future)

```json
{
  "id": "cmd_1729234567003",
  "type": "set_netkey",
  "params": {
    "networkKey": "0102030405060708090A0B0C0D0E0F10",
    "authToken": "1112131415161718",
    "networkId": 4660,
    "keyVersion": 2
  },
  "timestamp": 1729234567003,
  "status": "pending",
  "priority": 0
}
```

### 4.3 Command Status Flow

```
pending → processing → completed (success)
                    → failed (error)
```

**Processing Status:**
```json
{
  "id": "cmd_1729234567001",
  "type": "start_provisioning",
  "status": "processing",
  "startedAt": 1729234568000,
  "progress": {
    "nodes_discovered": 2,
    "time_elapsed_ms": 15000,
    "time_remaining_ms": 585000
  }
}
```

**Completed Status:**
```json
{
  "id": "cmd_1729234567001",
  "type": "start_provisioning",
  "status": "completed",
  "result": "success",
  "message": "Provisioning completed successfully",
  "startedAt": 1729234568000,
  "completedAt": 1729235168000,
  "executionTimeMs": 600000,
  "details": {
    "nodes_discovered": 5,
    "nodes_joined": 5,
    "nodes_failed": 0
  }
}
```

**Failed Status:**
```json
{
  "id": "cmd_1729234567001",
  "type": "start_provisioning",
  "status": "failed",
  "result": "failed",
  "message": "Gateway offline",
  "errorCode": "GATEWAY_OFFLINE",
  "startedAt": 1729234568000,
  "failedAt": 1729234568100,
  "executionTimeMs": 100
}
```

---

## 5. Command Results (Real-time Status)

**Path:** `users/{userUID}/command_results/{gatewayMAC}/`

Mobile App listen path này để nhận real-time updates:

```json
{
  "last_command_id": "cmd_1729234567001",
  "last_command_type": "start_provisioning",
  "status": "processing",
  "message": "Fast discovery mode activated",
  "timestamp": 1729234568000,
  "gateway_online": true,
  "last_poll": 1729234568500,
  "progress": {
    "nodes_discovered": 2,
    "time_remaining_ms": 585000,
    "discovered_nodes": [
      {
        "nodeId": "0x1234",
        "rssi": -45,
        "discovered_at": 1729234580000
      },
      {
        "nodeId": "0x5678",
        "rssi": -52,
        "discovered_at": 1729234595000
      }
    ]
  }
}
```

---

## 6. Provisioning Sessions (NEW)

**Path:** `users/{userUID}/provisioning_sessions/{gatewayMAC}/`

Track active provisioning sessions với chi tiết về nodes được phát hiện:

```json
{
  "sessionId": "session_1729234567001",
  "commandId": "cmd_1729234567001",
  "active": true,
  "started_at": 1729234567000,
  "duration_ms": 600000,
  "expires_at": 1729235167000,
  "nodes_discovered": [
    {
      "nodeId": "0x1234",
      "discovered_at": 1729234580000,
      "rssi": -45,
      "snr": 10.5,
      "status": "joined",              // "discovered" | "joining" | "joined" | "failed"
      "via": "0x1234",                 // Direct connection
      "metric": 1
    },
    {
      "nodeId": "0x5678",
      "discovered_at": 1729234595000,
      "rssi": -52,
      "snr": 9.0,
      "status": "joining",
      "via": "0x1234",                 // Via node 0x1234
      "metric": 2
    }
  ],
  "statistics": {
    "total_discovered": 2,
    "total_joined": 1,
    "total_failed": 0,
    "discovery_rate": 0.12            // Nodes per second
  }
}
```

---

## 7. Historical Sensor Data (Shared)

**Path:** `sensor_data/{nodeId}/{timestamp}/`

Dữ liệu lịch sử cho charts (không cần phân theo user vì đã có trong nodes):

```json
{
  "counter": 1234,
  "temperature": 25.5,
  "humidity": 65.0,
  "battery": 3.7,
  "timestamp": 1729234567,
  "rssi": -45,
  "snr": 10.5
}
```

---

## 8. System Events

**Path:** `users/{userUID}/events/{timestamp}/`

```json
{
  "type": "node_joined",
  "gateway_id": "AA:BB:CC:DD:EE:FF",
  "node_id": "0xCC64",
  "details": {
    "rssi": -45,
    "snr": 10.5,
    "metric": 1,
    "via_provisioning": true        // True if joined during provisioning
  },
  "timestamp": 1729234567000
}
```

**Event Types:**
- `node_joined` - Node mới vào mạng
- `node_left` - Node rời mạng
- `gateway_started` - Gateway khởi động
- `provisioning_started` - Bắt đầu provisioning
- `provisioning_completed` - Kết thúc provisioning
- `command_executed` - Command được thực thi

---

## Firebase Security Rules

```json
{
  "rules": {
    "users": {
      "$uid": {
        ".read": "$uid === auth.uid",
        ".write": "$uid === auth.uid",
        
        "profile": {
          ".validate": "newData.hasChildren(['email', 'displayName'])"
        },
        
        "gateways": {
          "$gatewayMAC": {
            "info": {
              ".validate": "newData.hasChildren(['mac', 'address'])"
            },
            "status": {
              ".write": "auth != null"
            },
            "routing_table": {
              ".write": "auth != null"
            }
          }
        },
        
        "nodes": {
          "$nodeId": {
            "latest_data": {
              ".write": "auth != null"
            },
            "info": {
              ".validate": "newData.hasChildren(['nodeId', 'gatewayMAC'])"
            }
          }
        },
        
        "commands": {
          "$gatewayMAC": {
            ".read": "$uid === auth.uid",
            ".write": "$uid === auth.uid",
            "pending": {
              "$commandId": {
                ".validate": "newData.hasChildren(['type', 'timestamp', 'status'])"
              }
            },
            "processing": {
              ".write": "auth != null"
            },
            "completed": {
              ".write": "auth != null"
            },
            "failed": {
              ".write": "auth != null"
            }
          }
        },
        
        "command_results": {
          "$gatewayMAC": {
            ".read": "$uid === auth.uid",
            ".write": "auth != null"
          }
        },
        
        "provisioning_sessions": {
          "$gatewayMAC": {
            ".read": "$uid === auth.uid",
            ".write": "auth != null"
          }
        },
        
        "events": {
          ".write": "auth != null",
          "$timestamp": {
            ".validate": "newData.hasChildren(['type', 'timestamp'])"
          }
        }
      }
    },
    
    "sensor_data": {
      ".read": "auth != null",
      ".write": "auth != null",
      "$nodeId": {
        "$timestamp": {
          ".validate": "newData.hasChildren(['temperature', 'humidity', 'timestamp'])"
        }
      }
    }
  }
}
```

---

## Data Flow Examples

### Example 1: Start Provisioning

```
1. Mobile App creates command:
   users/{uid}/commands/{gatewayMAC}/pending/{cmdId}

2. Gateway polls every 5-10 seconds:
   - Detects new command
   - Moves to processing/
   - Updates command_results/

3. Gateway executes:
   - Starts fast discovery mode
   - Broadcasts to all nodes
   - Updates provisioning_sessions/ with discovered nodes

4. Mobile App listens:
   - command_results/{gatewayMAC}
   - provisioning_sessions/{gatewayMAC}
   - Updates UI real-time

5. On completion:
   - Gateway moves command to completed/
   - Updates command_results/ with final status
   - Clears provisioning_sessions/
```

### Example 2: Node Discovery During Provisioning

```
1. Gateway discovers node:
   - Node sends HELLO packet
   - Gateway adds to routing table

2. Gateway updates Firebase:
   - provisioning_sessions/{gatewayMAC}/nodes_discovered
   - Adds node info: nodeId, rssi, snr, status

3. Mobile App displays:
   - "Found new node 0x1234 (RSSI: -45)"
   - Updates progress counter
   - Shows signal quality

4. When node joins:
   - Gateway updates node status to "joined"
   - Creates users/{uid}/nodes/{nodeId}/info
   - Triggers "node_joined" event
```

---

## Maintenance & Cleanup

### Auto-cleanup Rules (Implement in Cloud Functions)

1. **Old Commands:**
   - Delete completed commands older than 24 hours
   - Delete failed commands older than 1 hour

2. **Old Sessions:**
   - Delete provisioning sessions older than 1 hour

3. **Old Events:**
   - Keep only last 1000 events per user
   - Delete events older than 30 days

4. **Sensor Data:**
   - Keep raw data for 90 days
   - Aggregate to hourly averages after 7 days
   - Delete raw data after aggregation

---

## Testing Checklist

- [ ] User can write to their own commands/
- [ ] Gateway can read pending commands
- [ ] Gateway can move commands between states
- [ ] Gateway can update command_results/
- [ ] Mobile App receives real-time updates
- [ ] Provisioning sessions track nodes correctly
- [ ] Events are logged properly
- [ ] Security rules prevent unauthorized access
- [ ] Old data is cleaned up automatically

---

## Migration from Current Structure

### Current Paths:
```
gateways/{gatewayId}/info
gateways/{gatewayId}/status
nodes/{nodeId}/latest_data
sensor_data/{nodeId}/{timestamp}
```

### New Paths (Multi-user):
```
users/{uid}/gateways/{gatewayMAC}/info
users/{uid}/gateways/{gatewayMAC}/status
users/{uid}/nodes/{nodeId}/latest_data
sensor_data/{nodeId}/{timestamp}  (unchanged)
```

### Migration Steps:
1. Update Firebase client in firmware to use user-scoped paths
2. Update mobile app to write/read from user-scoped paths
3. Migrate existing data (optional - or start fresh)
4. Update security rules

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-10-18 | Initial multi-user structure with command queue |
