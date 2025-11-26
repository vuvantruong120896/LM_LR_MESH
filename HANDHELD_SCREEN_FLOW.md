# 📱 Luồng Hoạt Động Các Màn Hình - Handheld Device

## 1. Sơ Đồ Trạng Thái (State Machine)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          HANDHELD APP STATE FLOW                            │
└─────────────────────────────────────────────────────────────────────────────┘

                              🔌 BOOT
                                ↓
                    ┌─────────────────────────┐
                    │   INITIALIZING (10s)    │
                    │  - Initialize NVS       │
                    │  - Initialize Display   │
                    │  - Initialize Sensor    │
                    │  - Initialize WiFi      │
                    └────────────┬────────────┘
                                 │
                    ✓ Success     │     ✗ Timeout (10s)
                                 ↓                │
                    ┌─────────────────────┐      │
                    │    IDLE (HOME)      │◄─────┘
                    │  - Show home screen │  → ERROR state
                    │  - Wait for button  │
                    │  - Check battery    │
                    │  - WiFi reconnect   │
                    └────────┬────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   SHORT CLICK           5s LONG PRESS          (other: 1s reserved)
     (trigger)          (config mode)
          │                  │                  │
          ↓                  ↓                  ↓
    ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
    │  MEASURING   │  │ WIFI_CONFIG  │  │   ERROR      │
    │  (Show:      │  │  (Show:      │  │  (Show:      │
    │  "MEASURING" │  │  "CONFIG")   │  │  "ERROR!")   │
    │  "Wait...")  │  │  (60s timeout)  │  │  (30s recov)│
    └──────┬───────┘  └──────┬───────┘  └──────┬───────┘
           │                 │                  │
           │ Sensor data     │ Timeout/Done     │ Recovery timeout
           │ retrieved       │                  │
           ↓                 ↓                  ↓
    ┌──────────────┐  ┌──────────────┐        
    │  SENSOR DATA │  │   IDLE       │  
    │  (Display    │  │  (HOME)      │  ───→ IDLE (LOOP BACK)
    │  results)    │  │              │  
    └──────┬───────┘  └──────────────┘  
           │                              
           └─────────────────────→ IDLE  
                                          

┌─────────────────────────────────────────────────────────────────────────────┐
│ STATES (8 total):                                                           │
│ ✓ INITIALIZING → IDLE (boot)                                              │
│ ✓ IDLE → MEASURING (short click)                                          │
│ ✓ MEASURING → SENSOR_DATA (data ready) → IDLE (display done)             │
│ ✓ IDLE → WIFI_CONFIG (5s long press)                                     │
│ ✓ WIFI_CONFIG → IDLE (60s timeout or done)                               │
│ ✓ ERROR → IDLE (30s auto-recovery)                                       │
│ ✓ SLEEP (reserved for power-save mode)                                   │
│ ✓ UPLOADING (reserved for BLE/Firebase upload)                           │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Sơ Đồ Chi Tiết 8 Màn Hình

