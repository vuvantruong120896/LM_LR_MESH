// User_Setup.h for TFT_eSPI - ESP32-S3 ILI9341 Configuration
// Based on handheld_config.h pin definitions

#ifndef USER_SETUP_H
#define USER_SETUP_H

// ##########################################
// Section 1: Software driven SPI
// ##########################################

#define USER_SETUP_INFO "User_Setup for ESP32-S3 ILI9341"

// ##########################################
// Section 2: Selection of TFT driver IC
// ##########################################

#define ILI9341_DRIVER

// ##########################################
// Section 3: Define the pins that are used
// ##########################################

// For ESP32-S3 dev board
#define TFT_CS   5      // Chip select control pin (library sets phase correct PWM frequency)
#define TFT_DC   46     // Data Command control pin
#define TFT_SDA  19     // MOSI (DIN) - use 23 for ESP32S3, 14 for ESP8266
#define TFT_SCL  18     // SCK (CLK) - use 5 for ESP32S3, 4 for ESP8266
#define TFT_RST  4      // Reset pin (could connect to RST pin)
#define TFT_BL   17     // LED back-light (only for ST7789 with backlight control pin)

// ##########################################
// Section 4: Setup SPI speed
// ##########################################

// Speed up SPI clock for faster rendering
#define SPI_FREQUENCY  20000000

// Optional reduced SPI frequency for reading TFT
#define SPI_READ_FREQUENCY  10000000

// ##########################################
// Section 5: Font antialiasing
// ##########################################

#define SMOOTH_FONT

// ##########################################
// Section 6: Load specific fonts
// ##########################################

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
//#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
//#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
//#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
//#define LOAD_FONT8N // Font 8. Alternative to Font 8, slightly narrower, so 3 digits fit in width of TFT

#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit FreeFonts FF1 to FF48 and FM24

// ##########################################
// Section 7: Default textsize
// ##########################################

#define DEFAULT_TEXTSIZE 1

// ##########################################
// Section 8: User defined colour macros
// ##########################################

#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFD20
#define TFT_GREENYELLOW 0xAFE5
#define TFT_PINK        0xF81F
#define TFT_BROWN       0x9A60
#define TFT_GOLD        0xFEA0
#define TFT_SILVER      0xC618
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C
#define TFT_TRANSPARENT 0x0120

// ##########################################
// Section 9: Font antialiasing
// ##########################################

// If smooth fonts are enabled the library has about 40KB extra
// memory used to stay resident in the SRAM, this will not significantly
// change the main sketch memory usage. Set to 0 to disable this feature.

#define SMOOTH_FONT

// ##########################################
// Section 10: Optional touch screen
// ##########################################

// Touch is not connected on this device
//#define TOUCH_CS 33  // Chip select pin (T_CS) of touch screen

// ##########################################
// Section 11: The HSPI or VSPI port can be used, deflt is VSPI
// ##########################################

// #define USE_HSPI_PORT

// ##########################################
// Section 12: SPI clock frequency
// ##########################################

// The SPI clock rate of 40MHz works reliably on all ESP32 variant boards tested.
// For ESP32 Pro and other variant boards SPI writer performance appears to be improved by
// almost a factor of 2 by using SPI Mode 1 instead of Mode 0.
// Define SPI_MODE1 to use SPI mode 1 (CPHA = 1)

// #define SPI_MODE1

// ##########################################
// Section 13: SPI bus sharing
// ##########################################

// Leave undefined by default. The SPI bus may be shared with SD card and other SPI devices.
// If SD card bus sharing is enabled the TFT chip select line MUST USE the same GPIO as the SD_CS line.

// #define USE_SD_SPI_BUS

#endif // USER_SETUP_H
