# KAGRI Handheld Device - UI Design & Implementation

## 🎨 Complete UI Architecture

### Display Specifications
```
Hardware: ILI9341 240x320 TFT LCD
Resolution: 240 pixels (width) × 320 pixels (height)
Color Depth: 16-bit RGB565
Orientation: 180° rotation (landscape mode)
Backlight: GPIO17 (PWM control)
```

## 📱 Screen Hierarchy

```
                    SPLASH (3 seconds)
                         ↓
                    ┌─────HOME─────┐
                    │              │
        ┌───────────┘              └───────────┐
        ↓                                       ↓
   SOIL_DATA                            DEVICE_CONFIG
   (Display 7                               ├─ WiFi Config
    parameters)                            └─ Sensor Config
        ↓                                     ↓
    (Back)                              (Back to HOME)
        ↓
    HOME
```

## 🏠 Screen 1: HOME (Main Menu)

**Layout:**
```
┌────────────────────────────────┐
│ [Dark Blue Bar]  KAGRI HOME    │
├────────────────────────────────┤
│                                │
│  ┌──────────────────────────┐  │
│  │ 1. Read Soil Data        │  │  50px height
│  │ [Unselected - Dark Blue] │  │  + 15px spacing
│  └──────────────────────────┘  │
│                                │
│  ┌──────────────────────────┐  │
│  │ 2. Device Config         │  │
│  │ [Unselected - Dark Blue] │  │
│  └──────────────────────────┘  │
│                                │
│                                │
│  Press button to select        │
│  (Yellow text, small font)     │
│                                │
└────────────────────────────────┘
```

**Colors:**
- Header Background: Dark Blue (0x0841)
- Title Text: White (0xFFFF), scale=2
- Unselected Button: Dark Blue (0x4208) with white border
- Selected Button: Green (0x07E0) with white border
- Footer Text: Yellow (0x7BEF), scale=1
- Background: Black (0x0000)

**Interactions:**
- **Short Press (Button Click):**
  - Menu item 1 (0): Read Soil Data → Go to SOIL_DATA screen
  - Menu item 2 (1): Device Config → Go to DEVICE_CONFIG screen

- **Long Press (3+ seconds):**
  - Read sensor + Upload to Firebase

## 📊 Screen 2: SOIL_DATA (7 Parameters Display)

**Layout:**
```
┌────────────────────────────────┐
│ [Dark Blue]  SOIL DATA         │
├────────────────────────────────┤
│                                │
│ Moisture: 45.3 %               │ Line 1: Y=50
│                                │
│ Temp: 28.5 C                   │ Line 2: Y=82
│                                │
│ pH: 6.8                        │ Line 3: Y=114
│                                │
│ EC: 2.1 mS/cm                  │ Line 4: Y=146
│                                │
│ Salinity: 0.89                 │ Line 5: Y=178
│                                │
│ VWC: 45.3 %                    │ Line 6: Y=210
│                                │
│ ROH: 78.5                      │ Line 7: Y=242
│                                │
│  ┌──────────────────────────┐  │
│  │ < Back                   │  │
│  └──────────────────────────┘  │
└────────────────────────────────┘
```

**7 Parameters Displayed:**
1. **Soil Moisture** - Percentage (%)
2. **Temperature** - Celsius (°C)
3. **pH** - pH value (0-14)
4. **EC** - Electrical Conductivity (mS/cm)
5. **Salinity** - Salinity index
6. **VWC** - Volumetric Water Content (%)
7. **ROH** - Relative humidity equivalent

**Text Formatting:**
- Each parameter: White (0xFFFF), scale=2
- Y spacing: 32 pixels between lines
- X position: 15 pixels from left

**Interactions:**
- **Any Button Press:** Back to HOME screen

## ⚙️ Screen 3: DEVICE_CONFIG (Settings Menu)

**Layout:**
```
┌────────────────────────────────┐
│ [Dark Blue] DEVICE CONFIG      │
├────────────────────────────────┤
│                                │
│  ┌──────────────────────────┐  │
│  │ 1. WiFi Config           │  │  50px height
│  │ [Unselected - Dark Blue] │  │  + 15px spacing
│  └──────────────────────────┘  │
│                                │
│  ┌──────────────────────────┐  │
│  │ 2. Sensor Config         │  │
│  │ [Unselected - Dark Blue] │  │
│  └──────────────────────────┘  │
│                                │
│                                │
│                                │
│  ┌──────────────────────────┐  │
│  │ < Back                   │  │
│  │                          │  │
│  └──────────────────────────┘  │
└────────────────────────────────┘
```

