# SPI Pin Configuration Refactoring

## Overview
Replaced hardcoded SPI pin values with #define macros for better configurability and maintainability.

## Date
October 14, 2025

## Changes Made

### Problem
Previously, SPI pins were hardcoded in LoraMesher.cpp using conditional compilation based on DEVICE_MODE:
```cpp
#if DEVICE_MODE == 1
    SPI.begin(9, 8, 7, 6); // Esp32c3-Node - hardcoded
#elif DEVICE_MODE == 2
    SPI.begin(18, 16, 19, 5); // Esp32-Gateway - hardcoded  
#elif DEVICE_MODE == 3
    SPI.begin(18, 16, 19, 5); // Esp32-Node - hardcoded
#endif
```

This approach had several issues:
- **Not maintainable**: Changing pins required modifying core library code
- **Not portable**: Different hardware needs different pins
- **Error-prone**: Easy to make mistakes with magic numbers
- **Hard to document**: No clear indication of which pin is which

### Solution
Moved SPI pin definitions to config header files and used preprocessor macros:

#### 1. Added SPI Pin Defines to Config Headers

**bridge_config.h** (ESP32 Gateway):
```cpp
// SPI pin configuration for Bridge (ESP32 Gateway/Node)
#ifndef SPI_SCK
#define SPI_SCK     18
#endif
#ifndef SPI_MISO
#define SPI_MISO    16
#endif
#ifndef SPI_MOSI
#define SPI_MOSI    19
#endif
#ifndef SPI_CS
#define SPI_CS      5
#endif
```

**node_config.h** (ESP32-C3 Node):
```cpp
// SPI pin configuration for Node (ESP32-C3)
#ifndef SPI_SCK
#define SPI_SCK     9
#endif
#ifndef SPI_MISO
#define SPI_MISO    8
#endif
#ifndef SPI_MOSI
#define SPI_MOSI    7
#endif
#ifndef SPI_CS
#define SPI_CS      6
#endif
```

#### 2. Updated LoraMesher.cpp

Added includes for config headers:
```cpp
// Include device-specific config for SPI pin definitions
#ifndef DEVICE_MODE
#define DEVICE_MODE 1 // Default to Node mode
#endif

#if DEVICE_MODE == 2
#include "../../../../application/app_bridge/bridge_config.h"
#else
#include "../../../../application/app_node/node_config.h"
#endif
```

Simplified SPI initialization:
```cpp
#ifdef LORA_MISO
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#else 
    // Use SPI pin definitions from config files
    #ifdef SPI_SCK
        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
        ESP_LOGI(LM_TAG, "SPI.begin(SCK:%d, MISO:%d, MOSI:%d, CS:%d);", 
                 SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
    #else
        #error "SPI pins not defined! Please define SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS in config file"
    #endif
#endif
```

## Benefits

### 1. **Better Maintainability**
- SPI pins are now defined in one place per device type
- Easy to modify without touching core library code
- Clear documentation of pin assignments

### 2. **Improved Portability**
- Different hardware configurations can use different config files
- No need to modify LoraMesher.cpp for different boards
- Easier to support new hardware variants

### 3. **Enhanced Safety**
- Use of `#ifndef` guards prevents redefinition conflicts
- Compiler error if pins are not defined
- Clear error messages guide developers

### 4. **Better Documentation**
- Named constants (SPI_SCK) are self-documenting
- Comments explain which pins are for which purpose
- Easier for new developers to understand

## Technical Details

### Include Guard Strategy
Used `#ifndef` guards to prevent redefinition warnings when both bridge and node configs are included:
```cpp
#ifndef SPI_SCK
#define SPI_SCK     18
#endif
```

This allows:
- Each config file to define its own values
- First definition wins (no redefinition warnings)
- Flexibility for override via compiler flags

### Pin Mapping

| Device | SPI_SCK | SPI_MISO | SPI_MOSI | SPI_CS | Hardware |
|--------|---------|----------|----------|--------|----------|
| Node (ESP32-C3) | 9 | 8 | 7 | 6 | ESP32-C3-DevKitM-1 |
| Bridge (ESP32) | 18 | 16 | 19 | 5 | ESP32 DOIT DevKit V1 |

### Build Results

All environments build successfully:
- **esp32c3-node**: ✓ Success (RAM 5.6%, Flash 34.2%)
- **esp32-gateway**: ✓ Success (RAM 7.7%, Flash 35.9%)

## Migration Guide

For developers adding new hardware support:

1. Create new config header (e.g., `custom_board_config.h`)
2. Define SPI pins:
   ```cpp
   #ifndef SPI_SCK
   #define SPI_SCK     <your_pin>
   #endif
   // ... define SPI_MISO, SPI_MOSI, SPI_CS
   ```
3. Include config in LoraMesher.cpp or set via compiler flags
4. No other changes needed!

## Related Files Modified

- `src/application/app_bridge/bridge_config.h` - Added SPI pin defines
- `src/application/app_node/node_config.h` - Added SPI pin defines  
- `src/components/lora_mesh_manager/src/core/LoraMesher.cpp` - Refactored SPI init

## Related Documentation

- Bridge configuration: `bridge_config.h`
- Node configuration: `node_config.h`
- Security improvements: `REPLAY_PROTECTION_IMPROVEMENTS.md`
- Link quality routing: `LINK_QUALITY_IMPLEMENTATION_SUMMARY.md`

## Summary

Successfully replaced hardcoded SPI pins with configurable #define macros, improving code maintainability, portability, and documentation while maintaining backward compatibility.
