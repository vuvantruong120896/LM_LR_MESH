/**
 * @file tft_display_manager_lvgl.cpp
 * Display manager implementation using only LovyanGFX (no LVGL)
 */

#include "tft_display_manager_lvgl.h"
#include "lovyan_gfx_config.h"
#include "display_strings.h"
#include "display_colors.h"
#include "../../../utils/battery_monitor.h"
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
    currentScreen = CurrentScreen::HOME;
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // ==================== HEADER (0-50) ====================
    display.fillRect(0, 0, 240, 50, DisplayColor::DARK_BLUE);
    
    // Title "KAGRI" centered
    display.setTextSize(3);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(70, 12);
    display.print("KAGRI");
    
    // Header bottom line
    display.drawLine(0, 50, 240, 50, DisplayColor::CYAN);
    
    // ==================== STATUS BAR (52-75) ====================
    // WiFi status on left
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(10, 58);
    display.print("WiFi:");
    
    bool isWiFiConnected = WiFi.isConnected();
    display.setTextColor(isWiFiConnected ? DisplayColor::GREEN : DisplayColor::RED);
    display.setCursor(50, 58);
    display.print(isWiFiConnected ? "OK" : "X");
    
    // Battery status on right
    uint8_t batteryPercent = BatteryMonitor::getPercentage();
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(140, 58);
    display.print("Bat:");
    
    // Battery color based on level
    uint32_t battColor = (batteryPercent > 50) ? DisplayColor::GREEN : 
                         (batteryPercent > 20) ? DisplayColor::YELLOW : DisplayColor::RED;
    display.setTextColor(battColor);
    char battStr[8];
    snprintf(battStr, sizeof(battStr), "%d%%", batteryPercent);
    display.setCursor(175, 58);
    display.print(battStr);
    
    // Battery icon (small)
    display.drawRect(210, 56, 20, 10, battColor);
    display.fillRect(230, 58, 3, 6, battColor);
    int fillWidth = (batteryPercent * 18) / 100;
    display.fillRect(211, 57, fillWidth, 8, battColor);
    
    // Status bar bottom line
    display.drawLine(0, 72, 240, 72, DisplayColor::DARK_GRAY);
    
    // ==================== BOX 1: SENSOR READ (80-170) ====================
    // Outer border with rounded corners effect
    display.drawRect(10, 80, 220, 90, DisplayColor::CYAN);
    display.drawRect(11, 81, 218, 88, DisplayColor::CYAN);
    display.fillRect(12, 82, 216, 86, DisplayColor::DARK_BLUE);
    
    // Icon area (left side)
    display.fillRect(20, 95, 50, 55, DisplayColor::DARK_GREEN);
    display.drawRect(20, 95, 50, 55, DisplayColor::GREEN);
    
    // Sensor icon (simple representation)
    display.setTextSize(2);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(32, 100);
    display.print("S");
    display.setTextSize(1);
    display.setCursor(28, 125);
    display.print("READ");
    
    // Title and description (right side)
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(80, 95);
    display.print("SENSOR");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(80, 120);
    display.print("Read soil parameters");
    
    // Action hint
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(80, 140);
    display.print("> Click button");
    
    // ==================== BOX 2: WIFI CONFIG (180-270) ====================
    display.drawRect(10, 180, 220, 90, DisplayColor::MAGENTA);
    display.drawRect(11, 181, 218, 88, DisplayColor::MAGENTA);
    display.fillRect(12, 182, 216, 86, DisplayColor::DARK_BLUE);
    
    // Icon area (left side)
    display.fillRect(20, 195, 50, 55, 0x000080);  // Dark blue
    display.drawRect(20, 195, 50, 55, DisplayColor::CYAN);
    
    // WiFi icon (simple waves)
    display.setTextSize(2);
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(32, 200);
    display.print("W");
    display.setTextSize(1);
    display.setCursor(25, 225);
    display.print("WiFi");
    
    // Title and description (right side)
    display.setTextSize(2);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(80, 195);
    display.print("WiFi CFG");
    
    display.setTextSize(1);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(80, 220);
    display.print("Setup WiFi via BLE");
    
    // Action hint
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(80, 240);
    display.print("> Hold 5 seconds");
    
    // ==================== FOOTER (280-320) ====================
    display.drawLine(0, 280, 240, 280, DisplayColor::DARK_GRAY);
    
    // Device model
    display.setTextSize(1);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(10, 290);
    display.print(HANDHELD_DEVICE_MODEL);
    
    // Version on right
    display.setTextColor(DisplayColor::CYAN);
    char verStr[16];
    snprintf(verStr, sizeof(verStr), "%s", HANDHELD_FIRMWARE_VER);
    display.setCursor(190, 290);
    display.print(verStr);
}

