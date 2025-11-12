# Firebase Rules - Simplified for Gateway Testing

## Instructions

1. Go to Firebase Console: https://console.firebase.google.com/
2. Select project: **kagri-iot**
3. Navigate to: **Realtime Database** → **Rules**
4. Copy and paste the rules below
5. Click **Publish**

---

## Firebase Rules (Copy this)

```json
{
  "rules": {
    "sensor_data": {
      "$gatewayId": {
        "$nodeId": {
          ".read": "auth != null",
          ".write": "auth != null",
          "$timestamp": {
            ".validate": "newData.hasChildren(['nodeId', 'temperature', 'humidity', 'timestamp'])"
          }
        }
      }
    },
    
    "gateways": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null",
        
        "status": {
          ".validate": "newData.hasChildren(['gatewayId', 'timestamp'])"
        },
        
        "routing_table": {
          ".write": "auth != null"
        }
      }
    },
    
    "events": {
      "$gatewayId": {
        ".read": "auth != null",
        ".write": "auth != null",
        "$eventId": {
          ".validate": "newData.hasChildren(['type', 'timestamp'])"
        }
      }
    },
    
    "users": {
      "$uid": {
        ".read": "$uid === auth.uid",
        ".write": "$uid === auth.uid",
        
        "profile": {
          ".validate": "newData.hasChildren(['email', 'displayName'])"
        },
        
        "gateways": {
          "$gatewayMAC": {
            ".read": "$uid === auth.uid",
            ".write": "$uid === auth.uid || auth != null",
            
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
        
        "commands": {
          "$gatewayMAC": {
            ".read": "$uid === auth.uid",
            ".write": "$uid === auth.uid",
            
            "pending": {
              "$commandId": {
                ".validate": "newData.hasChildren(['type', 'timestamp'])"
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
          ".read": "$uid === auth.uid",
          ".write": "auth != null",
          "$timestamp": {
            ".validate": "newData.hasChildren(['type', 'timestamp'])"
          }
        }
      }
    },
    
    "nodes": {
      "$uid": {
        ".read": "$uid === auth.uid",
        ".write": "$uid === auth.uid || auth != null",
        
        "$gatewayMAC": {
          "$nodeId": {
            "latest_data": {
              ".write": "auth != null"
            },
            
            "info": {
              ".write": "$uid === auth.uid || auth != null",
              ".validate": "newData.hasChildren(['nodeId', 'gatewayMAC'])"
            }
          }
        }
      }
    }
  }
}
```

---

## Database Structure After Test

After running the Phase 5 test, you should see this structure in Firebase:

```
kagri-iot-default-rtdb/
├── sensor_data/
│   └── GW_TEST_001/
│       └── 0x1234/
│           └── {auto-generated-id}/
│               ├── nodeId: "0x1234"
│               ├── temperature: 25.5
│               ├── humidity: 60.0
│               ├── rssi: -45
│               ├── snr: 8.5
│               └── timestamp: 123456
│
├── gateways/
│   └── GW_TEST_001/
│       ├── status/
│       │   ├── gatewayId: "GW_TEST_001"
│       │   ├── nodesCount: 3
│       │   ├── packetsRx: 150
│       │   ├── packetsTx: 145
│       │   ├── cellularRssi: -55
│       │   ├── freeHeap: 245760
│       │   ├── uptime: 45
│       │   └── timestamp: 123456
│       │
│       └── routing_table/
│           ├── 0x1234/
│           │   ├── nextHop: "0x0000"
│           │   ├── hopCount: 1
│           │   └── rssi: -45
│           ├── 0x5678/
│           │   ├── nextHop: "0x1234"
│           │   ├── hopCount: 2
│           │   └── rssi: -60
│           └── 0x9ABC/
│               ├── nextHop: "0x0000"
│               ├── hopCount: 1
│               └── rssi: -50
│
└── events/
    └── GW_TEST_001/
        └── {auto-generated-id}/
            ├── type: "node_joined"
            ├── nodeId: "0x1234"
            ├── details: "New node joined the network"
            ├── gateway: "GW_TEST_001"
            └── timestamp: 123456
```

---

## Key Changes from Original Rules

### Simplified Top-Level Paths (No uid required):
- ✅ `/sensor_data/{gatewayId}/{nodeId}` - Gateway can write sensor data
- ✅ `/gateways/{gatewayId}/status` - Gateway can write status
- ✅ `/gateways/{gatewayId}/routing_table` - Gateway can write routing table
- ✅ `/events/{gatewayId}` - Gateway can log events

### Multi-User Paths (Requires uid - for app):
- `/users/{uid}/gateways/{gatewayMAC}` - User-owned gateways
- `/users/{uid}/commands/{gatewayMAC}` - Command queue
- `/nodes/{uid}/{gatewayMAC}/{nodeId}` - User-owned nodes

### Authentication:
- Gateway uses **Database Secret** (`?auth=<secret>`) → `auth != null` ✅
- Mobile app uses **Firebase Auth** → `auth.uid` matches `$uid` ✅

---

## Testing

After updating rules, run:
```bash
pio run -e test-cellular-phase5 -t upload
```

Expected log output:
```
✅ PASSED: Sensor data upload
✅ PASSED: Gateway status upload
✅ PASSED: Routing table upload
✅ PASSED: Event logging
✅ PASSED: Read data
🎉 PHASE 5 TEST PASSED!
```

Then check Firebase Console to verify data was written correctly.
