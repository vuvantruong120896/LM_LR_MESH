# WiFi BLE Configuration - Visual Screen Mockups

## Screen 1: WiFi Config Start (Magenta Header)
```
╔═══════════════════════════════╗
║   WiFi Config BLE             ║  ← Magenta header (50px)
╠═══════════════════════════════╣
║                               ║
║                               ║
║          ~                     ║  ← BLE icon (waves)
║         ~ ~                    ║     Cyan, textSize 3
║                               ║
║  Device Name:                 ║
║  KAGRI-ESP32                  ║  ← Cyan, textSize 2
║                               ║
║  1. Open KAGRI App            ║  ← Green text, left-aligned
║  2. Scan for device           ║
║                               ║
║  Waiting for connection...    ║  ← Orange, animated
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 2: BLE Waiting (Magenta Header, Animated)
```
╔═══════════════════════════════╗
║   SEARCHING...                ║  ← Magenta header
╠═══════════════════════════════╣
║                               ║
║       ~   ~   ~               ║  ← Animated BLE waves
║       ~       ~               ║     Cyan, growing
║       ~   ~   ~               ║
║                               ║
║  Searching for device...      ║  ← Cyan text
║  Enable Bluetooth on phone    ║  ← Green text
║                               ║
║  Time left: 45s               ║  ← Yellow countdown
║                               ║
║  [*  *  *]                    ║  ← Orange animated dots
║                               ║
╚═══════════════════════════════╝

Animation sequence:
Frame 1: [*  *  *]
Frame 2: [ *  * *]
Frame 3: [*  * * ]
Frame 4: [*  *  *]  (repeat)
```

---

## Screen 3: BLE Connected (Green Header)
```
╔═══════════════════════════════╗
║   BLE CONNECTED               ║  ← Dark Green header
╠═══════════════════════════════╣
║                               ║
║           ~                   ║  ← Checkmark symbol (large)
║                               ║     Green, textSize 4
║                               ║
║  Connected to phone!          ║  ← Green text
║                               ║
║  Receiving WiFi data...       ║  ← Cyan text
║                               ║
║  ╔══════════════════════╗     ║  ← Yellow progress bar
║  ║████████░░░░░░░░░░░░║     ║
║  ╚══════════════════════╝     ║
║                               ║
║                    45%        ║  ← White percentage
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 4: Credentials Received (Magenta Header)
```
╔═══════════════════════════════╗
║   CREDENTIALS                 ║  ← Magenta header
╠═══════════════════════════════╣
║                               ║
║  WiFi Network:                ║  ← Yellow label
║  MyHome-WiFi-5G               ║  ← Green SSID (large)
║                               ║
║  Signal:                      ║  ← Yellow label
║  ╔══════════════════════╗     ║  ← Cyan signal bar
║  ║████████░░░░░░░░░░░░║     ║
║  ╚══════════════════════╝     ║
║                    80%        ║
║                               ║
║  Connecting to WiFi...        ║  ← Orange status
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 5: WiFi Connecting (Magenta Header, Large Progress)
```
╔═══════════════════════════════╗
║   CONNECTING                  ║  ← Magenta header
╠═══════════════════════════════╣
║                               ║
║  Connecting to WiFi network   ║  ← Cyan text
║                               ║
║  ╔═════════════════════════╗  ║  ← Large progress bar
║  ║████████████░░░░░░░░░░░║  ║     (25px height)
║  ╚═════════════════════════╝  ║
║                               ║
║            50%                ║  ← Large white percentage
║                               ║
║        [*  *  *]              ║  ← Orange animated dots
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 6: WiFi Success (Green Header)
```
╔═══════════════════════════════╗
║   SUCCESS!                    ║  ← Dark Green header
╠═══════════════════════════════╣
║                               ║
║           ~                   ║  ← Checkmark (Green)
║                               ║
║  WiFi Connected!              ║  ← Green text
║                               ║
║  IP Address:                  ║  ← Yellow label
║  192.168.1.100                ║  ← Cyan large text
║                               ║
║  Returning to HOME...         ║  ← Orange message
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 7: WiFi Error (Red Header)
```
╔═══════════════════════════════╗
║   FAILED                      ║  ← Dark Red header
╠═══════════════════════════════╣
║                               ║
║  WiFi Connection Failed       ║  ← Red text
║  Authentication Failed        ║  ← Orange error detail
║                               ║
║  Possible causes:             ║  ← Cyan header
║  - Wrong Password             ║  ← Yellow bullets
║  - Network Unavailable        ║
║  - Signal Too Weak            ║
║                               ║
║  Hold button to retry         ║  ← Green instruction
║                               ║
╚═══════════════════════════════╝
```

---

## Screen 8: BLE Error (Red Header)
```
╔═══════════════════════════════╗
║   BLE ERROR                   ║  ← Dark Red header
╠═══════════════════════════════╣
║                               ║
║  BLE Connection Timeout       ║  ← Orange error message
║  Device was not found         ║  ← Red text
║                               ║
║  Please try again:            ║  ← Yellow header
║  1. Enable Bluetooth          ║  ← Green numbered list
║  2. Open KAGRI App            ║
║  3. Hold button 5s            ║
║                               ║
║  Return in: 10s               ║  ← Cyan countdown
║                               ║
╚═══════════════════════════════╝
```

---

## Color Palette Summary

```
Header Colors:
  ✓ Success/Connected: Dark Green (0x004D00)
  ✗ Error/Timeout:     Dark Red (0x660000)
  ⚙ Processing:        Magenta (0xFF00FF)

