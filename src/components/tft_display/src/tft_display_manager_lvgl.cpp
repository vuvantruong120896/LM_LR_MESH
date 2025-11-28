/**
 * @file tft_display_manager_lvgl.cpp
 * Display manager implementation using only LovyanGFX (no LVGL)
 */

#include "tft_display_manager_lvgl.h"
#include "lovyan_gfx_config.h"
#include "display_strings.h"
#include "display_colors.h"
#include <freertos/FreeRTOS.h>
#include <WiFi.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <driver/gpio.h>

// External LovyanGFX display instance
extern LGFX display;

DisplayManager* DisplayManager::instance = nullptr;

DisplayManager::DisplayManager() : initialized(false), scr(nullptr), 
                                    title_label(nullptr), status_label(nullptr), 
                                    sensor_label(nullptr) {
    ESP_LOGI(TAG_DISPLAY, "DisplayManager constructor");
}

DisplayManager::~DisplayManager() {
    ESP_LOGI(TAG_DISPLAY, "DisplayManager destructor");
}

DisplayManager* DisplayManager::getInstance() {
    if (instance == nullptr) {
        instance = new DisplayManager();
    }
    return instance;
}

bool DisplayManager::initialize() {
    if (initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display already initialized");
        return true;
    }
    
    ESP_LOGI(TAG_DISPLAY, "Initializing display with LovyanGFX only...");
    
    // Initialize display
    display.init();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Set to portrait mode
    display.setRotation(0);
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Turn on backlight
    tft_backlight_on();
    
    initialized = true;
    ESP_LOGI(TAG_DISPLAY, "Display initialized successfully with LovyanGFX");
    return true;
}

void DisplayManager::drawInitialScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    ESP_LOGI(TAG_DISPLAY, "Drawing initial screen");
    
    
    // Draw title "KAGRI" in big cyan
    display.setTextSize(4);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(60, 90);
    display.print("KAGRI");
    
    // Decorative line
    display.drawLine(20, 150, 220, 150, DisplayColor::CYAN);
    
    // Draw model text below
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(40, 165);
    display.print(HANDHELD_DEVICE_MODEL);
    
    // Draw version text at bottom
    display.setTextSize(1);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    char version_str[32];
    snprintf(version_str, sizeof(version_str), "Version %s", HANDHELD_FIRMWARE_VER);
    display.setCursor(70, 210);
    display.print(version_str);
    
    // Loading progress
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(90, 240);
    display.print("Init...");
    
    // Loading bar
    display.fillRect(30, 260, 180, 10, DisplayColor::DARK_GRAY);
    display.fillRect(30, 260, 180, 10, DisplayColor::GREEN);
    
    vTaskDelay(pdMS_TO_TICKS(3000));  // Display for 3 seconds
}

