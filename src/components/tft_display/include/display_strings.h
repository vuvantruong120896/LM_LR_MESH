/**
 * @file display_strings.h
 * Display strings with Vietnamese support via LVGL FreeType
 */

#ifndef DISPLAY_STRINGS_H
#define DISPLAY_STRINGS_H

namespace DisplayStrings {
    // Home screen
    constexpr const char* HOME_TITLE = "KAGRI HOME";
    
    // Vietnamese text - UTF-8 encoded (LVGL FreeType will render properly)
    constexpr const char* PRESS_BUTTON_MEASURE = u8"Nhấn nút để bắt đầu đo";
    constexpr const char* PRESS_5S_CONFIG = u8"Nhấn 5s để vào cài đặt";
    
    // Device config
    constexpr const char* DEVICE_CONFIG = "Device Config";
    
    // Sensor labels
    constexpr const char* TEMP_LABEL = "Temp: ";
    constexpr const char* MOISTURE_LABEL = "Moisture: ";
    constexpr const char* EC_LABEL = "EC: ";
    constexpr const char* PH_LABEL = "pH: ";
    
    // Status messages
    constexpr const char* MEASURING = u8"Đang đo...";
    constexpr const char* CONFIGURED = u8"Đã cấu hình";
}

#endif