```
╔═════════════════════════════════════════════════════════════════════════════╗
║                        SCREEN ARCHITECTURE (8 Screens)                      ║
╚═════════════════════════════════════════════════════════════════════════════╝

┌─ SCREEN 1: INITIAL SCREEN (INITIALIZING) ─────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │                    🌾 KAGRI SYSTEM 🌾                                   │ │
│ │                                                                         │ │
│ │                   Handheld Device v1.0.0                               │ │
│ │                                                                         │ │
│ │                   ⚙️ Initializing...                                    │ │
│ │                   ========================                              │ │
│ │                                                                         │ │
│ │ ▌▌▌▌▌▌░░░░░░░░░░░░░░░░░░░░░  35%                                    │ │
│ │                                                                         │ │
│ │ Duration: 5 seconds → then IDLE                                        │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: Boot                                                               │
│ Next: IDLE (auto, 5s)                                                      │
│ Color: White text on blue background                                       │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 2: HOME SCREEN (IDLE) ───────────────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │                    🏠 HOME                                              │ │
│ │                                                                         │ │
│ │           Status:  WiFi ✓  Battery: 85%                               │ │
│ │                                                                         │ │
│ │                                                                         │ │
│ │     ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                          │ │
│ │     ┃  📊 SENSOR READING (Short Press)    ┃                          │ │
│ │     ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                          │ │
│ │                                                                         │ │
│ │     ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                          │ │
│ │     ┃  🔧 WiFi CONFIG (Long Press 5s)    ┃                          │ │
│ │     ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                          │ │
│ │                                                                         │ │
│ │ Temperature: 28°C                                                       │ │
│ │ Humidity: 65%                                                           │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: INITIALIZING completes                                            │
│ Interact: 📌 SHORT CLICK → MEASURING                                      │
│ Interact: 📌 LONG PRESS (1s) → Reserved                                  │
│ Interact: 📌 EXTENDED PRESS (5s) → WIFI_CONFIG                           │
│ Color: White text, cyan "HOME", green status                              │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 3: MEASURING SCREEN (MEASURING) ─────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │               📊 MEASURING                                              │ │
│ │                                                                         │ │
│ │                  🟢 Please wait...                                      │ │
│ │                                                                         │ │
│ │               ⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧ ⠇ ⠏                                     │ │
│ │             (animated dots loading)                                     │ │
│ │                                                                         │ │
│ │             Reading sensor data...                                      │ │
│ │             Timeout: 30 seconds                                         │ │
│ │                                                                         │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: User presses button (SHORT CLICK) in IDLE                         │
│ Auto-transition: When sensor data arrives → SENSOR_DATA                    │
│ Color: Yellow "MEASURING", green "Please wait", cyan dots                  │
│ Timeout: If no data in 30s → ERROR state                                   │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 4: SENSOR DATA SCREEN (SENSOR_DATA) ─────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │              📈 SENSOR READING RESULTS                                 │ │
│ │                                                                         │ │
│ │  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                         │ │
│ │  ┃  🌡️  Temperature:    28.5°C             ┃                         │ │
│ │  ┃  💧  Moisture:       62.3%              ┃                         │ │
│ │  ┃  ⚡  Conductivity:   1245 μS/cm         ┃                         │ │
│ │  ┃  🟢  Nitrogen:       245 mg/kg          ┃                         │ │
│ │  ┃  🟠  Phosphorus:     156 mg/kg          ┃                         │ │
│ │  ┃  🟡  Potassium:      342 mg/kg          ┃                         │ │
│ │  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                         │ │
│ │                                                                         │ │
│ │  Last read: 2025-11-27 14:35:22                                        │ │
│ │  Status: ✓ Success                                                     │ │
│ │                                                                         │ │
│ │  [Press button to return]                                               │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: Sensor task returns data                                          │
│ Auto-transition: Back to IDLE (user presses button or timeout 60s)         │
│ Color: Cyan title, green values, magenta icons                             │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 5: CONFIG SCREEN (WIFI_CONFIG) ──────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │              🔧 WiFi CONFIGURATION                                     │ │
│ │                                                                         │ │
│ │  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                         │ │
│ │  ┃  Access WiFi Portal:                    ┃                         │ │
│ │  ┃                                          ┃                         │ │
│ │  ┃  🌐 IP: 192.168.1.1                    ┃                         │ │
│ │  ┃  📱 SSID: KAGRI-Setup                  ┃                         │ │
│ │  ┃  🔒 Password: kagri2024                ┃                         │ │
│ │  ┃                                          ┃                         │ │
│ │  ┃  Browser: http://192.168.1.1           ┃                         │ │
│ │  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                         │ │
│ │                                                                         │ │
│ │  ⚙️ Entering AP mode...                                                │ │
│ │  Auto-return to IDLE in: 60 seconds                                    │ │
│ │                                                                         │ │
│ │  [Waiting for configuration...]                                        │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: User presses button for 5s (EXTENDED_PRESS) in IDLE              │
│ Auto-transition: IDLE (60s timeout)                                        │
│ Color: Cyan title, green info, magenta portal details                      │
│ Timeout: 60 seconds → AUTO-RETURN to IDLE                                 │
│ Note: AP Mode activated, WiFi portal not yet implemented                   │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 6: ERROR SCREEN (ERROR) ─────────────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │                   ⚠️ ERROR!                                             │ │
│ │                                                                         │ │
│ │  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                         │ │
│ │  ┃  Sensor Disconnected                    ┃                         │ │
│ │  ┃                                          ┃                         │ │
│ │  ┃  • Check RS485 cable connection        ┃                         │ │
│ │  ┃  • Verify sensor power (5V)            ┃                         │ │
│ │  ┃  • Restart device if problem persists  ┃                         │ │
│ │  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                         │ │
│ │                                                                         │ │
│ │  🔄 Auto-recovering in: 30 seconds                                     │ │
│ │     ▌▌▌▌▌▌▌▌▌░░░░░░░░░░░░░░░░░░  60%                              │ │
│ │                                                                         │ │
│ │  [Press button to skip recovery]                                        │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: Sensor error / System error / Timeout                             │
│ Auto-transition: IDLE (30s timeout)                                        │
│ Color: Red "ERROR!", white text, yellow progress bar                       │
│ Recovery: Auto-return to IDLE after 30s                                    │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 7: SLEEP SCREEN (SLEEP) ────────────────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │                     💤 SLEEP MODE                                       │ │
│ │                                                                         │ │
│ │                      Z  z  z                                            │ │
│ │                     Z  z  z                                             │ │
│ │                    Z  z  z                                              │ │
│ │                                                                         │ │
│ │           Device is in low-power sleep state                            │ │
│ │                                                                         │ │
│ │           Press button to wake up                                       │ │
│ │                                                                         │ │
│ │           Battery: Minimal power consumption                            │ │
│ │                                                                         │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: Display timeout (not fully active yet)                            │
│ Wake: Button press → return to IDLE                                        │
│ Color: Dark background, dim text                                           │
│ Note: Sleep mode state exists but not actively triggered                   │
└─────────────────────────────────────────────────────────────────────────────┘

┌─ SCREEN 8: BLE SENDING SCREEN (UPLOADING) ───────────────────────────────┐
│ ┌─────────────────────────────────────────────────────────────────────────┐ │
│ │                                                                         │ │
│ │            📤 UPLOADING DATA                                            │ │
│ │                                                                         │ │
│ │            Transferring via Bluetooth...                                │ │
│ │                                                                         │ │
│ │  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                         │ │
│ │  ┃ Progress: 65%                           ┃                         │ │
│ │  ┃                                          ┃                         │ │
│ │  ┃ ███████████░░░░░░░░░░░░░░░░░░░░░░░░░░ ┃                         │ │
│ │  ┃                                          ┃                         │ │
│ │  ┃ Uploading... 1.2 MB / 1.9 MB           ┃                         │ │
│ │  ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛                         │ │
│ │                                                                         │ │
│ │  ⏱️  Estimated time: 45 seconds                                         │ │
│ │  📡 Signal: ▓▓▓▓▓░░░░ (Strong)                                         │ │
│ │                                                                         │ │
│ │  [Press to cancel]                                                      │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ Trigger: User initiates BLE/Firebase upload (NOT YET IMPLEMENTED)          │
│ Auto-transition: IDLE (when upload completes)                              │
│ Color: Cyan title, green progress bar, white text                          │
│ Note: UI implemented, BLE logic pending implementation                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Bảng Tóm Tắt Trạng Thái

| # | Màn Hình | State | Trigger | Next State | Timeout | Note |
|---|----------|-------|---------|-----------|---------|------|
| 1 | 🔌 Initial | INITIALIZING | Boot | IDLE | 10s | Splash screen, loading progress |
| 2 | 🏠 Home | IDLE | System ready | MEASURING / WIFI_CONFIG | — | Waits for button input |
| 3 | 📊 Measuring | MEASURING | Short click | SENSOR_DATA | 30s | Shows loading animation |
| 4 | 📈 Sensor Data | SENSOR_DATA | Data ready | IDLE | — | Displays all 6 metrics |
| 5 | 🔧 Config | WIFI_CONFIG | 5s long press | IDLE | 60s | Portal @ 192.168.1.1 |
| 6 | ⚠️ Error | ERROR | Error event | IDLE | 30s | Auto-recovery with countdown |
| 7 | 💤 Sleep | SLEEP | Timeout (inactive) | IDLE | — | Low power state (reserved) |
| 8 | 📤 Uploading | UPLOADING | Manual trigger (inactive) | IDLE | — | BLE/Firebase transfer (reserved) |

---

## 4. Luồng Sự Kiện Nút (Button Events)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        BUTTON EVENT FLOW                                 │
└──────────────────────────────────────────────────────────────────────────┘

    GPIO3 (Button) with Pull-Up
           ↓
    ISR triggers on CHANGE (falling/rising edge)
           ↓
    button_update() in main loop processes event
           ↓
    ┌──────────────────────────────────────────┐
    │         EVENT TYPE DETECTION             │
    └──────────────────────────────────────────┘
           ↓
    ┌──────────────────┬──────────────────┬──────────────────┐
    │                  │                  │                  │
    ↓                  ↓                  ↓                  ↓
CLICK              LONG_PRESS(1s)   EXTENDED_PRESS(5s)  MULTIPLE_CLICKS
(< 1s)             (1s - 5s)        (> 5s)              (reserved)
    │                  │                  │                  │
    │                  │                  │                  │
    ↓                  ↓                  ↓                  ↓
MEASURING          Reserved         WIFI_CONFIG          (unused)
(trigger sensor)   (1s reserved)    (enter setup)
    │                                   │
    └─────────────────┬─────────────────┘
                      │
              ┌───────────────────┐
              │ handleButtonPress │
              └────────┬──────────┘
                       │
            ┌──────────┴──────────┐
            │                     │
      drawMeasuring         drawConfig
      changeState(            changeState(
        MEASURING)              WIFI_CONFIG)
      trigger sensor task    start AP mode

EVENT HANDLING (handleButtonPress):
┌─────────────────────────────────────────────────────────────┐
│ if (event == BUTTON_EVENT_CLICK) {                         │
│   // Trigger sensor reading                                │
│   SensorTaskManager::triggerRead()                         │
│   displayManager->drawMeasuringScreen()                    │
│   changeState(AppState::MEASURING)                         │
│ }                                                           │
│ else if (event == BUTTON_EVENT_EXTENDED_PRESS) {          │
│   // 5s press: WiFi config                                │
│   displayManager->drawConfigScreen()                       │
│   changeState(AppState::WIFI_CONFIG)                       │
│ }                                                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. Luồng Dữ Liệu Cảm Biến (Sensor Data Flow)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                     SENSOR READING FLOW (On-Demand)                      │
└──────────────────────────────────────────────────────────────────────────┘

┌───────────────────┐
│  User Presses     │
│  Button (CLICK)   │
└────────┬──────────┘
         │
         ↓
┌────────────────────────────────────┐
│ handleButtonPress() triggered      │
│ BUTTON_EVENT_CLICK                 │
└────────┬───────────────────────────┘
         │
         ├─→ SensorTaskManager::triggerRead()
         │   └─→ Enqueue command to sensorTriggerQueue
         │
         ├─→ displayManager->drawMeasuringScreen()
         │   └─→ Show "MEASURING" + "Please wait" animation
         │
         └─→ changeState(AppState::MEASURING)
            └─→ Set currentState = MEASURING
                └─→ Record stateChangeTime = millis()

                        [MEASURING STATE ACTIVE]

         ↓
┌────────────────────────────────────┐
│  Main Loop (loop() function)       │
│  switch(currentState)              │
│    case MEASURING: {               │
└────────┬───────────────────────────┘
         │
         ├─→ SensorTaskManager::getData(newData)
         │   └─→ Check if new sensor data available
         │       (FreeRTOS sensor task reads Modbus sensor)
         │
         └─→ If data ready: {
            ├─→ lastSensorReading = newData
            ├─→ hasNewSensorData = true
            ├─→ displayManager->displaySensorData(...)
            │   └─→ Show all 6 metrics:
            │       • Temperature (°C)
            │       • Moisture (%)
            │       • Conductivity (μS/cm)
            │       • Nitrogen (mg/kg)
            │       • Phosphorus (mg/kg)
            │       • Potassium (mg/kg)
            └─→ changeState(AppState::IDLE)
                └─→ Return to IDLE state

[Sensor Task FreeRTOS Background Process]:
┌──────────────────────────────────────────────┐
│ sensorTask():                                │
│  while(1) {                                  │
│    xQueueReceive(sensorTriggerQueue)  ← Blocks
│      ↓ [Command received from triggerRead]  │
│    rs485_read_soil_sensor()           ← Modbus
│      ↓ [Data from IIC addressable sensor]   │
│    sensorDataQueue.push(data)         ← Store
│  }                                           │
└──────────────────────────────────────────────┘
```