void DisplayManager::drawHomeScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    ESP_LOGI(TAG_DISPLAY, "Drawing home screen");
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Header background
    display.fillRect(0, 0, 240, 55, DisplayColor::DARK_BLUE);
    
    // Draw title in cyan - left aligned
    display.setTextSize(3);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(15, 15);
    display.print("HOME");
    
    // Status icons in top right - Battery and WiFi
    // WiFi icon box (top right)
    display.drawRect(180, 8, 50, 20, DisplayColor::CYAN);
    display.fillRect(181, 9, 48, 18, DisplayColor::DARK_BLUE);
    
    // Draw WiFi icon and status
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(185, 15);
    display.print("WiFi");
    
    // Check actual WiFi status
    bool isWiFiConnected = WiFi.isConnected();
    display.setTextSize(1);
    display.setTextColor(isWiFiConnected ? DisplayColor::GREEN : DisplayColor::RED);
    display.setCursor(215, 15);
    display.print(isWiFiConnected ? "OK" : "X");
    
    // Battery icon box (below WiFi)
    display.drawRect(180, 32, 50, 20, DisplayColor::YELLOW);
    display.fillRect(181, 33, 48, 18, DisplayColor::DARK_GREEN);
    
    // Draw battery icon (rectangle with bars)
    uint32_t batteryColor = DisplayColor::YELLOW;
    // Battery body
    display.drawRect(186, 40, 10, 6, batteryColor);
    // Battery terminal
    display.drawRect(197, 41, 1, 4, batteryColor);
    // Battery charge bars (filled)
    display.fillRect(187, 41, 2, 4, batteryColor);
    display.fillRect(190, 41, 2, 4, batteryColor);
    display.fillRect(193, 41, 2, 4, batteryColor);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(210, 39);
    display.print("85%");
    
    // Decorative line below header
    display.drawLine(0, 56, 240, 56, DisplayColor::CYAN);
    
    // Box 1: Sensor reading - larger with better layout
    display.drawRect(8, 70, 224, 80, DisplayColor::CYAN);
    display.fillRect(9, 71, 222, 78, DisplayColor::DARK_BLUE);
    
    // Sensor label and info
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(15, 80);
    display.print("SENSOR READ");
    
    display.setTextSize(1.4);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(15, 120);
    display.print("Click button:Start reading");
    
    // Box 2: WiFi config - larger with better layout
    display.drawRect(8, 160, 224, 80, DisplayColor::MAGENTA);
    display.fillRect(9, 161, 222, 78, DisplayColor::DARK_BLUE);
    
    // WiFi label and info
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(15, 170);
    display.print("WiFi CONFIG");
    
    display.setTextSize(1.4);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(15, 210);
    display.print("Hold 5 seconds:Setup WiFi");
    
    // Footer - system status
    display.drawLine(0, 270, 240, 270, DisplayColor::LIGHT_GRAY);
    display.setTextSize(1.3);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(80, 280);
    display.print("System Ready");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(100, 300);
    display.print("v1.0");
}

void DisplayManager::displaySensorData(float temperature, float moisture, float ph, float ec, 
                                       float n, float p, float k) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 40, DisplayColor::DARK_GREEN);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(50, 10);
    display.print("SENSOR DATA");
    
    // Separator line
    display.drawLine(0, 42, 240, 42, DisplayColor::GREEN);
    
    // === LEFT COLUMN ===
    // Temperature (Orange)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(10, 60);
    display.print("Temp:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(50, 57);
    display.print(temperature, 1);
    display.print("C");
    
    // Moisture (Cyan)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(10, 85);
    display.print("RH:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::LIGHT_GREEN);
    display.setCursor(50, 82);
    display.print(moisture, 1);
    display.print("%");
    
    // pH (Purple/Magenta)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::MAGENTA);
    display.setCursor(10, 110);
    display.print("pH:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(50, 107);
    display.print(ph, 1);
    
    // EC (Yellow)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(10, 135);
    display.print("EC:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::LIGHT_BLUE);
    display.setCursor(50, 132);
    display.print(ec, 1);
    
    // === RIGHT COLUMN (NPK) ===
    // N (Green)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(130, 60);
    display.print("N:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::LIGHT_GREEN);
    display.setCursor(145, 57);
    display.print(n, 1);
    display.print("mg");
    
    // P (Orange)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(130, 85);
    display.print("P:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(145, 82);
    display.print(p, 1);
    display.print("mg");
    
    // K (Cyan)
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(130, 110);
    display.print("K:");
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::LIGHT_GREEN);
    display.setCursor(145, 107);
    display.print(k, 1);
    display.print("mg");
    
    // Info box
    display.drawRect(10, 170, 220, 90, DisplayColor::CYAN);
    display.fillRect(11, 171, 218, 88, DisplayColor::DARK_BLUE);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(15, 180);
    display.print("Optimal Ranges:");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(15, 200);
    display.print("RH: 40-60%");
    display.setCursor(15, 215);
    display.print("pH: 6-7");
    display.setCursor(15, 230);
    display.print("NPK: for growth");
    
    // Footer
    display.setTextSize(1);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(50, 280);
    display.print("Press button to return");
    display.drawLine(0, 270, 240, 270, DisplayColor::LIGHT_GRAY);
}

void DisplayManager::clearScreen() {
    if (!initialized) return;
    display.fillScreen(TFT_BLACK);
}

void DisplayManager::enableBacklight() {
    tft_backlight_on();
}

void DisplayManager::disableBacklight() {
    tft_backlight_off();
}

void DisplayManager::drawText(int x, int y, const char* text, uint32_t color) {
    if (!initialized) return;
    
    display.setTextSize(1);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(text);
}

void DisplayManager::drawMeasuringScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Header background
    display.fillRect(0, 0, 240, 60, DisplayColor::DARK_BLUE);
    
    // Title
    display.setTextSize(3);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(40, 15);
    display.print("MEASURING");
    
    // Status box
    display.drawRect(20, 80, 200, 100, DisplayColor::CYAN);
    display.fillRect(21, 81, 198, 98, DisplayColor::DARK_BLUE);
    
    // Waiting text
    display.setTextSize(2);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(50, 100);
    display.print("Sensor Ready");
    
    // Animated dots
    display.setTextSize(3);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(80, 130);
    display.print("...");
    
    // Status text
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(30, 210);
    display.print("Reading sensors...");
    
    // Progress bar
    display.fillRect(30, 240, 180, 8, DisplayColor::DARK_GRAY);
    display.fillRect(30, 240, 90, 8, DisplayColor::GREEN);
    
    // Timeout info
    display.setTextSize(1);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(50, 270);
    display.print("Timeout: 30s");
}

void DisplayManager::drawConfigScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_GREEN);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::MAGENTA);
    display.setCursor(40, 12);
    display.print("WiFi CONFIG");
    
    // Step 1: SSID
    display.drawRect(15, 70, 210, 50, DisplayColor::CYAN);
    display.fillRect(16, 71, 208, 48, DisplayColor::DARK_BLUE);
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 75);
    display.print("Step 1: Connect to WiFi");
    display.setTextSize(2);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(25, 92);
    display.print("KAGRI-Setup");
    
    // Step 2: Portal
    display.drawRect(15, 135, 210, 50, DisplayColor::ORANGE);
    display.fillRect(16, 136, 208, 48, DisplayColor::DARK_BLUE);
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 140);
    display.print("Step 2: Open Portal");
    display.setTextSize(2);
    display.setTextColor(DisplayColor::LIGHT_BLUE);
    display.setCursor(30, 158);
    display.print("192.168.1.1");
    
    // Step 3: Configure
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(15, 210);
    display.print("Step 3: Enter WiFi SSID & Password");
    
    // Auto-return timer
    display.setTextSize(1);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(30, 280);
    display.print("Auto-return in: 60s");
}

