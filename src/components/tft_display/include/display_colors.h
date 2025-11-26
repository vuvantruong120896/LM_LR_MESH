#ifndef DISPLAY_COLORS_H
#define DISPLAY_COLORS_H

#include <stdint.h>

// RGB565 Color Definitions
// Format: 16-bit color (5 bits R, 6 bits G, 5 bits B)

namespace DisplayColor {
    // Basic Colors
    constexpr uint16_t BLACK = 0x0000;      // 000000
    constexpr uint16_t WHITE = 0xFFFF;      // FFFFFF
    
    // Primary Colors
    constexpr uint16_t RED = 0xF800;        // FF0000
    constexpr uint16_t GREEN = 0x07E0;      // 00FF00
    constexpr uint16_t BLUE = 0x001F;       // 0000FF
    
    // Secondary Colors
    constexpr uint16_t CYAN = 0x07FF;       // 00FFFF
    constexpr uint16_t MAGENTA = 0xF81F;    // FF00FF
    constexpr uint16_t YELLOW = 0xFFE0;     // FFFF00
    
    // Light Colors
    constexpr uint16_t LIGHT_GRAY = 0xD69A;     // C0C0C0
    constexpr uint16_t DARK_GRAY = 0x7BEF;      // 808080
    constexpr uint16_t LIGHT_RED = 0xFB56;      // FF5555
    constexpr uint16_t LIGHT_GREEN = 0x9FE0;    // 00FF00 (lighter)
    constexpr uint16_t LIGHT_BLUE = 0x1F5F;     // 5555FF
    
    // Dark Colors
    constexpr uint16_t DARK_RED = 0x7800;       // 800000
    constexpr uint16_t DARK_GREEN = 0x0400;     // 008000
    constexpr uint16_t DARK_BLUE = 0x000F;      // 000080
    
    // UI Colors
    constexpr uint16_t BACKGROUND = BLACK;      // Black background
    constexpr uint16_t TEXT_PRIMARY = WHITE;    // White text
    constexpr uint16_t TEXT_SECONDARY = CYAN;   // Cyan for secondary text
    constexpr uint16_t TEXT_WARN = YELLOW;      // Yellow for warnings
    constexpr uint16_t TEXT_ERROR = RED;        // Red for errors
    constexpr uint16_t TEXT_SUCCESS = GREEN;    // Green for success
    constexpr uint16_t ACCENT = CYAN;           // Cyan accent color
    constexpr uint16_t BORDER = LIGHT_GRAY;     // Light gray for borders
    
    // Data Display Colors
    constexpr uint16_t TEMP_COLOR = LIGHT_BLUE;     // Temperature
    constexpr uint16_t MOISTURE_COLOR = GREEN;      // Moisture
    constexpr uint16_t pH_COLOR = YELLOW;           // pH
    constexpr uint16_t EC_COLOR = LIGHT_GREEN;      // EC (Electrical Conductivity)
    constexpr uint16_t BATTERY_COLOR = GREEN;       // Battery
    constexpr uint16_t SIGNAL_COLOR = CYAN;         // Signal strength
}

#endif // DISPLAY_COLORS_H