---

## 6. Bảng Màu (Color Scheme)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         COLOR CONSTANTS (RGB565)                        │
└─────────────────────────────────────────────────────────────────────────┘

BACKGROUND & TEXT:
  • TFT_BLACK       = 0x0000  (Background)
  • TFT_WHITE       = 0xFFFF  (Primary text)
  • TFT_GRAY        = 0x7BEF  (Secondary text)

STATUS & TITLES:
  • TFT_CYAN        = 0x07FF  (Titles, emphasis)
  • TFT_GREEN       = 0x07E0  (Success, positive values)
  • TFT_MAGENTA     = 0xF81F  (Icons, decorative)
  • TFT_YELLOW      = 0xFFE0  (Warnings, important info)
  • TFT_RED         = 0xF800  (Errors, critical)
  • TFT_ORANGE      = 0xFD20  (Temperature, warm)

DATA DISPLAY:
  • Temperature icon: ORANGE (🌡️)
  • Moisture icon: CYAN (💧)
  • Nitrogen: GREEN (🟢)
  • Phosphorus: ORANGE (🟠)
  • Potassium: YELLOW (🟡)
  • Conductivity: MAGENTA (⚡)

STATUS INDICATORS:
  • WiFi Connected: GREEN ✓
  • WiFi Failed: RED ✗
  • Battery Warning: YELLOW ⚠️
  • Error: RED ⚠️
  • Loading: CYAN ⠋ (spinner)
