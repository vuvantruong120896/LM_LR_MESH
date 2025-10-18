# Testing Firebase Command Polling

## ✅ Implementation Complete

Phase 2 (Firmware) is now fully implemented and compiled successfully!

### What Was Implemented:

1. **FirebaseCommandPoller Service** (`src/services/`)
   - Polls Firebase every 10 seconds for new commands
   - Manages command state: pending → processing → completed/failed
   - Updates real-time progress to `command_results/{gatewayMAC}/`

2. **GatewayApp Integration** (`src/application/app_gateway/`)
   - Command poller integrated into main loop
   - Provisioning handlers: start/stop with timeout support
   - Fast discovery mode: 30s HELLO broadcasts during provisioning
   - Mode change broadcasts to all mesh nodes
   - Progress updates every 5 seconds

3. **FirebaseClient Enhancement**
   - Added `getFirebaseData()` method to share Firebase connection
   - Enables command poller to use existing connection (no duplicate connections)

---

## 🧪 Manual Testing Steps

### Prerequisites:
```bash
# 1. Flash the firmware
cd d:\Projects\Lora\LM_LR_MESH
platformio run -e esp32-c3-devkitm-1 -t upload -t monitor

# 2. Wait for Gateway to connect to Firebase
# Look for logs: "Firebase initialized successfully"
```

### Test 1: Command Detection

**Goal**: Verify Gateway detects new commands within 10 seconds

**Steps**:
1. Open Firebase Console: https://console.firebase.google.com
2. Go to Realtime Database → your project
3. Navigate to: `users/test-uid/commands/AA:BB:CC:DD:EE:FF/pending/`
   - Replace `test-uid` with your test user UID
   - Replace `AA:BB:CC:DD:EE:FF` with your Gateway MAC (check Serial Monitor)

4. Click **+** to add new data:
   ```json
   {
     "cmd-001": {
       "id": "cmd-001",
       "type": "start_provisioning",
       "params": {
         "durationMs": 60000
       },
       "timestamp": 1735000000000,
       "priority": 5
     }
   }
   ```

5. Watch Serial Monitor for:
   ```
   [CommandPoller] Found 1 pending commands
   [CommandPoller] Processing command: cmd-001 (type: start_provisioning)
   [Gateway] Starting provisioning mode for 60000 ms (1 minute)
   [Radio] Switching to fast discovery mode
   ```

6. Check Firebase path changes:
   - `pending/cmd-001` should disappear
   - `processing/cmd-001` should appear
   - `command_results/AA:BB:CC:DD:EE:FF/cmd-001/` should show:
     ```json
     {
       "status": "processing",
       "progress": {
         "nodesDiscovered": 0,
         "timeRemainingMs": 60000
       }
     }
     ```

**Expected Result**: ✅ Command moves from pending → processing within 10 seconds

---

### Test 2: Provisioning Mode

**Goal**: Verify fast discovery mode activates and broadcasts to nodes

**Steps**:
1. After Test 1, check Serial Monitor for:
   ```
   [Radio] Fast discovery mode active (HELLO every 30s)
   [Mesh] Broadcasting HELLO with mode: FAST_DISCOVERY
   ```

2. Turn on Node devices (if available)
3. Watch for node discovery logs:
   ```
   [Mesh] New node joined: 0x1234
   [CommandPoller] Progress update: 1 nodes discovered, 50s remaining
   ```

4. Check Firebase `command_results/`:
   ```json
   {
     "status": "processing",
     "progress": {
       "nodesDiscovered": 1,
       "timeRemainingMs": 50000
     },
     "updatedAt": 1735000010000
   }
   ```

**Expected Result**: 
- ✅ HELLO packets every 30 seconds (instead of normal 120s)
- ✅ Nodes discover Gateway faster
- ✅ Progress updates to Firebase every 5 seconds

---

### Test 3: Command Completion

**Goal**: Verify provisioning stops after timeout and reports statistics

**Steps**:
1. Wait for 60 seconds (duration from Test 1)
2. Watch Serial Monitor for:
   ```
   [Gateway] Provisioning timeout reached
   [Gateway] Stopping provisioning mode
   [Radio] Switching to normal mode
   [Mesh] Broadcasting HELLO with mode: NORMAL
   ```

3. Check Firebase:
   - `processing/cmd-001` should disappear
   - `completed/cmd-001` should appear:
     ```json
     {
       "id": "cmd-001",
       "completedAt": 1735000060000,
       "result": {
         "success": true,
         "message": "Provisioning completed successfully",
         "nodesDiscovered": 1,
         "durationMs": 60000
       }
     }
     ```

   - `command_results/AA:BB:CC:DD:EE:FF/cmd-001/`:
     ```json
     {
       "status": "completed",
       "result": {
         "success": true,
         "message": "Provisioning completed successfully",
         "nodesDiscovered": 1,
         "durationMs": 60000
       }
     }
     ```

