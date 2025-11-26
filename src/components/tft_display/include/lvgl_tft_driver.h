/**
 * @file lvgl_tft_driver.h
 * Display driver for LovyanGFX only (no LVGL)
 */

#ifndef LVGL_TFT_DRIVER_H
#define LVGL_TFT_DRIVER_H

#include "lovyan_gfx_config.h"
#include <esp_log.h>

#define TAG_LVGL "LVGL_TFT"

// Global display instance (defined in lvgl_tft_driver.cpp)
extern LGFX display;

/**
 * Initialize LVGL display driver (stub - not used)
 */
void lvgl_init(void);

/**
 * LVGL flush callback (stub - not used)
 */
void lv_flush_cb(void* disp_drv, const void* area, void* color_map);

/**
 * Initialize TFT display (SPI, GPIO, backlight)
 */
void tft_init(void);

/**
 * Backlight control
 */
void tft_backlight_on(void);
void tft_backlight_off(void);

#endif
