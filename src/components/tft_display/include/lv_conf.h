/**
 * @file lv_conf.h
 * Configuration for LVGL v8.3
 * Vietnamese language support for ILI9341 display on ESP32-S3
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16-bit RGB565 */
#define LV_COLOR_DEPTH 16

/* Horizontal and vertical resolution of the display */
#define LV_HOR_RES_MAX 240
#define LV_VER_RES_MAX 320

/* Enable FreeType for full Unicode/Vietnamese support */
#define LV_USE_FREETYPE 0
#define LV_FREETYPE_CACHE_SIZE (64 * 1024)

/* Memory allocation */
#define LV_MEM_SIZE (48 * 1024)

/* Default font for Vietnamese text */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1

/* Enable all basic widgets */
#define LV_USE_BTN 1
#define LV_USE_LABEL 1
#define LV_USE_CONT 1
#define LV_USE_IMG 1
#define LV_USE_SCRL 1
#define LV_USE_TEXTAREA 1

/* Enable animations */
#define LV_USE_ANIM 1

/* Enable styles */
#define LV_USE_STYLE 1

/* Tick and display settings */
#define LV_TICK_CUSTOM 0
#define LV_TICK_CUSTOM_INCLUDE ""
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Use Unicode */
#define LV_USE_UNICODE 1

/* Enable Cyrillic */
#define LV_USE_CYRILLIC 0

/* Enable Thai */
#define LV_USE_THAI 0

/* Log module for debugging */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* Assert */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1

/* Other settings */
#define LV_USE_GPU 0
#define LV_USE_EMEMSET 1
#define LV_USE_LARGE_COORD 0

#endif