**Expected Result**: 
- ✅ Mode switches back to normal (HELLO every 120s)
- ✅ Command moves to completed/
- ✅ Statistics reported to Firebase

---

### Test 4: Stop Command

**Goal**: Verify manual stop before timeout

**Steps**:
1. Start provisioning again (repeat Test 1 with duration 300000 = 5 minutes)
2. Wait 30 seconds
3. Add stop command to Firebase:
   ```json
   {
     "cmd-002": {
       "id": "cmd-002",
       "type": "stop_provisioning",
       "params": {},
       "timestamp": 1735000030000,
       "priority": 10
     }
   }
   ```

4. Watch Serial Monitor:
   ```
   [CommandPoller] Found 1 pending commands
   [Gateway] Stopping provisioning mode (manual stop)
   [Radio] Switching to normal mode
   ```

5. Check Firebase:
   - Previous command (cmd-001) should complete with partial duration
   - Stop command (cmd-002) should complete immediately

**Expected Result**: 
- ✅ Provisioning stops before timeout
- ✅ Both commands show completed status

---

### Test 5: Error Handling

**Goal**: Verify invalid commands are handled gracefully

**Steps**:
1. Add invalid command:
   ```json
   {
     "cmd-003": {
       "id": "cmd-003",
       "type": "invalid_command",
       "params": {},
       "timestamp": 1735000000000,
       "priority": 5
     }
   }
   ```

2. Watch Serial Monitor:
   ```
   [CommandPoller] Unknown command type: invalid_command
   [CommandPoller] Command cmd-003 moved to failed
   ```

3. Check Firebase:
   - Command should be in `failed/cmd-003/`:
     ```json
     {
       "id": "cmd-003",
       "failedAt": 1735000010000,
       "error": "Unknown command type: invalid_command"
     }
     ```

**Expected Result**: ✅ Invalid commands move to failed/ with error message

---

## 📊 Monitoring Commands

### Serial Monitor Output:

Normal operation:
```
[CommandPoller] Polling for commands...
[CommandPoller] No pending commands
```

During provisioning:
```
[CommandPoller] Polling for commands...
[Gateway] Provisioning active: 45s remaining, 2 nodes discovered
[CommandPoller] Progress update sent to Firebase
```

### Firebase Console Monitoring:

Watch these paths in real-time:
- `users/{uid}/commands/{mac}/pending/` - New commands from Mobile App
- `users/{uid}/commands/{mac}/processing/` - Currently executing commands
- `users/{uid}/commands/{mac}/completed/` - Successfully finished commands
- `users/{uid}/commands/{mac}/failed/` - Failed commands with errors
- `command_results/{mac}/` - Real-time status for Mobile App UI

---

## 🐛 Troubleshooting

### "Firebase not connected"
- Check WiFi credentials in `src/config.h`
- Check Firebase Host and Auth token
- Look for "Firebase initialized successfully" in logs

### "No pending commands" after adding to Firebase
- Verify user UID matches `setUserContext()` call
- Verify Gateway MAC matches (check Serial Monitor for MAC)
- Wait 10 seconds for next poll cycle

### "Command stuck in processing"
- Check if provisioning timeout is set correctly
- Manually delete from `processing/` and re-add to `pending/`
- Restart Gateway to reset state

### "Nodes not discovering"
- Verify nodes are powered on and in range
- Check LoRa frequency configuration matches
- Look for "HELLO sent" logs every 30 seconds during provisioning

---

## ✅ Next Steps

**Phase 2 (Firmware) is COMPLETE!**

**Phase 3: Mobile App Implementation** (2-3 days)

1. **Firebase Service** (`lib/services/firebase_command_service.dart`)
   - Send commands to Firebase
   - Listen for command results
   - Handle timeout and errors

2. **Gateway Selection Screen** (`lib/screens/gateway_selection_screen.dart`)
   - Show list of user's Gateways
   - "Add Nodes via Gateway" button
   - Gateway status indicator

3. **Provisioning Screen** (`lib/screens/provisioning_progress_screen.dart`)
   - Real-time progress display
   - Nodes discovered count
   - Time remaining countdown
   - Stop button

4. **Home Screen Update** (`lib/screens/home_screen.dart`)
   - Show two options when pressing **+**:
     - "Add Gateway (BLE)"
     - "Add Nodes (via Gateway)"
   - Route to appropriate screen

**Ready to start Phase 3?** Let me know when you want to implement the Mobile App side! 🚀