/**
 * Update only WiFi status icon on HOME screen
 * This function only redraws the WiFi status area without affecting other parts
 * Call periodically (every 2-5 seconds) to keep WiFi status updated
 */
void DisplayManager::updateWiFiStatusIcon() {
    if (!initialized) {
        return;
    }
    
    // Only update if we're on HOME screen
    if (currentScreen != CurrentScreen::HOME) {
        return;
    }
    
    // Clear only the WiFi status area (position in new layout: x=50, y=58)
    display.fillRect(48, 55, 25, 15, DisplayColor::DARK_BLUE);
    
    // Check actual WiFi status and redraw
    bool isWiFiConnected = WiFi.isConnected();
    display.setTextSize(1);
    display.setTextColor(isWiFiConnected ? DisplayColor::GREEN : DisplayColor::RED);
    display.setCursor(50, 58);
    display.print(isWiFiConnected ? "OK" : "X");
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

    // Mở app KAGRI và kết nối
    display.setTextSize(1.5);  
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(10, 200);
    display.print("Open KAGRI app to connect");
    
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
    display.print("- pH, EC, NPK, Salt");
    
    // Rebooting message
    display.setTextSize(1);
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(30, 285);
    display.print("Rebooting...");
}

// ==================== Sensor Data Display Screen ====================

void DisplayManager::displaySensorData(float temperature, float moisture, float ph, float ec, 
                                      float n, float p, float k, float salt) {
    if (!initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display not initialized");
        return;
    }
    
    ESP_LOGI(TAG_DISPLAY, "Displaying 8-parameter sensor data");
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    
    // Header
    display.fillRect(0, 0, 240, 35, DisplayColor::DARK_BLUE);
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(70, 8);
    display.print("SENSOR DATA");
    
    // Display sensor readings in a grid layout
    int yStart = 45;
    int lineHeight = 50;
    int col1X = 10;
    int col2X = 130;
    
    // Row 1: Temperature and Moisture
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(col1X, yStart);
    display.print("Temp:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col1X, yStart + 18);
    display.printf("%.1f C", temperature);
    
    display.setTextColor(DisplayColor::CYAN);
    display.setCursor(col2X, yStart);
    display.print("Moisture:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col2X, yStart + 18);
    display.printf("%.1f%%", moisture);
    
    // Row 2: pH and EC
    yStart += lineHeight;
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(col1X, yStart);
    display.print("pH:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col1X, yStart + 18);
    display.printf("%.2f", ph);
    
    display.setTextColor(DisplayColor::MAGENTA);
    display.setCursor(col2X, yStart);
    display.print("EC:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col2X, yStart + 18);
    display.printf("%.1f uS", ec);
    
    // Row 3: N and P
    yStart += lineHeight;
    display.setTextColor(DisplayColor::ORANGE);
    display.setCursor(col1X, yStart);
    display.print("N:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col1X, yStart + 18);
    display.printf("%.0f mg", n);
    
    display.setTextColor(DisplayColor::RED);
    display.setCursor(col2X, yStart);
    display.print("P:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col2X, yStart + 18);
    display.printf("%.0f mg", p);
    
    // Row 4: K and Salt
    yStart += lineHeight;
    display.setTextColor(DisplayColor::LIGHT_BLUE);
    display.setCursor(col1X, yStart);
    display.print("K:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col1X, yStart + 18);
    display.printf("%.0f mg", k);
    
    display.setTextColor(DisplayColor::BROWN);  // Brown color for salt
    display.setCursor(col2X, yStart);
    display.print("Salt:");
    display.setTextColor(DisplayColor::WHITE);
    display.setCursor(col2X, yStart + 18);
    display.printf("%.0f mg", salt);
    
    // Footer with instructions
    yStart += lineHeight + 15;
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::LIGHT_GRAY);
    display.setCursor(10, yStart);
    display.print("Reading completed");
    
    // Status indicator
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::GREEN);
    display.setCursor(10, yStart + 20);
    display.print("Ready for BLE transfer");

    // Press button to send data
    display.setTextSize(1.5);
    display.setTextColor(DisplayColor::YELLOW);
    display.setCursor(10, yStart + 40);
    display.print("Press button to send data");
}