**Interactions:**
- **Config Option 1 (Short Press):** WiFi Config → WIFI_CONFIG screen
- **Config Option 2 (Short Press):** Sensor Config → SENSOR_CONFIG screen
- **Back Button:** Return to HOME

## 📶 Screen 4: WIFI_CONFIG (WiFi Configuration)

**Layout:**
```
┌────────────────────────────────┐
│ [Dark Blue]  WiFi CONFIG       │
├────────────────────────────────┤
│                                │
│ Status: Connected              │  Green if connected
│ (or: Status: Not Connected)    │  Red if disconnected
│                                │
│ SSID: MyNetwork                │  Yellow if not set
│ (or: SSID: Not Set)            │
│                                │
│                                │
│ Use BLE to configure           │  Info text
│ WiFi credentials               │  (Yellow, small)
│                                │
│                                │
│                                │
│  ┌──────────────────────────┐  │
│  │ < Back                   │  │
│  │                          │  │
│  └──────────────────────────┘  │
└────────────────────────────────┘
```

**Status Indicators:**
- **Connected:** Green text (0x07E0)
- **Not Connected:** Red text (0xF800)
- **SSID Text:** White (0xFFFF) or Yellow (0x7BEF)

**Interactions:**
- **Back Button:** Return to DEVICE_CONFIG

## 🌡️ Screen 5: SENSOR_CONFIG (Sensor Settings)

**Layout:**
```
┌────────────────────────────────┐
│ [Dark Blue] SENSOR CONFIG      │
├────────────────────────────────┤
│                                │
│ Sensor: RS485 Soil             │
│                                │
│ Status: OK                     │  Green text
│                                │
│ Calibration:                   │
│                                │
│ - Press button to              │  Info text
│   recalibrate                  │  (Yellow, small)
│                                │
│                                │
│  ┌──────────────────────────┐  │
│  │ < Back                   │  │
│  │                          │  │
│  └──────────────────────────┘  │
└────────────────────────────────┘
```

**Interactions:**
- **Back Button:** Return to DEVICE_CONFIG

## 🎨 Color Palette

```cpp
const uint16_t COLOR_BLACK         = 0x0000;  // Background
const uint16_t COLOR_WHITE         = 0xFFFF;  // Text
const uint16_t COLOR_DARK_BLUE     = 0x0841;  // Header
const uint16_t COLOR_BUTTON_DARK   = 0x4208;  // Unselected button
const uint16_t COLOR_BUTTON_GREEN  = 0x07E0;  // Selected button
const uint16_t COLOR_YELLOW        = 0x7BEF;  // Info text
const uint16_t COLOR_RED           = 0xF800;  // Error/Disconnected
const uint16_t COLOR_GREEN         = 0x07E0;  // Connected/OK
const uint16_t COLOR_GRAY          = 0x2945;  // Separator
```

## 📐 Layout Constants

```cpp
// Dimensions (pixels)
DISPLAY_WIDTH       = 240
DISPLAY_HEIGHT      = 320

// Spacing
HEADER_HEIGHT       = 32
BUTTON_WIDTH        = 200
BUTTON_HEIGHT       = 50
BUTTON_SPACING      = 15
LINE_HEIGHT         = 32
TEXT_X_OFFSET       = 15

// Text Scaling
HEADER_SCALE        = 2  // "KAGRI HOME" etc
DATA_SCALE          = 2  // Sensor values
INFO_SCALE          = 1  // Small info text
```

## 🔄 User Flow

### Scenario 1: Read Sensor Data
```
HOME (display "1. Read Soil Data" + "2. Device Config")
  ↓ (user presses button)
SOIL_DATA (display 7 parameters)
  ↓ (read from sensor queue)
  ├─ Moisture: 45.3 %
  ├─ Temp: 28.5 °C
  ├─ pH: 6.8
  ├─ EC: 2.1 mS/cm
  ├─ Salinity: 0.89
  ├─ VWC: 45.3 %
  └─ ROH: 78.5
  ↓ (user presses button again)
HOME
```

