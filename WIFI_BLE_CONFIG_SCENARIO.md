# WiFi Configuration via BLE - Scenario & UI Design

## Kịch Bản (Scenario)

### Flow:
1. **User Action**: Hold button 5 seconds on HOME screen
2. **Device State**: Enter `WIFI_CONFIG` state
3. **Screen 1 - WiFi Config Introduction** (5 seconds)
   - Display device name (for BLE discovery)
   - Show BLE icon and "Waiting for connection..."
   - Display pairing code/instructions
   
4. **Screen 2 - BLE Waiting** (waiting state)
   - Show animated BLE signal indicator
   - Display "Searching for BLE connection..."
   - Timeout: 60 seconds
   
5. **Screen 3 - BLE Connected** (when phone connects)
   - Display "Connected via BLE"
   - Show receiving WiFi credentials indicator
   - Animated progress dots
   
6. **Screen 4 - Credentials Received**
   - Display: "WiFi: [SSID]"
   - Display: "Connecting..."
   - Show connection progress bar
   
7. **Screen 5 - WiFi Connected Success**
   - Display: "WiFi Connected!"
   - Show IP address: "192.168.x.x"
   - Show signal strength
   - Auto-return to HOME after 3 seconds
   
8. **Error Screens**
   - "BLE Connection Failed"
   - "WiFi Connection Failed"
   - "Invalid Credentials"
   - Auto-return to HOME after timeout

---

## UI Screen Designs

### Screen 1: BLE Config Start
```
┌─────────────────────────────┐
│     WIFI CONFIG via BLE     │  (Magenta header)
├─────────────────────────────┤
│                             │
│   [BLE Icon]  ~   ~   ~     │  (BLE icon with waves)
│                             │
│  Device Name:               │
│  KAGRI-Device-A1B2          │  (Cyan, device identifier)
│                             │
│  Open BLE App on Phone:     │  (Yellow)
│  "KAGRI Config"             │  (Green)
│                             │
│  Waiting for connection...  │  (Orange, animated)
│                             │
└─────────────────────────────┘
```

### Screen 2: BLE Waiting
```
┌─────────────────────────────┐
│     SEARCHING BLE...        │  (Magenta header)
├─────────────────────────────┤
│                             │
│        ~   ~   ~            │  (Animated BLE waves)
│        ~       ~            │
│        ~   ~   ~            │
│                             │
│  Searching for connection   │  (Cyan)
│  Please open app on phone   │  (Green)
│                             │
│  Time remaining: 45s        │  (Yellow, countdown)
│                             │
│  [●  ●  ●]  Scanning...     │  (Orange, animated dots)
│                             │
└─────────────────────────────┘
```

### Screen 3: BLE Connected
```
┌─────────────────────────────┐
│    BLE CONNECTED!           │  (Green header)
├─────────────────────────────┤
│                             │
│   ✓ Connected to phone      │  (Green checkmark)
│                             │
│  Receiving WiFi data...     │  (Cyan)
│                             │
│  Progress:                  │
│  [████████░░░░░░░░░░]  45%  │  (Yellow progress bar)
│                             │
│  [∴  ∴∴  ∴∴∴]               │  (Animated dots)
│                             │
└─────────────────────────────┘
```

### Screen 4: Credentials Received
```
┌─────────────────────────────┐
│  CREDENTIALS RECEIVED       │  (Magenta header)
├─────────────────────────────┤
│                             │
│  WiFi Network:              │  (Yellow label)
│  MyHome-WiFi-5G             │  (Green SSID)
│                             │
│  Signal Strength:           │  (Yellow label)
│  ████████░░ 80%             │  (Cyan signal bars)
│                             │
│  Connecting to WiFi...      │  (Orange)
│                             │
│  [████████░░░░░░░░░░]  50%  │  (Cyan progress bar)
│                             │
└─────────────────────────────┘
```

### Screen 5: WiFi Success
```
┌─────────────────────────────┐
│   ✓ WiFi CONNECTED!         │  (Green header)
├─────────────────────────────┤
│                             │
│   Connection Successful!    │  (Green, large text)
│                             │
│  WiFi: MyHome-WiFi-5G       │  (Yellow)
│  Signal: ████████░░ 80%     │  (Cyan bars)
│                             │
│  IP Address:                │  (Yellow label)
│  192.168.1.100              │  (Green IP)
│                             │
│  Returning to HOME...       │  (Orange)
│  in 3 seconds               │  (Yellow countdown)
│                             │
└─────────────────────────────┘
```

