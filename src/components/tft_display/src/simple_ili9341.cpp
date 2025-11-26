#include "simple_ili9341.h"
#include "font5x7.h"
#include <SPI.h>

// ILI9341 Commands
#define ILI9341_SWRESET    0x01
#define ILI9341_RDDID      0x04
#define ILI9341_RDDST      0x09
#define ILI9341_SLPIN      0x10
#define ILI9341_SLPOUT     0x11
#define ILI9341_PTLON      0x12
#define ILI9341_NORON      0x13
#define ILI9341_INVOFF     0x20
#define ILI9341_INVON      0x21
#define ILI9341_GAMMASET   0x26
#define ILI9341_DISPOFF    0x28
#define ILI9341_DISPON     0x29
#define ILI9341_CASET      0x2A
#define ILI9341_PASET      0x2B
#define ILI9341_RAMWR      0x2C
#define ILI9341_RAMRD      0x2E
#define ILI9341_PTLAR      0x30
#define ILI9341_COLMOD     0x3A
#define ILI9341_MADCTL     0x36
#define ILI9341_FRMCTR1    0xB1
#define ILI9341_FRMCTR2    0xB2
#define ILI9341_FRMCTR3    0xB3
#define ILI9341_INVCTR     0xB4
#define ILI9341_DFUNCTR    0xB6
#define ILI9341_PWCTR1     0xC0
#define ILI9341_PWCTR2     0xC1
#define ILI9341_PWCTR3     0xC2
#define ILI9341_VMCTR1     0xC5
#define ILI9341_VMCTR2     0xC7
#define ILI9341_PWCTRA     0xCB
#define ILI9341_PWCTRB     0xCF
#define ILI9341_RDID1      0xDA
#define ILI9341_RDID2      0xDB
#define ILI9341_RDID3      0xDC
#define ILI9341_GMCTRP1    0xE0
#define ILI9341_GMCTRN1    0xE1
#define ILI9341_DTCA       0xE8
#define ILI9341_DTCB       0xEA
#define ILI9341_POWONABC   0xEE

#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08

SimpleILI9341::SimpleILI9341() {
    // Constructor
}

void SimpleILI9341::init() {
    // Initialize SPI
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    SPI.setFrequency(40000000); // 40 MHz
    SPI.setDataMode(SPI_MODE0);
    
    // Initialize pins
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    
    // Reset display
    reset();
    
    // Initialize display
    writeCommand(ILI9341_SWRESET);
    delay(150);
    
    writeCommand(ILI9341_PWCTRB);
    writeData(0x00);
    writeData(0xC1);
    writeData(0x30);
    
    writeCommand(ILI9341_POWONABC);
    writeData(0x64);
    writeData(0x03);
    writeData(0x12);
    writeData(0x81);
    
    writeCommand(ILI9341_DTCA);
    writeData(0x85);
    writeData(0x00);
    writeData(0x78);
    
    writeCommand(ILI9341_DTCB);
    writeData(0x00);
    writeData(0x00);
    
    writeCommand(ILI9341_PWCTR1);
    writeData(0x23);
    
    writeCommand(ILI9341_PWCTR2);
    writeData(0x10);
    
    writeCommand(ILI9341_VMCTR1);
    writeData(0x3e);
    writeData(0x28);
    
    writeCommand(ILI9341_VMCTR2);
    writeData(0x86);
    
    writeCommand(ILI9341_MADCTL);
    writeData(MADCTL_MX | MADCTL_BGR);
    
    writeCommand(ILI9341_COLMOD);
    writeData(0x55);
    
    writeCommand(ILI9341_FRMCTR1);
    writeData(0x00);
    writeData(0x18);
    
    writeCommand(ILI9341_DFUNCTR);
    writeData(0x08);
    writeData(0x82);
    writeData(0x27);
    
    writeCommand(ILI9341_INVCTR);
    writeData(0x00);
    
    writeCommand(ILI9341_GAMMASET);
    writeData(0x01);
    
    // Gamma correction
    writeCommand(ILI9341_GMCTRP1);
    writeData(0x0F);
    writeData(0x31);
    writeData(0x2B);
    writeData(0x0C);
    writeData(0x0E);
    writeData(0x08);
    writeData(0x4E);
    writeData(0xF1);
    writeData(0x37);
    writeData(0x07);
    writeData(0x10);
    writeData(0x03);
    writeData(0x0E);
    writeData(0x09);
    writeData(0x00);
    
    writeCommand(ILI9341_GMCTRN1);
    writeData(0x00);
    writeData(0x0E);
    writeData(0x14);
    writeData(0x03);
    writeData(0x11);
    writeData(0x07);
    writeData(0x31);
    writeData(0xC1);
    writeData(0x48);
    writeData(0x08);
    writeData(0x0F);
    writeData(0x0C);
    writeData(0x31);
    writeData(0x36);
    writeData(0x0F);
    
    writeCommand(ILI9341_SLPOUT);
    delay(120);
    
    writeCommand(ILI9341_DISPON);
    delay(10);
    
    fillScreen(ILI9341_BLACK);
}

