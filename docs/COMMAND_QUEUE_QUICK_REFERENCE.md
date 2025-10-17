# Firebase Command Queue - Quick Reference Card

## 🚀 Quick Start (Mobile App)

### Setup
```javascript
// React Native
npm install @react-native-firebase/database

// Flutter
flutter pub add firebase_database
```

### Send Command (3 Steps)

```javascript
// 1. Check gateway online
const status = await db.ref(`gateway_status/0xABCD`).once('value');
if (!status.val().online) return showError('Gateway offline');

// 2. Send command
const cmdId = `cmd_${Date.now()}`;
await db.ref(`commands/0xABCD/pending/${cmdId}`).set({
  type: 'start_provisioning',
  params: { durationMs: 600000, maxSessions: 4 },
  timestamp: Date.now(),
  status: 'pending',
  priority: 1
});

// 3. Listen for result
db.ref(`command_results/0xABCD`).on('value', snap => {
  if (snap.val().last_command_id === cmdId) {
    console.log(snap.val().message);
  }
});
```

---

## 📋 Command Types

### Start Provisioning
```json
{
  "type": "start_provisioning",
  "params": {
    "durationMs": 600000,
    "maxSessions": 4
  }
}
```

### Stop Provisioning
```json
{
  "type": "stop_provisioning",
  "params": {}
}
```

### Set Network Key
```json
{
  "type": "set_netkey",
  "params": {
    "networkKey": "0102030405060708090A0B0C0D0E0F10",
    "authToken": "1112131415161718",
    "networkId": 4660,
    "keyVersion": 2
  }
}
```

---

## 🗂️ Database Paths

| Path | Purpose |
|------|---------|
| `/commands/{gwId}/pending/` | Queue commands here |
| `/commands/{gwId}/completed/` | Check success |
| `/command_results/{gwId}/` | Real-time status |
| `/gateway_status/{gwId}/` | Check online |

---

## ⏱️ Timeouts

- **Pending:** Expires in 5 minutes
- **Processing:** Timeout in 2 minutes
- **Completed/Failed:** Auto-delete after 24 hours

---

## 🔐 Validation Rules

### Network Key
- Must be 32 hex characters
- Example: `0102030405060708090A0B0C0D0E0F10`

### Auth Token
- Must be 16 hex characters
- Example: `1112131415161718`

### Duration
- Min: 60000ms (1 minute)
- Max: 3600000ms (1 hour)

### Max Sessions
- Range: 1-10

---

## ❌ Error Codes

| Code | Retryable | Description |
|------|-----------|-------------|
| `UNKNOWN_COMMAND` | No | Invalid command type |
| `INVALID_PARAMS` | No | Bad parameters |
| `NOT_READY` | Yes | Gateway busy |
| `ALREADY_RUNNING` | Yes | Already provisioning |
| `TIMEOUT` | Yes | Command timeout |

---

## 🎯 Priority Levels

- **0:** High (Set netkey, emergency)
- **1:** Normal (Start/stop provisioning)
- **2:** Low (Status queries)

---

## 📊 Command Status Flow

```
pending → processing → completed (success)
                    → failed (error)
```

---

## 🧪 Testing Commands

### Manual Test (Firebase Console)

1. Go to: `commands/0xABCD/pending/`
2. Add new child: `cmd_test_001`
3. Set value:
```json
{
  "type": "start_provisioning",
  "params": {"durationMs": 300000, "maxSessions": 2},
  "timestamp": 1697500000000,
  "status": "pending",
  "priority": 1
}
```
4. Watch: `command_results/0xABCD/`
5. Expect: Command moves to `completed/` within 10 seconds

---

## 🔍 Troubleshooting

### Command Not Executed?
- [ ] Check `gateway_status/{gwId}/online` = true
- [ ] Verify command in `pending/` queue
- [ ] Check Firebase security rules allow write
- [ ] Gateway polls every 5-10 seconds

### No Result?
- [ ] Listener attached before sending command?
- [ ] Check `command_results/{gwId}/` exists
- [ ] Verify `last_command_id` matches
- [ ] Check security rules allow read

---

## 📚 Full Documentation

- **Architecture:** `FIREBASE_COMMAND_QUEUE.md`
- **Mobile Guide:** `MOBILE_APP_INTEGRATION_GUIDE.md`
- **Schema:** `firebase_command_queue_schema.json`
- **Security:** `firebase_security_rules.json`

---

**Version:** 1.0.0 | **Updated:** Oct 17, 2025
