# TFT Display Component

This component provides display management for ILI9341 TFT LCD with touch screen support for the handheld device.

## Features

- **Display Management**: Full control of ILI9341 240x320 TFT display
- **Touch Screen**: Capacitive touch screen support with calibration
- **Multiple Screens**: Home, Menu, WiFi Config, System Info, Error displays
- **Sensor Visualization**: Real-time display of soil sensor data
- **Power Management**: Display sleep/wake functionality
- **User Interface**: Touch-based navigation with buttons

## Hardware Requirements

- **ESP32-S3** microcontroller
- **ILI9341** 240x320 TFT LCD display
- **Capacitive touch** screen overlay
- **SPI interface** for display communication

## Pin Configuration

| Function | GPIO Pin | Description |
|----------|----------|-------------|
| TFT_CS   | 15       | Chip Select |
| TFT_DC   | 2        | Data/Command |
| TFT_RST  | 4        | Reset |
| TFT_MOSI | 23       | SPI MOSI |
| TFT_SCLK | 18       | SPI Clock |
| TFT_MISO | 19       | SPI MISO (optional) |
| TOUCH_CS | 5        | Touch Chip Select |
| TOUCH_IRQ| 27       | Touch Interrupt (optional) |

## Dependencies

- **TFT_eSPI**: Display driver library
- **LVGL**: (Optional) Advanced UI framework
- **ArduinoJson**: For configuration data

## Usage Example

```cpp
#include "display_manager.h"

DisplayManager display;

void setup() {
    // Initialize display
    if (!display.initialize()) {
        Serial.println("Display initialization failed!");
        return;
    }
    
    // Show home screen
    display.showScreen(DisplayManager::Screen::HOME);
}

void loop() {
    // Update display
    display.update();
    
    // Handle touch events
    auto touch = display.getTouchEvent();
    if (touch.pressed) {
        Serial.printf("Touch at: %d, %d\n", touch.x, touch.y);
    }
    
    // Update sensor data
    sensorData data;
    // ... read sensor data ...
    display.updateSensorData(data);
}
```

## Screen Types

1. **SPLASH**: Boot screen with logo and loading indicator
2. **HOME**: Main screen showing sensor readings and upload button
3. **MENU**: Settings and options menu
4. **WIFI_CONFIG**: WiFi network configuration interface
5. **SYSTEM_INFO**: Device status, battery, memory, uptime
6. **SENSOR_DETAIL**: Detailed sensor readings with history
7. **ERROR**: Error message display
8. **SLEEP**: Display off mode for power saving

## Configuration

Display settings can be configured in `handheld_config.h`:

```cpp
#define DISPLAY_BRIGHTNESS      80      // 0-100%
#define DISPLAY_ROTATION        1       // 0=0°, 1=90°, 2=180°, 3=270°
#define DISPLAY_TIMEOUT_MS      120000  // 2 minutes
```

## Touch Calibration

The display supports touch calibration on first boot. Calibration data is stored in NVS for future use.

## Power Management

- **Active Mode**: Display on, full functionality
- **Sleep Mode**: Display off, touch wake-up enabled
- **Brightness Control**: PWM backlight control (0-100%)

## API Reference

### Core Functions

- `bool initialize()`: Initialize display hardware
- `void update()`: Update display content (call in main loop)
- `void showScreen(Screen screen)`: Switch to different screen
- `void setDisplayOn(bool on)`: Turn display on/off
- `void setBrightness(uint8_t brightness)`: Set backlight brightness

### Data Updates

- `void updateSensorData(const sensorData& data)`: Update sensor readings
- `void updateSystemStatus(bool wifi, int battery, uint32_t uptime)`: Update system info
- `void showMessage(const String& message, bool isError)`: Show popup message

### Touch Handling

- `TouchEvent getTouchEvent()`: Get touch input
- `bool isTouchInButton(...)`: Check if touch is within button area

## Customization

The display can be customized by:

1. **Colors**: Modify color constants in header file
2. **Layout**: Adjust drawing functions for different layouts
3. **Fonts**: Use different TFT_eSPI fonts
4. **Graphics**: Add custom icons and graphics

## Troubleshooting

**Display not working:**
- Check SPI wiring connections
- Verify pin definitions in platformio.ini
- Check power supply (3.3V/5V requirements)

**Touch not responding:**
- Verify touch screen connections
- Run touch calibration
- Check TOUCH_CS pin configuration

**Poor display quality:**
- Check SPI clock speed
- Verify power supply stability
- Adjust display brightness

## Integration Notes

This component is designed to work with:
- **HandheldApp**: Main application controller
- **RS485 Soil Sensor**: For sensor data display
- **WiFi Connection Service**: For connection status
- **Firebase Uploader**: For upload status indication