void DisplayManager::drawErrorScreen(const char* errorMsg) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Error header (red background)
    display.fillRect(0, 0, 240, 60, DisplayColor::DARK_RED);
    
    // Error title
    display.setTextSize(3);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(60, 15);
    display.print("ERROR");
    
    // Error box
    display.drawRect(10, 80, 220, 100, DisplayColor::RED);
    display.fillRect(11, 81, 218, 98, DisplayColor::DARK_BLUE);
    
    // Error message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(15, 95);
    display.println(errorMsg != nullptr ? errorMsg : "Unknown error");
    
    // Recovery info box
    display.drawRect(10, 200, 220, 50, DisplayColor::CYAN);
    display.fillRect(11, 201, 218, 48, DisplayColor::DARK_BLUE);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(15, 210);
    display.print("Auto-recovering...");
    
    // Recovery progress bar
    display.fillRect(20, 230, 200, 6, DisplayColor::DARK_GRAY);
    display.fillRect(20, 230, 100, 6, DisplayColor::GREEN);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(90, 245);
    display.print("30s");
}

void DisplayManager::drawSleepScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen with dimmer effect
    display.fillScreen(TFT_BLACK);
    display.fillRect(0, 0, 240, 320, DisplayColor::DARK_GRAY);
    
    // Sleep box
    display.drawRect(30, 80, 180, 160, DisplayColor::CYAN);
    display.fillRect(31, 81, 178, 158, DisplayColor::DARK_BLUE);
    
    // Sleep icons (Z shapes)
    display.setTextSize(4);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(80, 100);
    display.print("Z");
    display.setCursor(85, 140);
    display.print("z");
    display.setCursor(75, 180);
    display.print("z");
    
    // Sleep text
    display.setTextSize(2);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(40, 230);
    display.print("SLEEP MODE");
    
    // Battery info
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(30, 260);
    display.print("Low Power State Active");
    
    // Wake info
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(20, 280);
    display.print("Press button to wake up");
}