```

---

## 7. Bảng Kích Thước Màn Hình

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    DISPLAY DIMENSIONS & LAYOUT                         │
└─────────────────────────────────────────────────────────────────────────┘

DISPLAY: ILI9341 (240x320 pixels) - Portrait Mode
├─ Width: 240 px
├─ Height: 320 px
├─ Color: RGB565 (16-bit)
└─ Refresh: ~20 FPS

LAYOUT ZONES:
┌─────────────────────────────────────────┐
│ HEADER (0-40px)                         │  Title + Status bar
├─────────────────────────────────────────┤
│                                         │
│ CONTENT (40-280px)                      │  Main data display
│ (240px height)                          │
│                                         │
├─────────────────────────────────────────┤
│ FOOTER (280-320px)                      │  Status indicators
└─────────────────────────────────────────┘

TEXT SIZES:
  • Title: Large font (~24px height)
  • Labels: Medium font (~16px height)
  • Values: Large font (~20px height)
  • Status: Small font (~12px height)

SPACING:
  • Left/Right margin: 15px
  • Top margin: 20px
  • Between lines: 10px
  • Box padding: 10px
```

---

## 8. Trạng Thái Nút (Button States)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    BUTTON STATE MACHINE (GPIO3)                         │
└─────────────────────────────────────────────────────────────────────────┘