### Screen 6: WiFi Connect Error
```
┌─────────────────────────────┐
│      ✗ CONNECTION FAILED    │  (Red header)
├─────────────────────────────┤
│                             │
│  WiFi Connection Error      │  (Red text)
│                             │
│  Network: MyHome-WiFi-5G    │  (Yellow)
│  Status: Authentication     │  (Orange)
│          Failed             │
│                             │
│  Possible causes:           │  (Cyan)
│  • Wrong Password           │  (Yellow bullet)
│  • Network Unavailable      │
│  • Signal Too Weak          │
│                             │
│  Retrying... 30s            │  (Orange countdown)
│                             │
└─────────────────────────────┘
```

### Screen 7: BLE Connection Error
```
┌─────────────────────────────┐
│      ✗ BLE FAILED           │  (Red header)
├─────────────────────────────┤
│                             │
│  BLE Connection Timeout     │  (Red text)
│                             │
│  Device was not found       │  (Orange)
│  Please try again:          │
│                             │
│  1. Check phone Bluetooth   │  (Yellow)
│  2. Hold button 5s again    │
│  3. Open KAGRI Config app   │
│                             │
│  Returning to HOME...       │  (Green)
│  in 10 seconds              │  (Cyan countdown)
│                             │
└─────────────────────────────┘
```

---

## BLE Configuration Data Flow

### Device (Handheld) Side:
1. Enter WIFI_CONFIG state
2. Start BLE advertising with service UUID
3. Accept BLE connection from phone
4. Receive WiFi credentials via BLE characteristics:
   - Characteristic 1: SSID (UTF-8 string)
   - Characteristic 2: Password (encrypted or plain)
   - Characteristic 3: Trigger connect (boolean)
5. Validate credentials
6. Connect to WiFi using ESP32 WiFi API
7. Display success/error status
8. Auto-return to HOME

### Phone App Side:
1. Scan for BLE devices named "KAGRI-*"
2. Connect to discovered device
3. Display WiFi network selector
4. Allow user to select/enter WiFi credentials
5. Send SSID via BLE characteristic 1
6. Send Password via BLE characteristic 2
7. Send trigger signal via characteristic 3
8. Wait for connection confirmation
9. Display connection status

---

## Implementation Tasks

### UI Updates (Display Manager):
- [ ] `drawWiFiConfigStartScreen()` - Show device name, BLE pairing instructions
- [ ] `drawBLEWaitingScreen()` - Animated BLE search indicator
- [ ] `drawBLEConnectedScreen()` - Connected status with progress
- [ ] `drawCredentialsReceivedScreen()` - Show WiFi network details
- [ ] `drawWiFiConnectingScreen()` - WiFi connection progress
- [ ] `drawWiFiConnectSuccessScreen()` - Success with IP address
- [ ] `drawWiFiConnectErrorScreen()` - Error details and retry info
- [ ] `drawBLEErrorScreen()` - BLE timeout/connection error

### State Machine Updates:
- [ ] Add WiFi configuration substates (BLE_WAIT, BLE_CONNECTED, WiFi_CONNECTING, etc.)
- [ ] Handle BLE connection events
- [ ] Handle WiFi credentials received events
- [ ] Handle connection timeouts

### BLE Integration:
- [ ] Configure BLE service and characteristics
- [ ] Implement credential reception handler
- [ ] Implement WiFi connection logic
- [ ] Add status callbacks

---

## Color Scheme Reference

| Element | Color | Usage |
|---------|-------|-------|
| Headers | Magenta (Error) / Green (Success) | State indication |
| Labels | Yellow | Information labels |
| Values | Green (success) / Orange (waiting) / Red (error) | Data display |
| Progress | Cyan/Yellow | Animated progress |
| Icons | Various | Status indicators |
| Backgrounds | DARK_BLUE | Consistent look |

---

## Timing

- BLE Waiting Timeout: 60 seconds
- WiFi Connect Timeout: 30 seconds
- Success Display: 3 seconds before return to HOME
- Error Display: 10-30 seconds before retry/return
- Animation Frames: 200-500ms per update