void DisplayManager::drawBleSendingScreen(int progress) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Title
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 30);
    display.print("SENDING TO BLE");
    
    // BLE icon
    display.setTextSize(3);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(100, 100);
    display.print("~");
    
    // Progress bar
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(10, 170);
    display.print("Progress:");
    
    // Progress percentage
    display.setTextSize(2);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(100, 200);
    display.print(progress);
    display.print("%");
    
    // Status
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(10, 250);
    if (progress < 100) {
        display.print("Transferring...");
    } else {
        display.print("Complete!");
    }
}

// ==================== WiFi BLE Configuration Screens ====================

void DisplayManager::drawWiFiConfigStartScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 50, DisplayColor::MAGENTA);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(20, 12);
    display.print("WiFi Config BLE");
    
    // BLE Icon (simple waves)
    display.setTextSize(3);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(80, 80);
    display.print("~");
    display.setCursor(75, 100);
    display.print("~ ~");
    
    // Device Name label
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 140);
    display.print("Scan for device:");
    
    // Display the actual BLE device name (KAGRI-HHC-XXYY)
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(20, 130);
    display.print("KAGRI-HHC");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(20, 175);
    display.print("(MAC ending)");
    
    // Instructions
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(20, 210);
    display.print("1. Open KAGRI App");
    
    display.setCursor(20, 225);
    display.print("2. Enable Bluetooth");
    
    display.setCursor(20, 240);
    display.print("3. Scan & select device");
    
    // Status
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(20, 285);
    display.print("Waiting for connection...");

}

void DisplayManager::drawBLEWaitingScreen(int timeoutSeconds) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Mark that we're on BLE waiting screen
    currentScreen = CurrentScreen::BLE_WAITING;
    
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 50, DisplayColor::MAGENTA);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(40, 12);
    display.print("SEARCHING...");

    // Text
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(10, 120);
    display.print("Searching for device...");
    
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(10, 160);
    display.print("Enable Bluetooth on phone");
    
    // Timeout countdown
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(60, 230);
    display.print("Time left: ");
    display.print(timeoutSeconds);
    display.print("s");
    
    // Progress dots
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(75, 260);
    display.print("[*  *  *]");
}

void DisplayManager::updateBLEWaitingScreenCountdown(int timeoutSeconds) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Only update countdown if we're still on BLE waiting screen
    // Skip update if we've switched to sensor data sent screen
    if (currentScreen != CurrentScreen::BLE_WAITING) {
        return;
    }
    
    // Only clear and redraw the countdown area (avoid full screen flicker)
    // Clear the countdown text area - make it wider to handle all digit transitions
    display.fillRect(55, 220, 150, 35, TFT_BLACK);
    
    // Redraw countdown text only
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(55, 230);
    display.print("Time left: ");
    display.print(timeoutSeconds);
    display.print("s");
}

void DisplayManager::drawBLEConnectedScreen(int progress) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header (Green for connected)
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_GREEN);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(20, 12);
    display.print("BLE CONNECTED");
    
    // Checkmark
    display.setTextSize(4);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(100, 70);
    display.print("~");
    
    // Status text
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(40, 130);
    display.print("Connected to phone!");
    
    // Receiving data
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(30, 170);
    display.print("Receiving WiFi data...");
    
    // Progress bar
    display.drawRect(20, 200, 200, 20, DisplayColor::YELLOW);
    display.fillRect(21, 201, (progress * 198) / 100, 18, DisplayColor::YELLOW);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(100, 245);
    display.print(progress);
    display.print("%");
}

void DisplayManager::drawCredentialsReceivedScreen(const char* ssid, int signalStrength) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 50, DisplayColor::MAGENTA);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(30, 12);
    display.print("CREDENTIALS");
    
    // WiFi Network label
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 80);
    display.print("WiFi Network:");
    
    display.setTextSize(2);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(20, 100);
    if (ssid) {
        display.print(ssid);
    } else {
        display.print("Unknown");
    }
    
    // Signal Strength label
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 150);
    display.print("Signal:");
    
    // Signal bars
    display.drawRect(20, 170, 200, 15, DisplayColor::CYAN);
    display.fillRect(21, 171, (signalStrength * 198) / 100, 13, DisplayColor::CYAN);
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(100, 210);
    display.print(signalStrength);
    display.print("%");
    
    // Connecting status
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(50, 260);
    display.print("Connecting to WiFi...");
}