Text Colors (per usage):
  Labels:              Yellow (0xFFFF00)
  Data/Values:         Cyan (0x00FFFF)
  Instructions:        Green (0x00FF00)
  Status/Progress:     Orange (0xFFA500)
  Errors:              Red (0xFF0000)
  Standard Text:       White (0xFFFFFF)
  Backgrounds:         Dark Blue (0x000033)

Progress/Indicators:
  Filled:              Cyan, Yellow (depending on context)
  Empty/Border:        Light Gray or color outline
  Animated Elements:   Orange dots, Green checkmark
```

---

## Animation Details

### BLE Searching Animation (Screen 2):
Waves expand outward from center, repeat every 800ms
```
Frame 1: ~   ~   ~    (narrow waves)
Frame 2: ~ ← ~ → ~    (expanding)
Frame 3: ~       ~    (widest)
Frame 4: ~   ~   ~    (reset to narrow)
```

### Connected Status Animation (Screen 3/6):
Checkmark symbol: `~` (or could use degree symbol for variety)
- Static display (no animation needed)
- Large size (textSize 4) makes it prominent

### Progress Bar Animation:
Fills smoothly left-to-right, updates with each percent
- Border: colored rectangle (Yellow/Cyan)
- Fill: same color progress bar
- Smooth animation with text percentage

### Animated Dots (Screens 2, 5):
Cycle through patterns every 300-500ms
```
Pattern 1: [*  *  *]   (all filled)
Pattern 2: [ *  * *]   (first empty)
Pattern 3: [*  * * ]   (middle empty)
Pattern 4: [*  *  *]   (last empty, repeat)
```

---

## Screen Display Flow Diagram

```
                    ┌─────────────────────┐
                    │   HOME SCREEN       │
                    │  (hold button 5s)   │
                    └──────────┬──────────┘
                               │
                    ┌──────────▼──────────┐
                    │  Screen 1: START    │ (2 seconds)
                    │  (device name)      │
                    └──────────┬──────────┘
                               │
           ┌───────────────────┴───────────────────┐
           │                                       │
      YES (BLE OK)                          NO (Timeout)
           │                                       │
┌──────────▼──────────┐          ┌─────────────────▼────┐
│ Screen 2: WAITING   │          │  Screen 8: BLE ERROR │
│ (animated, 60s)     │          │  (10s, retry option) │
└──────────┬──────────┘          └──────────┬───────────┘
           │                                 │
    (Connected)                         (Retry)
           │                                 └──────────┐
┌──────────▼──────────┐                                 │
│ Screen 3: CONNECTED │◄─────────────────────────────────
│ (green, progress)   │
└──────────┬──────────┘
           │
  (Creds Received)
           │
┌──────────▼──────────────┐
│ Screen 4: CREDENTIALS   │
│ (SSID, signal strength) │
└──────────┬──────────────┘
           │
┌──────────▼──────────────┐
│ Screen 5: CONNECTING    │
│ (progress bar, 30s)     │
└──────────┬──────────────┘
           │
    ┌──────┴──────┐
    │             │
 SUCCESS       ERROR
    │             │
    │     ┌───────▼──────────┐
    │     │ Screen 7: ERROR  │
    │     │ (red, causes)    │
    │     └──────────┬───────┘
    │                │
┌───▼────────────┐   │ (Retry)
│ Screen 6:      │   │
│ SUCCESS!       │   └────┐
│ (3s countdown) │        │
└───┬────────────┘        │
    │                     │
    │              ┌──────▼─────┐
    │              │ Back to     │
    │              │ Screen 5    │
    │              └─────┬──────┘
    │                    │
    └────────┬───────────┘
             │
    ┌────────▼─────────┐
    │  HOME SCREEN    │ (return)
    └─────────────────┘
```

---

## Implementation Notes

1. **Header Height**: 50px for all screens
2. **Main Content Area**: 70px to 270px (200px available)
3. **Progress Bar**: 200px width (20px left+right margin)
4. **Text Sizes**: 
   - Header: textSize 2
   - Labels: textSize 1
   - Data: textSize 2-3 for emphasis
   - Small: textSize 1 for details

5. **Margins**: 20-30px from left/right edges
6. **Line Spacing**: 20-30px between text lines
7. **Color Contrast**: Always use dark backgrounds with bright text for readability on small LCD screen

---

## Display Manager API Reference

```cpp
// Screen Selection Methods
displayManager->drawWiFiConfigStartScreen();
displayManager->drawBLEWaitingScreen(60);           // 60s remaining
displayManager->drawBLEConnectedScreen(45);         // 45% progress
displayManager->drawCredentialsReceivedScreen("MyWiFi", 80);  // SSID, 80% signal
displayManager->drawWiFiConnectingScreen(50);       // 50% connected
displayManager->drawWiFiConnectSuccessScreen("192.168.1.100");  // IP address
displayManager->drawWiFiConnectErrorScreen("Password Failed");
displayManager->drawBLEErrorScreen(10);             // 10s before auto-return
```

All methods handle `nullptr` gracefully with fallback messages.