PIN CONFIGURATION:
  • GPIO: 3
  • Mode: INPUT_PULLUP (active low)
  • ISR: CHANGE (both edges)
  • Pull-up: Internal 10kΩ

STATE TRANSITIONS:
┌──────────┐  Release   ┌──────────┐
│ RELEASED │◄─────────►│ PRESSED  │
│ (HIGH)   │   Press   │ (LOW)    │
└─────┬────┘           └────┬─────┘
      │ ↑                    │ ↑
      │ │                    │ │
      │ └─ Debounce 50ms ◄───┘ │
      │                        │
      └─ ISR CHANGE ◄──────────┘

PRESS DURATION THRESHOLDS:
  0 ├─────────────────────┬────────────┬──────────────┐
  │  CLICK            1s   │    5s      │    (future)  │
1s ├─────────────────────┬────────────┤
5s │ LONG_PRESS_TIME      │            │
   │ (1s reserved)        │            │
   └─ EXTENDED_PRESS_TIME │            │
      (5s reserved)       │            │
      ↓                   ↓            ↓
  CLICK              LONG_PRESS   EXTENDED_PRESS
  (< 1s)             (1-5s)       (> 5s)
  │                  │            │
  └─→ Sensor read    │            └─→ WiFi config
     MEASURING       │               WIFI_CONFIG
                     │
                     └─→ Reserved

CURRENT TIME MEASUREMENT:
  Press detected: record time_pressed = millis()
  Release detected: time_released = millis()
  Duration = time_released - time_pressed

  IF Duration < 1000ms:
    → BUTTON_EVENT_CLICK

  ELSE IF Duration < 5000ms:
    → BUTTON_EVENT_LONG_PRESS (reserved)

  ELSE IF Duration >= 5000ms:
    → BUTTON_EVENT_EXTENDED_PRESS (WiFi config)

DEBOUNCE:
  Hardware + Software
  • Capacitor on GPIO3 (RC filter)
  • 50ms software debounce in button_update()