void DisplayManager::drawWiFiConnectingScreen(int progress) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 50, DisplayColor::MAGENTA);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(40, 12);
    display.print("CONNECTING");
    
    // Status text
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(30, 100);
    display.print("Connecting to WiFi network...");
    
    // Progress bar
    display.drawRect(20, 140, 200, 25, DisplayColor::CYAN);
    display.fillRect(21, 141, (progress * 198) / 100, 23, DisplayColor::CYAN);
    
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(90, 200);
    display.print(progress);
    display.print("%");
    
    // Animated dots
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(80, 260);
    display.print("[*  *  *]");
}

void DisplayManager::drawWiFiConnectSuccessScreen(const char* ipAddress) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header (Green for success)
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_GREEN);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(30, 12);
    display.print("SUCCESS!");
    
    // Checkmark
    display.setTextSize(4);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(100, 70);
    display.print("~");
    
    // Success message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(40, 130);
    display.print("WiFi Connected!");
    
    // IP Address label
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 170);
    display.print("IP Address:");
    
    display.setTextSize(2);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(30, 190);
    if (ipAddress) {
        display.print(ipAddress);
    } else {
        display.print("Assigned");
    }
    
    // // Return message
    // display.setTextSize(1);
    // display.setTextColor(DisplayColor::ORANGE);
    // display.setCursor(40, 260);
    // display.print("Returning to HOME...");
}

void DisplayManager::drawWiFiConnectErrorScreen(const char* errorMsg) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header (Red for error)
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_RED);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(40, 12);
    display.print("FAILED");
    
    // Error title
    display.setTextSize(1);
    display.setTextColor(DisplayColor::RED);
    display.setCursor(30, 70);
    display.print("WiFi Connection Failed");
    
    // Error message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(20, 110);
    if (errorMsg) {
        display.print(errorMsg);
    } else {
        display.print("Unknown error");
    }
    
    // Possible causes
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(20, 160);
    display.print("Possible causes:");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(30, 180);
    display.print("- Wrong Password");
    display.setCursor(30, 200);
    display.print("- Network Unavailable");
    display.setCursor(30, 220);
    display.print("- Signal Too Weak");
    
    // Retry message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(50, 270);
    display.print("Hold button to retry");
}

void DisplayManager::drawBLEErrorScreen(int timeoutSeconds) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    display.fillScreen(TFT_BLACK);
    
    // Header (Red for error)
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_RED);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(50, 12);
    display.print("BLE ERROR");
    
    // Error message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(30, 80);
    display.print("BLE Connection Timeout");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::RED);
    display.setCursor(30, 110);
    display.print("Device was not found");
    
    // Instructions
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 160);
    display.print("Please try again:");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(30, 180);
    display.print("1. Enable Bluetooth");
    display.setCursor(30, 200);
    display.print("2. Open KAGRI App");
    display.setCursor(30, 220);
    display.print("3. Hold button 5s");
    
    // Timeout countdown
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(50, 270);
    display.print("Return in: ");
    display.print(timeoutSeconds);
    display.print("s");
}

void DisplayManager::drawSensorDataSentScreen() {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    // Mark that we're on sensor data sent screen
    currentScreen = CurrentScreen::SENSOR_DATA_SENT;
    
    display.fillScreen(TFT_BLACK);
    
    // Header (Green for success)
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_GREEN);
    display.setTextSize(2);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(30, 12);
    display.print("SUCCESS!");
    
    // Success message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(20, 80);
    display.print("Sensor Data Sent");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(30, 110);
    display.print("to Mobile App");
    
    // Success icon area
    display.setTextSize(1);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(100, 150);
    display.print("[OK]");
    
    // Data transmitted message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(20, 200);
    display.print("Data transmitted:");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(30, 220);
    display.print("- Temperature");
    display.setCursor(30, 235);
    display.print("- Moisture");
    display.setCursor(30, 250);
    display.print("- pH, EC, NPK");
    
    // Rebooting message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(30, 285);
    display.print("Rebooting...");
}
