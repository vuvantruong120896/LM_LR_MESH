/**
 * @file tft_display_manager_lvgl.h
 * Display manager using only LovyanGFX (no LVGL)
 */

#ifndef TFT_DISPLAY_MANAGER_LVGL_H
#define TFT_DISPLAY_MANAGER_LVGL_H

#include <stdint.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include "display_colors.h"
#include "display_strings.h"

#define TAG_DISPLAY "DisplayMgr"

// Backlight control functions
void tft_backlight_on(void);
void tft_backlight_off(void);

class DisplayManager {
public:
    static DisplayManager* getInstance();
    
    /**
     * Initialize display with LovyanGFX
     */
    bool initialize();

    /**
     * Draw màn hình khởi động
     */
    void drawInitialScreen();
    
    /**
     * Draw home screen with text
     */
    void drawHomeScreen();
    
    /**
     * Update WiFi status icon only (without redrawing entire screen)
     * Call this periodically to update WiFi status on HOME screen
     */
    void updateWiFiStatusIcon();
    
    /**
     * Display sensor data (temperature, moisture, pH, EC, NPK, Salt)
     */
    void displaySensorData(float temperature, float moisture, float ph, float ec, 
                          float n = 0, float p = 0, float k = 0, float salt = 0);
    
    /**
     * Draw "Đang đo..." screen (measuring)
     */
    void drawMeasuringScreen();
    
    /**
     * Draw WiFi config screen
     */
    void drawConfigScreen();
    
    /**
     * Draw error screen
     */
    void drawErrorScreen(const char* errorMsg);
    
    /**
     * Draw sleep screen
     */
    void drawSleepScreen();
    
    /**
     * Draw BLE sending data screen
     */
    void drawBleSendingScreen(int progress = 0);
    
    /**
     * WiFi BLE Configuration Screens
     */
    void drawWiFiConfigStartScreen();
    void drawBLEWaitingScreen(int timeoutSeconds = 60);
    void updateBLEWaitingScreenCountdown(int timeoutSeconds);
    void drawBLEConnectedScreen(int progress = 0);
    void drawCredentialsReceivedScreen(const char* ssid, int signalStrength);
    void drawWiFiConnectingScreen(int progress = 0);
    void drawWiFiConnectSuccessScreen(const char* ipAddress);
    void drawWiFiConnectErrorScreen(const char* errorMsg);
    void drawBLEErrorScreen(int timeoutSeconds = 10);
    
    /**
     * Sensor Data Transfer Success Screen
     */
    void drawSensorDataSentScreen();
    
    /**
     * Clear screen
     */
    void clearScreen();
    
    /**
     * Backlight control
     */
    void enableBacklight();
    void disableBacklight();
    
    /**
     * Draw text on screen
     */
    void drawText(int x, int y, const char* text, uint32_t color = 0xFFFFFF);
    
    ~DisplayManager();
    
private:
    DisplayManager();
    static DisplayManager* instance;
    bool initialized;
    
    // Track current screen type to prevent countdown updates on success screen
    enum class CurrentScreen {
        HOME,
        MEASURING,
        BLE_WAITING,
        SENSOR_DATA_SENT,
        OTHER
    };
    CurrentScreen currentScreen = CurrentScreen::HOME;
    
    // Placeholder members (not used in LovyanGFX-only version)
    void* scr;
    void* title_label;
    void* status_label;
    void* sensor_label;
};

#endif