```

---

## 9. Lưu Đồ Thời Gian Khởi Động (Boot Timeline)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    STARTUP SEQUENCE (TIMELINE)                          │
└─────────────────────────────────────────────────────────────────────────┘

Time    Event                          State          Screen
────    ─────────────────────────────  ─────────────  ──────────────────────
0ms     Power on
        ├─ Setup()
        │  ├─ Serial init
        │  ├─ GPIO setup
        │  └─ FreeRTOS init
        │

50ms    HandheldApp::initialize()
        │
100ms   ├─ NVS init                   INITIALIZING   [no display yet]
        │
150ms   ├─ Display init
        │
200ms   ├─ displayManager→         
        │   drawInitialScreen()       INITIALIZING   🔌 INITIAL SCREEN
        │  "KAGRI SYSTEM"             │              (shows 5s splash)
        │  Loading... ▌▌▌░░░░░░       │
        │

500ms   ├─ Sensor init               INITIALIZING    (loading continues)
        │
1000ms  ├─ Button init               INITIALIZING    (loading continues)
        │
1500ms  ├─ WiFi init                 INITIALIZING    (loading continues)
        │
2000ms  ├─ Firebase init (skipped)   INITIALIZING    (loading continues)
        │

5000ms  └─ changeState(IDLE)         IDLE            🏠 HOME SCREEN
        └─ drawHomeScreen()                          (ready for input)
        
                                      [SYSTEM READY]


INITIALIZATION CHECKS:
  ✓ NVS Flash initialized
  ✓ Display initialized (LovyanGFX)
  ✓ Sensor task created
  ✓ Button ISR attached
  ✓ WiFi configured (from NVS)
  ✓ Ready to accept button input

MEMORY STATE AT STARTUP:
  • RAM used: 48,044 bytes (14.7%)
  • Flash used: 875,461 bytes (27.8%)
  • Free heap: ~279 KB
```

---

## 10. Ký Hiệu & Huyền Thoại (Legend)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           LEGEND & SYMBOLS                              │
└─────────────────────────────────────────────────────────────────────────┘

ARROWS:
  → Forward flow / transition
  ← Backward flow / return
  ↓ Downward flow / enter
  ↑ Upward flow / exit
  ◄─ Bidirectional
  ─→ State change
  └─ Branch / alternate path

STATE INDICATORS:
  ✓ Complete / Success
  ✗ Failed / Timeout
  ⚠️ Warning / Error
  🔄 Loop / Repeat
  ⏱️ Timer / Timeout
  🔌 Power / Boot

SCREEN STATES:
  🏠 Home (IDLE)
  📊 Measuring (MEASURING)
  📈 Sensor Data (SENSOR_DATA)
  🔧 Config (WIFI_CONFIG)
  ⚠️ Error (ERROR)
  💤 Sleep (SLEEP)
  📤 Uploading (UPLOADING)
  🔌 Initial (INITIALIZING)

INTERACTION:
  📌 Button input
  🎯 Auto-transition
  ⏳ Timeout trigger
  ✅ User action
  🔗 System event

TIMING:
  ms = milliseconds
  s  = seconds
  ⏱️ = timeout/duration
```

---

## 📋 Tóm Tắt

### Qui Trình Chính:
1. **Boot** → INITIALIZING (5s splash) → IDLE (HOME)
2. **User presses button** → MEASURING (show loading) → MEASURING (sensor reads) → SENSOR_DATA (show results) → IDLE
3. **User presses 5s** → WIFI_CONFIG (show portal) → timeout 60s → IDLE
4. **Error occurs** → ERROR (show message) → timeout 30s auto-recovery → IDLE

### Các Trạng Thái:
- **8 states** với 8 màn hình tương ứng
- **Button events**: CLICK (sensor), EXTENDED_PRESS (config), LONG_PRESS (reserved)
- **Auto-transitions**: MEASURING → SENSOR_DATA → IDLE, WIFI_CONFIG → IDLE (60s), ERROR → IDLE (30s)
- **On-demand sensor**: Chỉ đọc khi người dùng bấm nút (không tự động)

### Công Nghệ:
- **Display**: LovyanGFX only (không LVGL)
- **Sensor**: On-demand via trigger queue
- **Button**: ISR + polling, 1s/5s long-press support
- **Memory**: 27.8% Flash, 14.7% RAM (tối ưu)

