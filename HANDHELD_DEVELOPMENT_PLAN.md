# ESP32 Handheld Device Development Plan

## Project Overview

Phát triển thiết bị handheld ESP32-S3 cho hệ thống KAgri với các tính năng:
- Đo cảm biến đất qua RS485
- Hiển thị LCD TFT 2.4" cảm ứng
- Kết nối WiFi upload Firebase
- Giao diện người dùng thân thiện
- Pin sạc, vận hành cầm tay

## Hardware Requirements

### ESP32-S3 Development Board
- **MCU**: ESP32-S3 với 16MB Flash
- **RAM**: 8MB PSRAM (recommended)
- **Connectivity**: WiFi 802.11 b/g/n
- **GPIO**: Đủ pin cho LCD, RS485, buttons

### LCD Display
- **Model**: ILI9341 240x320 TFT với cảm ứng điện dung
- **Interface**: SPI (4-wire + touch)
- **Size**: 2.4 inch
- **Colors**: 65K (16-bit RGB565)

### Soil Sensor Interface
- **Protocol**: Modbus RTU qua RS485
- **Sensors**: 7-in-1 soil sensor (NPK + pH + EC + Moisture + Temperature)
- **Connection**: UART với RS485 transceiver

### User Interface
- **Buttons**: 2 physical buttons (Upload + Menu)
- **Touch**: Capacitive touch screen
- **LED**: Status indication

### Power Management
- **Battery**: Li-ion 3.7V (3000-5000mAh recommended)
- **Charging**: USB-C hoặc Micro USB
- **Runtime**: 8-12 hours continuous use

## Software Architecture

### 1. Application Layer
```
src/application/app_handheld/
├── handheld_app.h/cpp          # Main application controller
├── handheld_config.h           # Configuration constants
└── firebase_uploader.h         # Firebase upload client
```

### 2. Component Layer
```
src/components/
├── rs485_soil_sensor/          # Soil sensor driver (reused)
├── button_led/                 # Button handling (reused)
├── tft_display/                # New: TFT LCD component
└── lora_mesh_manager/          # WiFi service only (partial reuse)
```

### 3. Utility Layer
```
src/utils/                      # Common utilities (reused)
src/application/common/         # Shared definitions (reused)
```

## Development Phases

### Phase 1: Platform Setup ✅
- [x] PlatformIO environment configuration
- [x] Device type definitions
- [x] Main application structure
- [x] Component architecture planning

### Phase 2: Core Components (Week 1)
- [ ] **TFT Display Driver**
  - [ ] ILI9341 SPI initialization
  - [ ] Touch screen calibration
  - [ ] Basic drawing functions
  - [ ] Screen management
  
- [ ] **WiFi Integration**
  - [ ] Reuse WiFiConnectionService
  - [ ] Configuration management
  - [ ] Auto-reconnection logic
  
- [ ] **Firebase Client**
  - [ ] Simplify from gateway version
  - [ ] Direct sensor data upload
  - [ ] Offline queue management

### Phase 3: User Interface (Week 2)
- [ ] **Screen Layouts**
  - [ ] Splash screen with logo
  - [ ] Home screen with sensor data
  - [ ] Menu system navigation
  - [ ] WiFi configuration screen
  - [ ] System information display
  
- [ ] **Touch Navigation**
  - [ ] Button touch areas
  - [ ] Screen transitions
  - [ ] User feedback (visual/haptic)

### Phase 4: Sensor Integration (Week 3)
- [ ] **RS485 Soil Sensor**
  - [ ] Reuse existing sensor task
  - [ ] Periodic reading (5-minute intervals)
  - [ ] Data validation and storage
  - [ ] Error handling and recovery
  
- [ ] **Data Management**
  - [ ] Local data caching
  - [ ] Offline storage (NVS)
  - [ ] Upload queue management

### Phase 5: System Integration (Week 4)
- [ ] **Button Handling**
  - [ ] Upload button (IO3) function
  - [ ] Menu button (IO0) navigation
  - [ ] Long-press for WiFi config
  - [ ] Power management triggers
  
- [ ] **Power Management**
  - [ ] Display timeout and sleep
  - [ ] Battery monitoring
  - [ ] Low power modes
  - [ ] Charging indication

### Phase 6: Testing & Optimization (Week 5)
- [ ] **Functional Testing**
  - [ ] Sensor reading accuracy
  - [ ] WiFi connectivity stability
  - [ ] Firebase upload reliability
  - [ ] User interface responsiveness
  
- [ ] **Performance Optimization**
  - [ ] Memory usage optimization
  - [ ] Battery life improvement
  - [ ] Display refresh optimization
  - [ ] Error handling robustness

## Technical Implementation