void SimpleILI9341::reset() {
    digitalWrite(TFT_RST, HIGH);
    delay(5);
    digitalWrite(TFT_RST, LOW);
    delay(20);
    digitalWrite(TFT_RST, HIGH);
    delay(150);
}

void SimpleILI9341::writeCommand(uint8_t cmd) {
    digitalWrite(TFT_DC, LOW);
    digitalWrite(TFT_CS, LOW);
    SPI.write(cmd);
    digitalWrite(TFT_CS, HIGH);
}

void SimpleILI9341::writeData(uint8_t data) {
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    SPI.write(data);
    digitalWrite(TFT_CS, HIGH);
}

void SimpleILI9341::writeWord(uint16_t word) {
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    SPI.write16(word);
    digitalWrite(TFT_CS, HIGH);
}

void SimpleILI9341::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writeCommand(ILI9341_CASET);
    writeData(x0 >> 8);
    writeData(x0 & 0xFF);
    writeData(x1 >> 8);
    writeData(x1 & 0xFF);
    
    writeCommand(ILI9341_PASET);
    writeData(y0 >> 8);
    writeData(y0 & 0xFF);
    writeData(y1 >> 8);
    writeData(y1 & 0xFF);
}

void SimpleILI9341::fillScreen(uint16_t color) {
    fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);
}

void SimpleILI9341::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    
    writeCommand(ILI9341_RAMWR);
    
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    
    uint32_t pixels = (uint32_t)w * h;
    while (pixels--) {
        SPI.write16(color);
    }
    
    digitalWrite(TFT_CS, HIGH);
}

void SimpleILI9341::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    setAddrWindow(x, y, x, y);
    
    writeCommand(ILI9341_RAMWR);
    writeWord(color);
}

void SimpleILI9341::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    // Draw horizontal lines
    for (uint16_t i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
        drawPixel(x + i, y + h - 1, color);
    }
    // Draw vertical lines
    for (uint16_t i = 0; i < h; i++) {
        drawPixel(x, y + i, color);
        drawPixel(x + w - 1, y + i, color);
    }
}

void SimpleILI9341::drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg) {
    // Get character code
    uint8_t idx = (uint8_t)c;
    
    // Check if character is in valid range (32-126)
    if (idx < 32 || idx > 126) {
        return;
    }
    
    // Adjust index (32 is first character)
    idx = idx - 32;
    
    // Get font data
    const uint8_t *fontData = font5x7[idx];
    
    // Draw character
    for (uint8_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t byte = fontData[row];
        
        for (uint8_t col = 0; col < FONT_WIDTH; col++) {
            // Check bit from MSB to LSB (bit 7 to bit 3)
            uint8_t bit = (byte >> (7 - col)) & 0x01;
            
            if (bit) {
                drawPixel(x + col, y + row, color);
            } else {
                if (bg != color) {  // Only draw background if different from foreground
                    drawPixel(x + col, y + row, bg);
                }
            }
        }
    }
}

void SimpleILI9341::drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg) {
    if (!text) return;
    
    uint16_t currentX = x;
    uint16_t currentY = y;
    
    while (*text) {
        char c = *text;
        
        // Handle newline
        if (c == '\n') {
            currentX = x;
            currentY += (FONT_HEIGHT + 2);
            text++;
            continue;
        }
        
        // Handle carriage return
        if (c == '\r') {
            text++;
            continue;
        }
        
        drawChar(currentX, currentY, c, color, bg);
        
        currentX += (FONT_WIDTH + 1);  // Add spacing between characters
        
        // Wrap to next line if needed
        if (currentX + FONT_WIDTH >= TFT_WIDTH) {
            currentX = x;
            currentY += (FONT_HEIGHT + 2);
        }
        
        text++;
    }
}

void SimpleILI9341::setBacklight(bool on) {
    // Backlight control can be added here if needed
    // For now, just a placeholder
}
