#include "display_manager.h"
#include <stdio.h>

DisplayManager* DisplayManager::instance = nullptr;

DisplayManager::DisplayManager() {
    // Constructor
}

DisplayManager* DisplayManager::getInstance() {
    if (instance == nullptr) {
        instance = new DisplayManager();
    }
    return instance;
}

void DisplayManager::init() {
    display.init();
}

bool DisplayManager::initialize() {
    display.init();
    return true;
}

void DisplayManager::clear(uint16_t color) {
    display.fillScreen(color);
}

void DisplayManager::drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg) {
    display.drawText(x, y, text, color, bg);
}

void DisplayManager::drawText(uint16_t x, uint16_t y, const std::string& text, uint16_t color, uint16_t bg) {
    display.drawText(x, y, text.c_str(), color, bg);
}

void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    display.fillRect(x, y, w, h, color);
}

void DisplayManager::drawHomeScreen() {
    display.fillScreen(ILI9341_BLACK);
    display.drawText(10, 10, "KAGRI", ILI9341_WHITE, ILI9341_BLACK);
    display.drawText(10, 30, "Home Screen", ILI9341_CYAN, ILI9341_BLACK);
}

void DisplayManager::displaySensorData(float temperature, float moisture, float ec, float n, float p, float k) {
    display.fillScreen(ILI9341_BLACK);
    display.drawText(10, 10, "Sensor Data", ILI9341_WHITE, ILI9341_BLACK);
    
    char buffer[64];
    
    snprintf(buffer, sizeof(buffer), "Temp: %.1fC", temperature);
    display.drawText(10, 30, buffer, ILI9341_YELLOW, ILI9341_BLACK);
    
    snprintf(buffer, sizeof(buffer), "Moisture: %.1f%%", moisture);
    display.drawText(10, 45, buffer, ILI9341_GREEN, ILI9341_BLACK);
    
    snprintf(buffer, sizeof(buffer), "EC: %.2f", ec);
    display.drawText(10, 60, buffer, ILI9341_CYAN, ILI9341_BLACK);
    
    if (n > 0 || p > 0 || k > 0) {
        snprintf(buffer, sizeof(buffer), "N:%.1f P:%.1f K:%.1f", n, p, k);
        display.drawText(10, 75, buffer, ILI9341_ORANGE, ILI9341_BLACK);
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
    display.fillScreen(ILI9341_BLACK);
    display.drawText(10, 10, "Device Config", ILI9341_WHITE, ILI9341_BLACK);
    display.drawText(10, 30, "WiFi Settings", ILI9341_YELLOW, ILI9341_BLACK);
}

uint16_t DisplayManager::RGB565(uint8_t r, uint8_t g, uint8_t b) {
    // Convert RGB888 to RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

