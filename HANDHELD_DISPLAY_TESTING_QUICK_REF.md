# Quick Reference: Handheld Display Hardware Testing

## Current Status
✅ **Handheld firmware compiled successfully with display support**

## Hardware Pinout Summary
```
Display Connection (ILI9341 240x320):
├── GPIO46 (IO46) ← DC Pin (Data/Command) **CRITICAL**
├── GPIO17 (IO17) ← Backlight Control
├── GPIO5  (IO5)  ← CS (Chip Select)
├── GPIO4  (IO4)  ← RESET
├── GPIO19 (IO19) ← MOSI (SPI Data Out)
├── GPIO18 (IO18) ← SCLK (SPI Clock)
├── GPIO16 (IO16) ← MISO (SPI Data In)
└── Power: 3.3V + GND
```

## Verification Checklist Before Upload

- [ ] DC pin wiring verified on GPIO46
- [ ] All SPI pins (18,19,16) connected correctly
- [ ] Backlight power on GPIO17 is connected
- [ ] Reset pin (GPIO4) is connected
- [ ] Chip Select (GPIO5) is connected
- [ ] Display has power (3.3V + GND)
- [ ] USB cable connected to ESP32

## Upload Commands

### Option 1: Full Build + Upload
```bash
cd D:/Projects/Lora/LM_LR_MESH
pio run -e esp32-handheld -t upload
```

### Option 2: Just Upload (if already built)
```bash
cd D:/Projects/Lora/LM_LR_MESH
pio run -e esp32-handheld -t upload --no-dep
```

### Option 3: View Serial Monitor
```bash
cd D:/Projects/Lora/LM_LR_MESH
pio device monitor -e esp32-handheld
```

## Expected Behavior on Device

### Power-Up Sequence (0-2 seconds)
1. ESP32 boots, initializes PSRAM
2. App calls `HandheldApp::initialize()`
3. Components start initializing
4. Display manager initializes

### Display Initialization (2-5 seconds)
1. GPIO46 (DC pin) configured as output
2. SPI bus initialized at 27MHz
3. ILI9341 receives initialization commands:
   - Power control sequences (0xCB, 0xCF, 0xE8, 0xEA, 0xED, 0xF7)
   - Voltage settings (0xC0, 0xC1, 0xC5, 0xC7)
   - Memory access settings (0x36, 0x3A)
4. Backlight turns ON (GPIO17 goes high)
5. Screen fills with **BLACK** color (test pattern)

### Serial Console Output
```
[HandheldApp] Initializing Handheld Application v1.6.0
[HandheldApp] Initializing components...
[HandheldApp] Initializing display...
[DisplayManager] Initializing display...
[ILI9341] Starting ILI9341 initialization...
[ILI9341] Setting resolution: 240 x 320
[ILI9341] Power control commands sent
[ILI9341] Initialization complete
[DisplayManager] Display initialized successfully
[HandheldApp] Initializing RS485 soil sensor...
[HandheldApp] WiFi initialized
...
```

## Troubleshooting

### Issue: Display stays black/no change
**Possible Causes:**
- DC pin (GPIO46) not connected - display won't respond
- SPI frequency too high - reduce to 20MHz
- Power supply not stable - verify 3.3V steady

**Test**: 
- Check GPIO46 with multimeter (should toggle during init)
- Verify backlight is on (GPIO17 should be high)

### Issue: Garbled display or wrong colors
**Possible Causes:**
- SPI clock pin (GPIO18) or data pin (GPIO19) wiring issue
- MISO pullup needed on GPIO16

**Test**:
- Upload standalone `test_display.cpp` build to verify SPI
- Check oscilloscope on clock/data lines

### Issue: Display flickering or partial update
**Possible Causes:**
- DC pin (GPIO46) timing issues
- SPI speed too high
- Power supply brownout

**Test**:
- Reduce SPI frequency in `display_manager.cpp` line ~15
- Verify 3.3V supply voltage under 100mA

### Issue: App crashes immediately
**Possible Causes:**
- PSRAM initialization conflict (shouldn't happen - using raw SPI)
- GPIO conflict with other components

**Test**:
- Check other components not using GPIO46, GPIO17
- Enable verbose logging in platformio.ini

## Standalone Display Test (Alternative)

If handheld app doesn't show display, verify with standalone test:

```bash
pio run -e esp32-display-test -t upload
```

This will:
- Skip app initialization
- Directly test ILI9341 communication
- Cycle through 5 colors continuously
- Show `DisplayManager` is working

## Performance Metrics

After upload, check via serial monitor:
- **Boot Time**: < 5 seconds to black screen
- **Color Fill Time**: < 100ms per screen
- **SPI Frequency**: 27MHz (verified)
- **Flash Usage**: ~1.06MB (33.6%)
- **RAM Usage**: ~46KB (14.2%)

## Next Integration Steps

Once display works on hardware:
1. Add text rendering (for sensor data)
2. Implement touch screen handling (if needed)
3. Create UI screens (home, settings, data view)
4. Integrate with sensor data updates
5. Add WiFi status display

---

**Last Updated**: 2025-11-26  
**ESP32-S3 Board**: 4D Systems GEN4-ESP32 16MB  
**Display**: ILI9341 240x320 TFT  
**Ready for Testing**: ✅ YES
