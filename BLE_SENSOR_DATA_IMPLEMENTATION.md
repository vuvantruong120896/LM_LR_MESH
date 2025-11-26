# BLE Sensor Data Service Implementation

## Overview
Implemented a separate BLE service for transmitting sensor data from the handheld device to the mobile app, distinct from WiFi provisioning BLE.

## Files Created

### 1. `ble_sensor_data.h` - Header file
- New class `BleSensorData` for sensor data BLE service
- Device advertises as: `KAGRI_HHT-XXYY` (last 2 bytes of MAC)
- Different UUIDs from provisioning service for app differentiation
- Service UUID: `0000ffe0-0000-1000-8000-00805f9b34fb`
- Sensor data characteristic: `0000ffe1-0000-1000-8000-00805f9b34fb` (NOTIFY)
- Command characteristic: `0000ffe2-0000-1000-8000-00805f9b34fb` (WRITE)

### 2. `ble_sensor_data.cpp` - Implementation
- `begin()`: Starts BLE advertising for sensor data
- `stop()`: Stops BLE and cleans up
- `isActive()`: Checks if BLE is running
- `sendSensorData()`: Sends current sensor data as JSON via BLE notification
- Sensor data format: JSON with temp, moisture, ec, pH, N, P, K, timestamp

## Files Modified

### 1. `handheld_app.h`
- Added `#include "ble_sensor_data.h"`
- Added new AppState: `SENSOR_DATA_TRANSFER`
- Added member variable: `BleSensorData* bleSensorData`

### 2. `handheld_app.cpp`
- **Constructor**: Initialize `bleSensorData` to nullptr
- **Destructor**: Clean up `bleSensorData`
- **initializeComponents()**: Create BleSensorData instance
- **handleButtonPress()**: 5s press in IDLE state triggers WiFi_CONFIG
- **loop()**: Added SENSOR_DATA_TRANSFER state case with:
  - Displays BLE waiting screen for 30 seconds
  - Updates countdown every 1 second
  - Auto-returns to IDLE after timeout
  - Resets static flags on state changes
- **changeState()**: 
  - Stop BLE sensor data when leaving SENSOR_DATA_TRANSFER
  - Start BLE sensor data when entering SENSOR_DATA_TRANSFER
  - Auto-send latest sensor data to connected phone

## Usage Workflow

1. **User takes sensor reading**: Click button → MEASURING state → displays sensor data
2. **User presses button 5s**: From IDLE state → WIFI_CONFIG state (existing WiFi setup)
3. **User presses button 5s**: From SENSOR_DATA_TRANSFER state → Starts BLE for sensor data
4. **Phone connects**: Receives JSON sensor data via BLE notification
5. **After 30s timeout**: Automatically returns to IDLE state

## Advertisement Names

- **WiFi Configuration**: `KAGRI_HHC-XXYY` (provisioning)
- **Sensor Data**: `KAGRI_HHT-XXYY` (data transmission)

Where XXYY = last 2 bytes of device MAC address

## Data Format (JSON)

```json
{
  "temp": 25.5,
  "moisture": 65.2,
  "ec": 1.2,
  "pH": 7.1,
  "N": 45.2,
  "P": 12.3,
  "K": 89.4,
  "timestamp": 1234567890
}
```

## Notes

- BLE services are mutually exclusive (WiFi config OR sensor data, not both)
- Sensor data is sent automatically on phone connection
- 30-second timeout prevents battery drain if phone doesn't disconnect
- Static screen flags reset properly between state entries
- Full compatibility with existing WiFi provisioning flow

