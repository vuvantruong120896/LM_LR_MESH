#pragma once

#include "simple_ili9341.h"
#include "display_task.h"
#include <stdint.h>
#include <string>

// Sensor data structure
struct SensorData {
    double moisture;
    double temperature;
    double ec;
};

class DisplayManager {
public:
    static DisplayManager* getInstance();
    
    void init();
    bool initialize();
    void clear(uint16_t color = 0x0000);
    void drawText(uint16_t x, uint16_t y, const char* text, uint16_t color = 0xFFFF, uint16_t bg = 0x0000);
    void drawText(uint16_t x, uint16_t y, const std::string& text, uint16_t color = 0xFFFF, uint16_t bg = 0x0000);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    
    // UI Screen Methods
    void drawHomeScreen();
    void displaySensorData(float temperature, float moisture, float ec, float n = 0, float p = 0, float k = 0);
    void showScreen(DisplayScreen screen);
    void drawDeviceConfig();
    
    // Color utilities
    uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b);
    
private:
    DisplayManager();
    static DisplayManager* instance;
    SimpleILI9341 display;
};