### Scenario 2: Device Configuration
```
HOME (display menu)
  ↓ (user selects "Device Config")
DEVICE_CONFIG (display "1. WiFi Config" + "2. Sensor Config")
  ↓ (user presses button for WiFi)
WIFI_CONFIG (display "Status: Not Connected", "SSID: Not Set")
  ↓ (user presses button to go back)
DEVICE_CONFIG
  ↓ (user presses button for Sensor Config)
SENSOR_CONFIG (display "Sensor: RS485 Soil", "Status: OK")
  ↓ (user presses button to go back)
DEVICE_CONFIG
  ↓ (user presses button to go back)
HOME
```

### Scenario 3: Quick Upload
```
HOME (any screen)
  ↓ (user holds button 3+ seconds)
LONG PRESS DETECTED
  ├─ readSensorManual()
  └─ uploadNow()
  ↓
Upload to Firebase (if WiFi connected)
  ↓
Stay on current screen
```

## 🔌 Button Behavior

| Location | Action | Result |
|----------|--------|--------|
| HOME | Short Press | Select menu item |
| HOME | Long Press (3s) | Read sensor + Upload |
| SOIL_DATA | Any Press | Back to HOME |
| DEVICE_CONFIG | Short Press | Enter selected config |
| DEVICE_CONFIG | Any Press on Back | Back to HOME |
| WIFI_CONFIG | Any Press on Back | Back to DEVICE_CONFIG |
| SENSOR_CONFIG | Any Press on Back | Back to DEVICE_CONFIG |

## 📊 Implementation Details

### DisplayManager Methods

```cpp
// Screen management
void showScreen(DisplayScreen screen);
DisplayScreen getCurrentScreen();

// Screen rendering
void drawHomeScreen();
void drawSoilDataScreen(float moisture, float temp, float ph, float ec, 
                        float salinity, float vwc, float roh);
void drawDeviceConfigScreen(int selected);
void drawWiFiConfigScreen(const char* ssid, bool connected);
void drawSensorConfigScreen();

// UI helpers
void drawHeader(const char* title);
void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, 
                const char* text, bool selected);
void drawSeparator(int16_t y);
```

### HandheldApp Navigation

```cpp
// Member variables
DisplayScreen currentUIScreen;      // Current screen
int menuSelection;                  // For multi-select menus

// Navigation methods
void handleMenuNavigation(button_event_t event);
void handleSoilDataScreen();
void handleConfigScreen();
```

## 🚀 Startup Sequence

1. **Power On**
   - Initialize display
   - Show "KAGRI" splash (3 seconds)
   - Buzzer beeps 500ms

2. **Display HOME Screen**
   - Background: Black
   - Menu options: "1. Read Soil Data" + "2. Device Config"
   - Wait for user interaction

3. **Ready for Input**
   - Button press → Navigate menus
   - Button long press → Read + Upload

## 💾 Memory Usage

**Flash Impact:**
- Screen rendering code: ~2 KB
- UI strings/text: ~500 bytes
- Total: ~2.5 KB additional

**RAM Impact:**
- Display buffers: Negligible (uses SPI streaming)
- State variables: ~20 bytes

**Current Build Status:**
- RAM: 14.3% (46,912 / 327,680 bytes)
- Flash: 34.6% (1,088,089 / 3,145,728 bytes)

## ✅ Completed Features

- ✅ HOME screen with 2 menu items
- ✅ SOIL_DATA screen with 7 parameters
- ✅ DEVICE_CONFIG with 2 sub-options
- ✅ WIFI_CONFIG screen
- ✅ SENSOR_CONFIG screen
- ✅ Button navigation (short press = select, long press = action)
- ✅ Color-coded UI elements
- ✅ Responsive screen transitions
- ✅ Back navigation support

## 🔄 Future Enhancements

- [ ] Touch screen integration
- [ ] Animation/transitions
- [ ] Settings persistence
- [ ] Real-time data updates
- [ ] Battery level indicator on all screens
- [ ] WiFi signal strength indicator
- [ ] Scrollable lists for large data
- [ ] Data logging/history view
