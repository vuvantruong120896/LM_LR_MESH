#include "tft_display_manager.h"
#include "lovyan_gfx_config.h"
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>

DisplayManager* DisplayManager::instance = nullptr;
LGFX display_instance;

DisplayManager::DisplayManager() : display(nullptr), initialized(false) {
    ESP_LOGI(TAG_DISPLAY, "DisplayManager constructor called");
}

DisplayManager::~DisplayManager() {
    if (display != nullptr) {
        // LovyanGFX will be deleted automatically
        display = nullptr;
    }
    ESP_LOGI(TAG_DISPLAY, "DisplayManager destructor called");
}

DisplayManager* DisplayManager::getInstance() {
    if (instance == nullptr) {
        instance = new DisplayManager();
    }
    return instance;
}

void DisplayManager::init() {
    if (initialized) {
        ESP_LOGW(TAG_DISPLAY, "Display already initialized");
        return;
    }
    
    if (display == nullptr) {
        ESP_LOGI(TAG_DISPLAY, "Initializing LovyanGFX display...");
        ESP_LOGI(TAG_DISPLAY, "Pin config: CS=%d, DC=%d, RST=%d, MOSI=%d, MISO=%d, CLK=%d, BL=%d",
                 TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_MISO, TFT_SCLK, TFT_BL);
        
        // Use global display instance
        display = &display_instance;
        
        // Initialize display
        display->init();
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Set rotation
        display->setRotation(1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Clear screen
        display->fillScreen(0x0000);  // Black
        vTaskDelay(50 / portTICK_PERIOD_MS);
        
        // Turn on backlight after display is ready
        enableBacklight();
        
        initialized = true;
        ESP_LOGI(TAG_DISPLAY, "Display initialized successfully");
    }
}

bool DisplayManager::initialize() {
    init();
    return initialized;
}

void DisplayManager::clear(uint16_t color) {
    if (display) display->fillScreen(color);
}

void DisplayManager::drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bg) {
    if (!display) return;
    display->setTextColor(color, bg);
    display->setCursor(x, y);
    display->print(text);
}

void DisplayManager::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (display) display->fillRect(x, y, w, h, color);
}

void DisplayManager::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (display) display->drawRect(x, y, w, h, color);
}

void DisplayManager::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (display) display->drawPixel(x, y, color);
}

void DisplayManager::drawHomeScreen() {
    if (!display) return;
    
    display->fillScreen(0x0000);  // Black background
    display->setTextColor(0xFFFF, 0x0000);  // White text on black
    display->setTextSize(3);
    
    display->setCursor(50, 50);
    display->print("KAGRI HOME");
    
    display->setTextSize(2);
    display->setCursor(10, 100);
    display->print("Nhấn nút để bắt đầu đo");
    display->setCursor(10, 120);
    display->print("Nhấn 5s để vào cài đặt");
}

void DisplayManager::displaySensorData(float temperature, float moisture, float ec, float n, float p, float k) {
    if (!display) return;
    
    display->fillScreen(0x0000);
    display->setTextColor(0xFFFF);
    display->setTextSize(1);
    
    display->setCursor(10, 10);
    display->printf("Temp: %.1f C", temperature);
    
    display->setCursor(10, 30);
    display->printf("Moisture: %.1f%%", moisture);
    
    display->setCursor(10, 50);
    display->printf("EC: %.1f", ec);
}

void DisplayManager::showScreen(DisplayScreen screen) {
    switch (screen) {
        case DisplayScreen::HOME:
            drawHomeScreen();
            break;
        case DisplayScreen::SOIL_DATA:
            // displaySensorData will be called separately
            break;
        case DisplayScreen::DEVICE_CONFIG:
            drawDeviceConfig();
            break;
        default:
            break;
    }
}

void DisplayManager::drawDeviceConfig() {
    if (!display) return;
    
    display->fillScreen(0x0000);
    display->setTextColor(0xFFFF);
    display->setTextSize(1);
    
    display->setCursor(10, 10);
    display->print("Device Config");
}

uint16_t DisplayManager::RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void DisplayManager::enableBacklight() {
    gpio_set_level((gpio_num_t)TFT_BL, 1);
    ESP_LOGI(TAG_DISPLAY, "Backlight turned ON");
}

void DisplayManager::disableBacklight() {
    gpio_set_level((gpio_num_t)TFT_BL, 0);
    ESP_LOGI(TAG_DISPLAY, "Backlight turned OFF");
}