### PlatformIO Configuration
```ini
[env:esp32-handheld]
platform = espressif32
framework = arduino
board = 4d_systems_esp32s3_gen4_r8n16
build_flags = 
    -D DEVICE_MODE=4
    -D ENABLE_HANDHELD_MODE
    -D USE_TFT_DISPLAY
    -D ILI9341_DRIVER=1
    # TFT pin definitions...
lib_deps = 
    bblanchon/ArduinoJson@^7.0.4
    mobizt/Firebase ESP32 Client@^4.4.17
    bodmer/TFT_eSPI@^2.5.43
```

### Pin Assignment
```cpp
// LCD TFT Pins
#define TFT_CS      15    // Chip select
#define TFT_DC      2     // Data/Command  
#define TFT_RST     4     // Reset
#define TFT_MOSI    23    // SPI MOSI
#define TFT_SCLK    18    // SPI Clock
#define TOUCH_CS    5     // Touch CS

// User Interface
#define BUTTON_UPLOAD  3  // Upload data button
#define BUTTON_MENU    0  // Menu/back button

// RS485 Interface  
#define RS485_TX    17    // UART2 TX
#define RS485_RX    16    // UART2 RX
#define RS485_DE    21    // Driver Enable
```

### Code Reuse Strategy

#### From Gateway (app_gateway/)
- ✅ Firebase authentication logic
- ✅ WiFi connection management
- ✅ JSON data formatting
- ✅ Error handling patterns

#### From Node (app_node/)
- ✅ Basic application structure
- ✅ Configuration management
- ✅ NVS data storage

#### From Components
- ✅ **rs485_soil_sensor/**: Complete reuse
- ✅ **button_led/**: Button handling only
- ⚠️ **lora_mesh_manager/**: WiFi service only
- ❌ **cellular/**: Not needed

### Data Flow

```
[Soil Sensor] --RS485--> [ESP32-S3] --WiFi--> [Firebase]
       |                       |                    |
       |                 [TFT Display]         [Mobile App]
       |                       |                    |
   [User Press]  <--Touch--  [User]  <--Data--  [Dashboard]
```

### State Management

```cpp
enum class AppState {
    INITIALIZING,    // Boot and component setup
    IDLE,           // Normal operation
    MEASURING,      // Reading sensor data  
    UPLOADING,      // Uploading to Firebase
    SLEEP,          // Display off, low power
    ERROR,          // Error condition
    WIFI_CONFIG     // WiFi setup mode
};
```

## Key Features Implementation

### 1. Sensor Data Display
- Real-time readings on main screen
- Historical data graphs (optional)
- Color-coded warnings for out-of-range values
- Units and measurement context

### 2. Manual Upload Function  
- One-touch upload via IO3 button
- Visual progress indication
- Success/failure feedback
- Offline queue when no connectivity

### 3. Power Management
- 2-minute display timeout
- Wake on button/touch
- Battery level indicator
- Charging status display

### 4. Configuration Management
- WiFi credentials via touch interface
- Sensor calibration settings
- Upload frequency configuration
- Factory reset capability

## Expected Timeline

| Phase | Duration | Deliverables |
|-------|----------|--------------|
| 1 | Completed | PlatformIO setup, architecture |
| 2 | 1 week | Core components working |
| 3 | 1 week | Basic UI functional |
| 4 | 1 week | Sensor integration complete |
| 5 | 1 week | Full system integration |
| 6 | 1 week | Testing and optimization |
| **Total** | **5 weeks** | **Production-ready handheld device** |

## Success Criteria

1. **Sensor Accuracy**: ±2% accuracy compared to reference
2. **Battery Life**: Minimum 8 hours continuous operation
3. **Upload Reliability**: 99%+ success rate with stable WiFi
4. **User Experience**: Intuitive operation with <10 second learning curve
5. **Robustness**: 48+ hours continuous operation without reset

## Next Steps

1. **Immediate**: Test PlatformIO build with new environment
2. **Week 1**: Implement TFT display basic functionality
3. **Week 2**: Add Firebase upload capability  
4. **Week 3**: Integrate RS485 sensor reading
5. **Week 4**: Complete user interface and testing

## Risk Mitigation

### Technical Risks
- **Display compatibility**: Test with actual hardware early
- **Memory constraints**: Monitor heap usage throughout development
- **Power consumption**: Implement sleep modes from day 1

### Schedule Risks  
- **Component delays**: Have backup display options
- **Integration issues**: Build incrementally, test frequently
- **Performance problems**: Profile early and often

---

**Project Status**: Phase 1 Complete ✅  
**Next Milestone**: TFT Display Basic Functionality  
**Target Completion**: 5 weeks from start