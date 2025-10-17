# Firebase Console Setup Guide - Command Queue

## Tổng quan

Guide này hướng dẫn từng bước để setup Firebase Realtime Database cho command queue system.

---

## Bước 1: Truy cập Firebase Console

### 1.1. Đăng nhập Firebase Console

1. Mở trình duyệt và truy cập: https://console.firebase.google.com/
2. Đăng nhập bằng Google Account của bạn
3. Chọn project **`kagri-iot`** (hoặc project bạn đang dùng)

### 1.2. Mở Realtime Database

1. Từ sidebar bên trái, chọn **"Build"** → **"Realtime Database"**
2. Bạn sẽ thấy URL database: `https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/`

---

## Bước 2: Import Database Schema

### Option A: Import JSON File (Recommended)

#### 2.1. Prepare JSON File

Sử dụng file `firebase_command_queue_schema.json` đã có trong project:
```
docs/firebase_command_queue_schema.json
```

#### 2.2. Import Steps

1. **Vào Database Tab:**
   - Click vào tab **"Data"** trong Realtime Database

2. **Chọn Root Node:**
   - Click vào root node (ký hiệu `/`)
   - Hoặc click vào database URL ở đầu trang

3. **Import JSON:**
   - Click vào icon **3 chấm dọc** (⋮) bên phải
   - Chọn **"Import JSON"**
   
   ![Import JSON Location](https://i.imgur.com/example.png)

4. **Upload File:**
   - Click **"Browse"** và chọn file `firebase_command_queue_schema.json`
   - Hoặc kéo thả file vào dialog

5. **Confirm Import:**
   - Click **"Import"**
   - Chờ vài giây để Firebase xử lý

6. **Verify:**
   - Sau khi import, bạn sẽ thấy structure mới trong database:
   ```
   /
   ├── commands/
   │   └── 0xABCD/
   ├── command_results/
   ├── gateway_status/
   └── _metadata/
   ```

### Option B: Manual Structure Creation

Nếu không muốn import file, tạo thủ công:

#### 2.3. Create Commands Structure

1. Click vào root node `/`
2. Click button **"+"** để thêm child
3. Tạo các nodes sau:

```
Root (/)
│
├─ commands
│  └─ 0xABCD
│     ├─ pending
│     ├─ processing
│     ├─ completed
│     └─ failed
│
├─ command_results
│  └─ 0xABCD
│
└─ gateway_status
   └─ 0xABCD
```

#### 2.4. Add Sample Command (Manual)

1. Navigate to: `/commands/0xABCD/pending/`
2. Click **"+"** button
3. Name: `cmd_test_001`
4. Click **"+"** để thêm fields:

**Type:** String
```
type: "start_provisioning"
```

**Params:** Object → Add children:
```
durationMs: 600000 (Number)
maxSessions: 4 (Number)
```

**Timestamp:** Number
```
timestamp: 1697500000000
```

**Status:** String
```
status: "pending"
```

**Priority:** Number
```
priority: 1
```

5. Click **"Add"** để save

---

## Bước 3: Configure Security Rules

### 3.1. Open Rules Tab

1. Trong **Realtime Database** page
2. Click tab **"Rules"**
3. Bạn sẽ thấy editor với rules hiện tại

### 3.2. Replace with Command Queue Rules

#### Method 1: Copy từ File

1. Mở file: `docs/firebase_security_rules.json`
2. Copy **toàn bộ nội dung** (Ctrl+A, Ctrl+C)
3. Quay lại Firebase Console → Rules tab
4. **Xóa hết** rules cũ (Ctrl+A, Delete)
5. **Paste** rules mới (Ctrl+V)

#### Method 2: Copy từ đây

```json
{
  "rules": {
    ".read": false,
    ".write": false,
    
    "commands": {
      "$gatewayId": {
        ".validate": "newData.hasChildren(['pending', 'processing', 'completed', 'failed']) == false || true",
        
        "pending": {
          ".read": "auth != null",
          ".write": "auth != null",
          
          "$commandId": {
            ".validate": "newData.hasChildren(['type', 'params', 'timestamp', 'status', 'priority'])",
            
            "type": {
              ".validate": "newData.isString() && (newData.val() == 'start_provisioning' || newData.val() == 'stop_provisioning' || newData.val() == 'set_netkey')"
            },
            
            "params": {
              ".validate": "newData.exists()"
            },
            
            "timestamp": {
              ".validate": "newData.isNumber() && newData.val() <= now + 60000"
            },
            
            "status": {
              ".validate": "newData.val() == 'pending'"
            },
            
            "priority": {
              ".validate": "newData.isNumber() && newData.val() >= 0 && newData.val() <= 2"
            },
            
            "created_by": {
              ".validate": "newData.isString()"
            },
            
            "user_id": {
              ".validate": "newData.isString()"
            },
            
            "$other": {
              ".validate": false
            }
          }
        },
        
        "processing": {
          ".read": "auth != null",
          ".write": "auth != null",
          
          "$commandId": {
            ".validate": "newData.hasChildren(['type', 'params', 'timestamp', 'status', 'startedAt'])",
            
            "status": {
              ".validate": "newData.val() == 'processing'"
            },
            
            "startedAt": {
              ".validate": "newData.isNumber()"
            },
            
            "progress": {
              ".validate": "!newData.exists() || (newData.isNumber() && newData.val() >= 0 && newData.val() <= 100)"
            }
          }
        },
        
        "completed": {
          ".read": "auth != null",
          ".write": "auth != null",
          
          "$commandId": {
            ".validate": "newData.hasChildren(['result', 'message', 'timestamp', 'completedAt', 'executionTimeMs'])",
            
            "result": {
              ".validate": "newData.val() == 'success'"
            },
            
            "message": {
              ".validate": "newData.isString()"
            },
            
            "completedAt": {
              ".validate": "newData.isNumber()"
            },
            
            "executionTimeMs": {
              ".validate": "newData.isNumber() && newData.val() >= 0"
            },
            
            "details": {
              ".validate": "newData.exists()"
            }
          }
        },
        
        "failed": {
          ".read": "auth != null",
          ".write": "auth != null",
          
          "$commandId": {
            ".validate": "newData.hasChildren(['result', 'message', 'timestamp', 'failedAt', 'errorCode'])",
            
            "result": {
              ".validate": "newData.val() == 'failed'"
            },
            
            "message": {
              ".validate": "newData.isString()"
            },
            
            "failedAt": {
              ".validate": "newData.isNumber()"
            },
            
            "errorCode": {
              ".validate": "newData.isString()"
            },
            
            "retryable": {
              ".validate": "!newData.exists() || newData.isBoolean()"
            }
          }
        }
      }
    },
    
    "command_results": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null",
        
        ".validate": "newData.hasChildren(['last_command_id', 'last_command_type', 'status', 'message', 'timestamp', 'gateway_online', 'last_poll'])",
        
        "last_command_id": {
          ".validate": "newData.isString()"
        },
        
        "last_command_type": {
          ".validate": "newData.isString()"
        },
        
        "status": {
          ".validate": "newData.isString() && (newData.val() == 'success' || newData.val() == 'failed' || newData.val() == 'processing')"
        },
        
        "message": {
          ".validate": "newData.isString()"
        },
        
        "timestamp": {
          ".validate": "newData.isNumber()"
        },
        
        "gateway_online": {
          ".validate": "newData.isBoolean()"
        },
        
        "last_poll": {
          ".validate": "newData.isNumber()"
        },
        
        "last_heartbeat": {
          ".validate": "!newData.exists() || newData.isNumber()"
        }
      }
    },
    
    "gateway_status": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null",
        
        ".validate": "newData.hasChildren(['online', 'provisioning_active', 'connected_nodes', 'can_accept_commands', 'last_heartbeat'])",
        
        "online": {
          ".validate": "newData.isBoolean()"
        },
        
        "provisioning_active": {
          ".validate": "newData.isBoolean()"
        },
        
        "connected_nodes": {
          ".validate": "newData.isNumber() && newData.val() >= 0"
        },
        
        "can_accept_commands": {
          ".validate": "newData.isBoolean()"
        },
        
        "last_heartbeat": {
          ".validate": "newData.isNumber()"
        },
        
        "current_operation": {
          ".validate": "!newData.exists() || newData.isString()"
        },
        
        "wifi_rssi": {
          ".validate": "!newData.exists() || (newData.isNumber() && newData.val() <= 0)"
        },
        
        "free_heap": {
          ".validate": "!newData.exists() || (newData.isNumber() && newData.val() >= 0)"
        },
        
        "uptime_seconds": {
          ".validate": "!newData.exists() || (newData.isNumber() && newData.val() >= 0)"
        },
        
        "firmware_version": {
          ".validate": "!newData.exists() || newData.isString()"
        }
      }
    },
    
    "gateways": {
      ".read": "auth != null"
    },
    
    "nodes": {
      ".read": "auth != null"
    },
    
    "sensor_data": {
      ".read": "auth != null"
    },
    
    "routing_tables": {
      ".read": "auth != null"
    },
    
    "events": {
      ".read": "auth != null"
    }
  }
}
```

### 3.3. Publish Rules

1. Sau khi paste rules vào editor
2. Click button **"Publish"** (màu xanh, góc trên bên phải)
3. Confirm khi popup xuất hiện
4. Chờ vài giây để Firebase apply rules

### 3.4. Verify Rules

1. Rules sẽ hiển thị timestamp: "Last updated: a few seconds ago"
2. Check không có lỗi syntax (Firebase sẽ báo lỗi nếu có)

---

## Bước 4: Setup Authentication (Important!)

**QUAN TRỌNG:** Security rules yêu cầu `auth != null`, bạn cần enable authentication.

### 4.1. Enable Anonymous Authentication (Quickest)

1. Từ sidebar, chọn **"Build"** → **"Authentication"**
2. Click **"Get started"** (nếu chưa enable)
3. Chọn tab **"Sign-in method"**
4. Enable **"Anonymous"**:
   - Click vào "Anonymous"
   - Toggle **"Enable"**
   - Click **"Save"**

### 4.2. Test Authentication

Trong Mobile App hoặc Gateway:

**JavaScript (Mobile App):**
```javascript
import auth from '@react-native-firebase/auth';

// Sign in anonymously
await auth().signInAnonymously();
console.log('User signed in anonymously');
```

**C++ (Gateway - Optional):**
Gateway có thể dùng Database Secret thay vì auth, nên không cần sign in.

---

## Bước 5: Test Command Queue

### 5.1. Manual Test via Firebase Console

1. **Navigate to pending commands:**
   ```
   /commands/0xABCD/pending/
   ```

2. **Add test command:**
   - Click **"+"** button
   - Name: `cmd_manual_test_001`
   - Add fields:
     ```json
     {
       "type": "start_provisioning",
       "params": {
         "durationMs": 300000,
         "maxSessions": 2
       },
       "timestamp": 1697500000000,
       "status": "pending",
       "priority": 1
     }
     ```

3. **Verify command created:**
   - Command should appear trong pending queue
   - No error messages

4. **Watch for Gateway processing:**
   - Sau 5-10 giây, Gateway sẽ:
     - Di chuyển command từ `pending/` sang `processing/`
     - Sau khi hoàn thành, di chuyển sang `completed/` hoặc `failed/`
   - Check `/command_results/0xABCD/` để xem status

### 5.2. Test from Mobile App

**React Native Example:**
```javascript
import database from '@react-native-firebase/database';
import auth from '@react-native-firebase/auth';

async function testCommand() {
  // 1. Sign in
  await auth().signInAnonymously();
  
  // 2. Send command
  const commandId = `cmd_${Date.now()}`;
  await database()
    .ref(`commands/0xABCD/pending/${commandId}`)
    .set({
      type: 'start_provisioning',
      params: { durationMs: 300000, maxSessions: 2 },
      timestamp: Date.now(),
      status: 'pending',
      priority: 1
    });
  
  console.log('Command sent:', commandId);
  
  // 3. Listen for result
  database()
    .ref('command_results/0xABCD')
    .on('value', snapshot => {
      const result = snapshot.val();
      if (result.last_command_id === commandId) {
        console.log('Result:', result.status, result.message);
      }
    });
}

testCommand();
```

---

## Bước 6: Monitor & Debug

### 6.1. Enable Database Logging

1. Trong **Realtime Database** page
2. Click tab **"Usage"**
3. Xem charts:
   - **Reads:** Số lần read từ database
   - **Writes:** Số lần write vào database
   - **Bandwidth:** Data transfer

### 6.2. Check Recent Activity

1. Trong Firebase Console
2. Xem **"Recent activity"** section (nếu có)
3. Shows read/write operations theo real-time

### 6.3. Debug Rules

Nếu gặp lỗi "Permission denied":

1. **Check authentication:**
   ```javascript
   const user = auth().currentUser;
   console.log('Signed in:', user !== null);
   ```

2. **Test rules manually:**
   - Click tab **"Rules"**
   - Scroll xuống → Click **"Rules Playground"**
   - Nhập path: `/commands/0xABCD/pending`
   - Type: **"read"** hoặc **"write"**
   - Check **"Authenticated"** → Nhập UID: `test-user-123`
   - Click **"Run"** → Should show "Allow" hoặc "Deny"

3. **Common issues:**
   - `auth != null` → User chưa sign in
   - Field validation failed → Data structure không đúng
   - Missing required fields → Command thiếu fields

---

## Bước 7: Production Setup

### 7.1. Replace Test Gateway ID

Thay `0xABCD` bằng Gateway ID thật:

1. Gateway ID được tạo từ MAC address:
   ```cpp
   // Gateway code
   String macAddr = WiFi.macAddress();  // Example: "AA:BB:CC:DD:EE:FF"
   String gatewayId = "0x" + macAddr.substring(macAddr.length() - 4);
   // Result: "0xEEFF"
   ```

2. Update Firebase structure:
   ```
   /commands/0xEEFF/pending/    ← Your actual Gateway ID
   /command_results/0xEEFF/
   /gateway_status/0xEEFF/
   ```

### 7.2. Delete Test Data

1. Navigate to `/commands/0xABCD/`
2. Click icon **3 chấm** (⋮)
3. Chọn **"Delete"**
4. Confirm

### 7.3. Enable Firebase Backup (Optional)

1. Trong **Realtime Database** page
2. Click tab **"Backups"** (nếu có)
3. Enable automatic daily backups

---

## Checklist Hoàn Thành

Sau khi làm xong tất cả bước trên:

- [ ] ✅ Đã import database schema vào Firebase Console
- [ ] ✅ Đã apply security rules
- [ ] ✅ Đã enable Authentication (Anonymous)
- [ ] ✅ Đã test manual command từ Console
- [ ] ✅ Đã verify command structure đúng
- [ ] ✅ Đã test từ Mobile App (optional)
- [ ] ✅ Đã replace test Gateway ID bằng ID thật
- [ ] ✅ Đã xóa test data

---

## Screenshots Guide

### Import JSON Location
![Firebase Import JSON](https://i.imgur.com/example1.png)

1. Click root node `/`
2. Click 3-dot menu (⋮)
3. Select "Import JSON"

### Rules Editor
![Firebase Rules Editor](https://i.imgur.com/example2.png)

1. Tab "Rules"
2. Paste rules
3. Click "Publish"

### Database Structure After Import
![Database Structure](https://i.imgur.com/example3.png)

```
/
├── commands/
├── command_results/
├── gateway_status/
└── _metadata/
```

---

## Troubleshooting

### Error: "Permission denied"

**Cause:** User không authenticated hoặc rules sai

**Solution:**
```javascript
// Sign in first
await auth().signInAnonymously();
```

### Error: "Validation failed"

**Cause:** Command structure không đúng format

**Solution:** Check command có đủ required fields:
- `type` (string)
- `params` (object)
- `timestamp` (number)
- `status` (string: "pending")
- `priority` (number: 0-2)

### Error: "Cannot read properties of null"

**Cause:** Path không tồn tại

**Solution:** Verify path trong console:
```
/commands/0xABCD/pending/cmd_xxx
```

---

## Next Steps

Sau khi setup Firebase Console xong:

1. ✅ **Phase 1:** COMPLETED - Documentation & Schema
2. 🔄 **Phase 2:** Implement Gateway command listener
3. 🔄 **Phase 3:** Mobile App integration
4. 🔄 **Phase 4:** End-to-end testing

---

**Document Version:** 1.0.0  
**Last Updated:** October 17, 2025  
**Support:** Refer to FIREBASE_COMMAND_QUEUE.md for details
