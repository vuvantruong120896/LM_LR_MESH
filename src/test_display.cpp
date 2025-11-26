#include <Arduino.h>
#include <SPI.h>

// Hardware pins
#define TFT_MOSI  19
#define TFT_SCLK  18  
#define TFT_MISO  16
#define TFT_CS    5
#define TFT_RST   4
#define TFT_BL    17
#define TFT_DC    46

void sendCommand(uint8_t cmd) {
    digitalWrite(TFT_DC, LOW);   
    digitalWrite(TFT_CS, LOW);   
    SPI.transfer(cmd);
    digitalWrite(TFT_CS, HIGH);  
    delayMicroseconds(100);
}

void sendData(uint8_t data) {
    digitalWrite(TFT_DC, HIGH);  
    digitalWrite(TFT_CS, LOW);   
    SPI.transfer(data);
    digitalWrite(TFT_CS, HIGH);  
    delayMicroseconds(100);
}

void sendData16(uint16_t data) {
    digitalWrite(TFT_DC, HIGH);  
    digitalWrite(TFT_CS, LOW);   
    SPI.transfer16(data);
    digitalWrite(TFT_CS, HIGH);  
    delayMicroseconds(100);
}

void initDisplay() {
    Serial.println("Initializing ILI9341...");
    
    // Software reset
    sendCommand(0x01);
    delay(150);
    
    // Sleep out
    sendCommand(0x11);
    delay(120);
    
    // Power control A
    sendCommand(0xCB);
    sendData(0x39);
    sendData(0x2C);
    sendData(0x00);
    sendData(0x34);
    sendData(0x02);
    
    // Power control B
    sendCommand(0xCF);
    sendData(0x00);
    sendData(0xC1);
    sendData(0x30);
    
    // Driver timing control A
    sendCommand(0xE8);
    sendData(0x85);
    sendData(0x00);
    sendData(0x78);
    
    // Driver timing control B
    sendCommand(0xEA);
    sendData(0x00);
    sendData(0x00);
    
    // Power on sequence
    sendCommand(0xED);
    sendData(0x64);
    sendData(0x03);
    sendData(0x12);
    sendData(0x81);
    
    // Pump ratio
    sendCommand(0xF7);
    sendData(0x20);
    
    // Power control 1
    sendCommand(0xC0);
    sendData(0x23);
    
    // Power control 2
    sendCommand(0xC1);
    sendData(0x10);
    
    // VCOM control 1
    sendCommand(0xC5);
    sendData(0x3E);
    sendData(0x28);
    
    // VCOM control 2
    sendCommand(0xC7);
    sendData(0x86);
    
    // Memory access control
    sendCommand(0x36);
    sendData(0x48);
    
    // Pixel format
    sendCommand(0x3A);
    sendData(0x55);
    
    // Frame rate control
    sendCommand(0xB1);
    sendData(0x00);
    sendData(0x18);
    
    // Display function control
    sendCommand(0xB6);
    sendData(0x08);
    sendData(0x82);
    sendData(0x27);
    
    // Gamma correction
    sendCommand(0xF2);
    sendData(0x00);
    
    // Gamma curve 1
    sendCommand(0xE0);
    sendData(0x0F); sendData(0x31); sendData(0x2B); sendData(0x0C);
    sendData(0x0E); sendData(0x08); sendData(0x4E); sendData(0xF1);
    sendData(0x37); sendData(0x07); sendData(0x10); sendData(0x03);
    sendData(0x0E); sendData(0x09); sendData(0x00);
    
    // Gamma curve 2
    sendCommand(0xE1);
    sendData(0x00); sendData(0x0E); sendData(0x14); sendData(0x03);
    sendData(0x11); sendData(0x07); sendData(0x31); sendData(0xC1);
    sendData(0x48); sendData(0x08); sendData(0x0F); sendData(0x0C);
    sendData(0x31); sendData(0x36); sendData(0x0F);
    
    // Display on
    sendCommand(0x29);
    delay(100);
    
    Serial.println("✅ ILI9341 initialized!");
}

void fillColor(uint16_t color) {
    // Set window
    sendCommand(0x2A);
    sendData16(0x0000);
    sendData16(0x00EF);
    
    sendCommand(0x2B);
    sendData16(0x0000);
    sendData16(0x013F);
    
    // Write RAM
    sendCommand(0x2C);
    
    // Fill screen
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    
    for(int i = 0; i < 240 * 320; i++) {
        SPI.transfer16(color);
    }
    
    digitalWrite(TFT_CS, HIGH);
}

void identifyDisplay() {
    Serial.println("🔍 Display initialized successfully!");
}
void setup() {
    delay(2000);
    
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("=== ILI9341 COLOR TEST ===");
    
    // Setup pins
    pinMode(TFT_BL, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_DC, OUTPUT);
    
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_RST, HIGH);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_BL, HIGH);
    
    Serial.println("✅ Pins configured");
    
    // SPI setup
    SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
    SPI.setFrequency(27000000); // 27MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    Serial.println("✅ SPI at 27MHz");
    
    // Reset display
    Serial.println("Resetting display...");
    digitalWrite(TFT_RST, LOW);
    delay(200);
    digitalWrite(TFT_RST, HIGH);
    delay(300);
    Serial.println("✅ Reset complete");
    
    // Backlight test
    Serial.println("Backlight test...");
    for(int i = 0; i < 3; i++) {
        digitalWrite(TFT_BL, LOW);
        delay(200);
        digitalWrite(TFT_BL, HIGH);
        delay(200);
    }
    Serial.println("✅ Backlight working");
    
    // Initialize display
    initDisplay();
    delay(500);
    
    // Test colors
    Serial.println("🔴 RED");
    fillColor(0xF800);
    delay(2000);
    
    Serial.println("🟢 GREEN");
    fillColor(0x07E0);
    delay(2000);
    
    Serial.println("🔵 BLUE");
    fillColor(0x001F);
    delay(2000);
    
    Serial.println("⚪ WHITE");
    fillColor(0xFFFF);
    delay(2000);
    
    Serial.println("⚫ BLACK");
    fillColor(0x0000);
    
    Serial.println("=== TEST COMPLETE ===");
}

void loop() {
    static unsigned long lastBeat = 0;
    static uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF};
    static int idx = 0;
    
    if (millis() - lastBeat > 5000) {
        Serial.printf("💓 Color #%d\n", idx);
        fillColor(colors[idx]);
        idx = (idx + 1) % 6;
        
        // Flash backlight
        digitalWrite(TFT_BL, LOW);
        delay(100);
        digitalWrite(TFT_BL, HIGH);
        
        lastBeat = millis();
    }
    
    delay(100);
}