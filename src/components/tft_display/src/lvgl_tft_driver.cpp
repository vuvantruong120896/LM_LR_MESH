/**
 * @file lvgl_tft_driver.cpp
 * Simple display driver using only LovyanGFX (no LVGL)
 */

#include "lvgl_tft_driver.h"
#include "lovyan_gfx_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_log.h>

// Global LovyanGFX display instance
LGFX display;

/**
 * TFT initialization with LovyanGFX
 */
void tft_init(void) {
    ESP_LOGI(TAG_LVGL, "Initializing TFT display with LovyanGFX...");
    
    // Initialize LovyanGFX display
    display.init();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Set to portrait mode
    display.setRotation(0);
    
    // Clear screen
    display.fillScreen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Turn on backlight
    tft_backlight_on();
    ESP_LOGI(TAG_LVGL, "TFT display initialized (portrait), backlight ON");
}

/**
 * Backlight control
 */
void tft_backlight_on(void) {
    gpio_set_level((gpio_num_t)TFT_BL, 1);
    ESP_LOGI(TAG_LVGL, "Backlight ON");
}

void tft_backlight_off(void) {
    gpio_set_level((gpio_num_t)TFT_BL, 0);
    ESP_LOGI(TAG_LVGL, "Backlight OFF");
}

/**
 * Stub LVGL functions (not used)
 */
void lv_flush_cb(void* disp_drv, const void* area, void* color_map) {
    // Not used in LovyanGFX-only mode
}

void lvgl_init(void) {
    // Not used in LovyanGFX-only mode
    ESP_LOGI(TAG_LVGL, "LVGL disabled - using LovyanGFX only");
}
