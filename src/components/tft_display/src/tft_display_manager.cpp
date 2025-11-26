#include "tft_display_manager.h"
#include <stdio.h>

DisplayManager* DisplayManager::instance = nullptr;

DisplayManager::DisplayManager() {
    // Constructor - TFT object will be initialized in init()
}

DisplayManager* DisplayManager::getInstance() {
    if (instance == nullptr) {
        instance = new DisplayManager();
    }
    return instance;
}

void DisplayManager::init() {
    tft.init();
    tft.setRotation(1);  // Set rotation (0-3)
    tft.fillScreen(TFT_BLACK);
}

bool DisplayManager::initialize() {
    init();
    return true;
}

void DisplayManager::clear(uint16_t color) {
    tft.fillScreen(color);
}

void DisplayManager::drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg) {
    tft.setTextColor(color, bg);
    tft.setCursor(x, y);
    tft.print(text);
}

void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    tft.fillRect(x, y, w, h, color);
}

void DisplayManager::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    tft.drawRect(x, y, w, h, color);
}

void DisplayManager::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    tft.drawPixel(x, y, color);
}

void DisplayManager::drawHomeScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("KAGRI");
    
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print("Home Screen");
}

void DisplayManager::displaySensorData(float temperature, float moisture, float ec, float n, float p, float k) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.print("Sensor Data");
    
    char buffer[64];
    tft.setTextSize(1);
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buffer, sizeof(buffer), "Temp: %.1fC", temperature);
    tft.setCursor(10, 40);
    tft.print(buffer);
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    snprintf(buffer, sizeof(buffer), "Moisture: %.1f%%", moisture);
    tft.setCursor(10, 55);
    tft.print(buffer);
    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buffer, sizeof(buffer), "EC: %.2f", ec);
    tft.setCursor(10, 70);
    tft.print(buffer);
    
    if (n > 0 || p > 0 || k > 0) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        snprintf(buffer, sizeof(buffer), "N:%.1f P:%.1f K:%.1f", n, p, k);
        tft.setCursor(10, 85);
        tft.print(buffer);
    }
}

void DisplayManager::showScreen(DisplayScreen screen) {
    switch (screen) {
        case DisplayScreen::HOME:
            drawHomeScreen();
            break;
        case DisplayScreen::SOIL_DATA: {
            displaySensorData(25.0, 50.0, 1.5, 0, 0, 0);
            break;
        }
        case DisplayScreen::DEVICE_CONFIG:
            drawDeviceConfig();
            break;
    }
}

void DisplayManager::drawDeviceConfig() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.print("Device Config");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.print("WiFi Settings");
}

uint16_t DisplayManager::RGB565(uint8_t r, uint8_t g, uint8_t b) {
    // Convert RGB888 to RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}
