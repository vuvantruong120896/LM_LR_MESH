#pragma once

#include <TFT_eSPI.h>
#include <stdint.h>

// Screen types
enum class DisplayScreen {
    HOME,          ///< Home screen
    SOIL_DATA,     ///< Soil sensor data
    DEVICE_CONFIG  ///< Device configuration
};

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
    void clear(uint16_t color = TFT_BLACK);
    void drawText(uint16_t x, uint16_t y, const char* text, uint16_t color = TFT_WHITE, uint16_t bg = TFT_BLACK);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    
    // UI Screen Methods
    void drawHomeScreen();
    void displaySensorData(float temperature, float moisture, float ec, float n = 0, float p = 0, float k = 0);
    void showScreen(DisplayScreen screen);
    void drawDeviceConfig();
    
    // Color utilities
    uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b);
    
    // Direct access to TFT object
    TFT_eSPI* getTFT() { return &tft; }
    
private:
    DisplayManager();
    static DisplayManager* instance;
    TFT_eSPI tft;
};

// For backward compatibility
typedef DisplayManager TFTDisplayManager;

// Display task placeholder
class DisplayTask {
public:
    static void init() {}
    static void stop() {}
};
