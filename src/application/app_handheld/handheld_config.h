#ifndef HANDHELD_CONFIG_H
#define HANDHELD_CONFIG_H

// Device identification
#define HANDHELD_DEVICE_TYPE    DeviceType::HANDHELD
#define HANDHELD_DEVICE_NAME    "KAgri Handheld"
#define HANDHELD_FIRMWARE_VER   "1.0.0"

// GPIO Pin Configuration for ESP32-S3
// LCD TFT 2.4" ILI9341 SPI pins (actual hardware wiring - VERIFIED WORKING)
#define TFT_CS          5       // Chip select (CS pin)
#define TFT_DC          46      // Data/Command (DC pin) - Connected to GPIO46 ✅ WORKING
#define TFT_RST         4       // Reset (RST pin)
#define TFT_MOSI        19      // SPI MOSI (DIN pin)
#define TFT_SCLK        18      // SPI Clock (CLK pin)
#define TFT_MISO        16      // SPI MISO (optional - SDO pin)
#define TFT_BL          17      // Backlight control pin

// Touch screen - NOT CONNECTED
// Touch controller is not connected to this device
// Screen uses button-based navigation instead

// Button pins
// Single button IO3 (pull-up): short press = select, long press 3s = menu
#define BUTTON_SELECT   3       // IO3 - Unified button (pull-up, active LOW)

// RS485 Soil Sensor pins (from rs485_config.h - shared with Node & Gateway)
#define RS485_TX_PIN    21      // UART2 TX - Transmit to RS485 (D pin)
#define RS485_RX_PIN    20      // UART2 RX - Receive from RS485 (R pin)
#define RS485_DE_PIN    42      // Driver Enable / Receiver Enable (DE pin)

// Status LED - Display backlight control
#define STATUS_LED_PIN  17      // LED backlight PWM control (IO17)
#define BOARD_LED       STATUS_LED_PIN  // For button_led component compatibility
#define LED_ON          HIGH
#define LED_OFF         LOW

// Buzzer control
#define BUZZER_PIN      14      // IO14 - Buzzer pin (1=on, 0=off)

// WiFi Configuration (store in NVS for production)
#ifndef WIFI_SSID
#define WIFI_SSID               "OXII"      // Default WiFi SSID
#endif
#ifndef WIFI_PASSWORD  
#define WIFI_PASSWORD           "sharitek-nerd-2019"  // Default WiFi password
#endif

// Firebase Configuration
#ifndef FIREBASE_HOST
#define FIREBASE_HOST           "https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/:null"
#endif
#ifndef FIREBASE_AUTH
#define FIREBASE_AUTH           "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"  // Set in NVS
#endif

// Operational Settings
#define SENSOR_READ_INTERVAL_MS     (5 * 60 * 1000)    // Read sensor every 5 minutes (handheld = more frequent)
#define DISPLAY_TIMEOUT_MS          (2 * 60 * 1000)    // Turn off display after 2 minutes
#define AUTO_UPLOAD_INTERVAL_MS     (30 * 60 * 1000)   // Auto upload every 30 minutes
#define BATTERY_CHECK_INTERVAL_MS   (10 * 1000)        // Check battery every 10 seconds

// Display Settings
#define DISPLAY_BRIGHTNESS          80      // 0-100% (PWM on IO17)
#define DISPLAY_ROTATION            1       // 0=0°, 1=90°, 2=180°, 3=270°
#define TOUCH_ENABLED               false   // Touch screen not connected
#define TOUCH_CALIBRATION           false   // No touch calibration (not needed)

// Data Storage
#define MAX_OFFLINE_READINGS        50      // Maximum readings stored when offline
#define NVS_NAMESPACE              "handheld"
#define NVS_WIFI_SSID_KEY          "wifi_ssid"
#define NVS_WIFI_PASS_KEY          "wifi_pass"
#define NVS_FIREBASE_AUTH_KEY      "firebase_auth"
#define NVS_DEVICE_ID_KEY          "device_id"

// System Configuration
#define HEAP_WARNING_THRESHOLD      10000   // Warn if free heap below 10KB
#define WATCHDOG_TIMEOUT_SEC        30      // Reset if no activity for 30s

// Debug Configuration
#ifdef DEBUG
#define HANDHELD_LOG_LEVEL          ESP_LOG_DEBUG
#else
#define HANDHELD_LOG_LEVEL          ESP_LOG_INFO
#endif

#endif // HANDHELD_CONFIG_H