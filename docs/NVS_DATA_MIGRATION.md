# NVS Data Migration & Versioning

## Overview

This document describes the NVS (Non-Volatile Storage) data layout and migration strategy for the firmware, particularly for the offline data buffer that stores sensor readings when connectivity is lost.

## Data Structure Evolution

### Version 0.x → Version 1.0 (Current)

**Breaking Change**: `sensorData` struct layout changed to support multi-sensor types.

**Old Structure** (before 1.0):
```cpp
struct sensorData {
    uint32_t counter;
    float temperature;      // 4 bytes
    float humidity;         // 4 bytes
    float battery;
    uint32_t timestamp;
    uint16_t nodeId;
    // Total: ~22 bytes
};
```

**New Structure** (1.0+):
```cpp
struct sensorData {
    DeviceType deviceType;  // 1 byte
    uint32_t counter;
    float battery;
    uint32_t timestamp;
    uint16_t nodeId;
    union {
        struct { float soil[7]; } soil;        // 28 bytes max
        struct { float env[4]; } environment;
        struct { float water[4]; } water;
        float values[8];
    } data;
    // Total: ~44 bytes
};
```

## NVS Storage Layout

### Offline Buffer Namespace: `offline_buf`

```
Key           | Type   | Purpose
--------------|--------|--------------------------------------------------
"head"        | u16    | Index of next write position (circular buffer)
"tail"        | u16    | Index of next read position (oldest data)
"count"       | u16    | Current count of buffered items
"version"     | u8     | NVS schema version (NEW in 1.0)
"nid_XXX"     | str    | Node ID at index XXX (up to 50 entries)
"dat_XXX"     | blob   | Sensor data blob at index XXX
```

### Schema Version

- **Version 0**: Old temperature/humidity structure (deprecated)
- **Version 1**: New multi-sensor union structure (current)

## Migration Strategy

### On First Boot (Version Mismatch)

When the device boots and detects an old NVS schema version:

1. **Log Warning**: "⚠️ Detected old NVS schema version: %d, current: %d"
2. **Clear Buffer**: `OfflineDataBuffer::clearAll()` - old data format incompatible
3. **Update Version**: Write new version number to NVS
4. **Reinitialize**: Fresh buffer ready for new data

### Rationale

- Binary blob format differences between versions would cause **memory corruption**
- Old temperature/humidity data cannot be automatically converted to soil sensor readings (semantically different)
- 50-sample buffer provides sufficient redundancy for ~8-12 minutes typical offline time
- Firebase already has cloud backup of all uploaded data

## Implementation Details

### Version Check Location
- File: `offline_data_buffer.cpp::initialize()`
- Performs version check and migration if needed

### Version Constants
```cpp
static const uint8_t CURRENT_NVS_SCHEMA_VERSION = 1;
static const char* KEY_VERSION = "version";
```

### No Data Loss in Practice
- Most data is continuously uploaded to Firebase when online
- Offline buffer only holds **recent** data that failed to upload
- Sensor data is timestamped, so replay is feasible even if format differs
- User can always view historical data in Firebase console

## Future Migrations

If schema needs to change again:

1. Create new namespace: `offline_buf_v2` (optional)
2. Increment `CURRENT_NVS_SCHEMA_VERSION` to 2
3. Add migration logic in `initialize()`:
   ```cpp
   if (schemaVersion == 0) {
       // Migration from v0 → v1
       clearAll();  // Safe: data already in Firebase
   }
   if (schemaVersion == 1) {
       // Migration from v1 → v2
       // ... implement if needed
   }
   ```

## Testing Checklist

- [ ] Verify old device boots successfully with new firmware
- [ ] Check that NVS version is updated correctly
- [ ] Confirm offline buffer is empty after migration
- [ ] Test that new sensor data is buffered correctly
- [ ] Verify Firebase upload resumes normally

## See Also

- `offline_data_buffer.h` - NVS buffer interface
- `offline_data_buffer.cpp` - NVS buffer implementation
- `mesh_utils.h` - sensorData struct definition
