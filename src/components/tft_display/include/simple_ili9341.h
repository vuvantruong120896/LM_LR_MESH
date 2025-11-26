#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <Arduino.h>

// ILI9341 Color definitions
#define ILI9341_BLACK       0x0000
#define ILI9341_WHITE       0xFFFF
#define ILI9341_RED         0xF800
#define ILI9341_GREEN       0x07E0
#define ILI9341_BLUE        0x001F
#define ILI9341_CYAN        0x07FF
#define ILI9341_MAGENTA     0xF81F
#define ILI9341_YELLOW      0xFFE0
#define ILI9341_ORANGE      0xFD20

// SPI Pins for ESP32 Handheld
#define TFT_MOSI    23
#define TFT_MISO    19
#define TFT_SCLK    18
#define TFT_CS      5
#define TFT_DC      4
#define TFT_RST     2

// Display dimensions
#define TFT_WIDTH   320
#define TFT_HEIGHT  240

class SimpleILI9341 {
public:
    SimpleILI9341();
    
    // Initialization
    void init();
    
    // Basic drawing
    void fillScreen(uint16_t color);
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    
    // Text drawing
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg);
    void drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg);
    
    // Backlight control
    void setBacklight(bool on);
    
private:
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void writeWord(uint16_t word);
    void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void reset();
    
    // SPI operations
    void spiWrite(uint8_t data);
    void spiWrite16(uint16_t data);
};
