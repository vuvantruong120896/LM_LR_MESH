# Modbus Address Scanner Guide

## Quick Start

1. **Upload firmware:**
   ```bash
   pio run -e esp32-sensor-test --target upload
   ```

2. **Monitor output:**
   ```bash
   pio device monitor -e esp32-sensor-test --filter=esp32_exception_decoder
   ```

3. **Wait for scan to complete** (~4-5 minutes for 255 addresses)

## Expected Output

```
[SENSOR-TEST] ====== MODBUS ADDRESS SCANNER ======
[SENSOR-TEST] Scanning addresses 0x01 to 0xFF...
[SENSOR-TEST] Timeout: 500 ms, Retries: 1
[SENSOR-TEST] This will take approximately 4 minutes

[00s] Scanning 0x0001 - 0x0010...
[08s] Scanning 0x0011 - 0x0020...
[16s] Scanning 0x0021 - 0x0030...
...

✅✅✅ FOUND DEVICE at 0x04B2 (1202) ✅✅✅
    Register 0x0000 value: 452.0

...

====== SCAN COMPLETE ======
Scan duration: 245 seconds (4 minutes)
Total devices found: 1

Found devices:
  1. Address: 0x04B2 (1202 decimal)

📝 Update rs485_config.h:
   #define MODBUS_SLAVE_ADDRESS  0x04B2
```

## Troubleshooting: No Devices Found

### 1. Check RS485 Wiring

**Correct wiring:**
```
ESP32-S3         RS485 Module       Soil Sensor
--------         ------------       -----------
GPIO 21 (TX) --> DI (Data In)
GPIO 20 (RX) <-- RO (Data Out)
GPIO 42 -----> DE/RE (Enable)
                     A+ ----------> A+ (Data+)
                     B- ----------> B- (Data-)
GND -----------> GND ------------> GND
```

**Common mistakes:**
- ❌ A and B swapped → Try swapping them
- ❌ No GND connection → Add GND wire
- ❌ DE pin not connected → Enable signal won't work

### 2. Check Power Supply

**Sensor power requirements:**
- Most soil sensors: 5-12V DC
- Current: 50-200mA typical
- **Important:** ESP32 3.3V is NOT enough!

**Verification:**
```bash
# Use multimeter to measure:
- Sensor power pins: Should read 5V or 12V
- RS485 A-B voltage: Should show activity during scan
```

### 3. Check Baud Rate

**Current setting:** 9600 bps (in `rs485_config.h`)

**Try common rates if no response:**
- 4800 bps
- 9600 bps (default)
- 19200 bps
- 38400 bps

**To change baud rate:**
Edit `src/components/rs485_soil_sensor/include/rs485_config.h`:
```cpp
#define MODBUS_BAUD_RATE  9600  // Change to 4800, 19200, etc
```

### 4. Check Physical Hardware

**Cable quality:**
- Maximum length: 1000m (for RS485)
- Use shielded twisted pair for noisy environments
- Check for breaks/shorts with multimeter

**Termination resistors:**
- Required for long cables (>10m)
- 120Ω resistor between A and B at both ends
- Not always needed for short cables

**RS485 module LED indicators:**
- TX LED: Should blink during scan
- RX LED: Should blink if device responds
- Power LED: Should be always on

## Optimize Scan Speed

### Current Performance
- **Timeout:** 500ms per address
- **Retries:** 1 (total 2 attempts)
- **Total time:** ~4-5 minutes for full scan

### Fast Scan Mode (for troubleshooting only)

Edit `rs485_config.h`:
```cpp
// Original (reliable but slow)
#define MODBUS_RESPONSE_TIMEOUT_MS  500
#define MODBUS_RETRY_COUNT      1

// Fast scan (may miss some devices)
#define MODBUS_RESPONSE_TIMEOUT_MS  200  // Reduced timeout
#define MODBUS_RETRY_COUNT      0        // No retries

// Rebuild after changes:
// pio run -e esp32-sensor-test --target upload
```

**⚠️ Warning:** Fast mode may miss slow-responding devices. Use only for initial troubleshooting.

**After finding address, restore original settings:**
```cpp
#define MODBUS_RESPONSE_TIMEOUT_MS  500
#define MODBUS_RETRY_COUNT      1
```

## Advanced Debugging

### Enable Verbose Logging

Add to `platformio.ini` under `[env:esp32-sensor-test]`:
```ini
build_flags = 
    -D CORE_DEBUG_LEVEL=5        # Verbose
    -D SENSOR_TEST_BUILD
```

### Manual Address Test

Instead of full scan, test specific address:

1. Edit `rs485_config.h`:
   ```cpp
   #define MODBUS_SLAVE_ADDRESS  0x01  // Try: 0x01, 0x02, 0x10, 0xFF
   ```

2. Change test mode in `main_sensor_test.cpp`:
   ```cpp
   #define TEST_MODE_SINGLE  // Instead of TEST_MODE_SCAN
   ```

3. Build and upload:
   ```bash
   pio run -e esp32-sensor-test --target upload
   pio device monitor
   ```

### Check UART Communication

Monitor raw UART traffic:
```cpp
// Add to scan function for debugging:
size_t available = uart_read_bytes(RS485_UART_NUM, buffer, sizeof(buffer), 0);
if (available > 0) {
    ESP_LOGI(TAG, "Raw RX: %d bytes", available);
    for (int i = 0; i < available; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
}
```

## Common Sensor Addresses

**Typical defaults:**
- `0x0001` - Most common default
- `0x0002` - Secondary device
- `0x00FE` - Broadcast address (some sensors)
- `0x00FF` - Broadcast address (some sensors)

**Manufacturer-specific:**
- **Chinese soil sensors:** Usually `0x01` or `0x02`
- **Industrial sensors:** Often configurable via DIP switches
- **RS485-to-TTL modules:** May have their own address

**Scan optimization:**
If you know likely range, modify scan loop:
```cpp
// Instead of 0x0001 to 0x00FF
for (uint16_t addr = 0x0001; addr <= 0x0010; addr++) {
    // Scan only first 16 addresses
}
```

## Next Steps After Finding Address

1. **Update configuration:**
   ```cpp
   // In rs485_config.h
   #define MODBUS_SLAVE_ADDRESS  0xXXXX  // Your found address
   ```

2. **Test single read:**
   ```cpp
   // In main_sensor_test.cpp
   #define TEST_MODE_SINGLE
   ```

3. **Verify all 7 parameters:**
   - Moisture
   - Temperature  
   - pH
   - EC (Conductivity)
   - Nitrogen (N)
   - Phosphorus (P)
   - Potassium (K)

4. **Run continuous test:**
   ```cpp
   #define TEST_MODE_CONTINUOUS
   ```

5. **Integrate with main application**

## Support

If scan still finds nothing after all checks:
1. Verify sensor is Modbus RTU compatible
2. Check sensor documentation for default address
3. Try sensor with known-working Modbus master (PC software)
4. Contact sensor manufacturer for configuration tool
