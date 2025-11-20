# Sensor Task Quick Reference

## One-Minute Overview

Soil sensor now reads **every 10 minutes on core 0** without blocking main loop.

**Before (❌):** Main loop blocked ~125ms during sensor read  
**After (✅):** Main loop never blocks - reads from queue when available

---

## Basic Usage

### Initialize (in setup)
```cpp
SensorTaskManager::initialize();  // Start task on core 0
```

### Read in Loop (non-blocking)
```cpp
sensorData latest;
if (SensorTaskManager::getData(latest, 0)) {  // 0ms = non-blocking
    ESP_LOGI(TAG, "Moisture: %.1f%%", latest.data.soil.soilMoisture);
}
// If false = no new data yet, continue main loop
```

### Shutdown (if needed)
```cpp
SensorTaskManager::shutdown();  // Stop task, free memory
```

---

## Testing Sensor

### Build & Run Test Environment
```bash
# Build test
pio run -e esp32-sensor-test

# Upload and monitor
pio run -e esp32-sensor-test --target upload
pio device monitor -e esp32-sensor-test

# Or use test framework
pio test -e esp32-sensor-test
```

### Test Modes (edit test/test_soil_sensor_main.cpp)
```cpp
// Choose one:
#define TEST_MODE_SINGLE          // One read, then stop
#define TEST_MODE_CONTINUOUS      // Continuous reads
#define TEST_MODE_DIAGNOSTIC      // Diagnostics only
```

### Manual Test Functions (if integrated in main app)
```cpp
sensor_test_read_single();        // Take one reading, show all 7 parameters
sensor_test_continuous(5, 10);    // 5 readings, 10s apart (takes ~50s)
```

### Check Status
```cpp
if (SensorTaskManager::isRunning()) {
    uint32_t lastRead = SensorTaskManager::getLastReadTime();
    size_t queueDepth = SensorTaskManager::getQueueDepth();
    uint32_t success = SensorTaskManager::getSuccessfulReadCount();
    uint32_t failed = SensorTaskManager::getFailedReadCount();
}
```

---

## Configuration (Edit Before Compile)

**File:** `src/components/rs485_soil_sensor/src/sensor_task.cpp` (Lines 13-17)

```cpp
// Change read interval (default: 10 minutes = 600,000 ms)
#define SENSOR_READ_INTERVAL_MS (10 * 60 * 1000)

// Change queue size (default: 2, can hold up to 2 readings)
#define SENSOR_QUEUE_SIZE 2

// Change stack size if stack overflow (default: 4KB)
#define SENSOR_TASK_STACK_SIZE (4096)
```

---

## Common Scenarios

### Scenario 1: "How often does the sensor read?"
Every 10 minutes (configurable). Check with:
```cpp
uint32_t lastTime = SensorTaskManager::getLastReadTime();
uint32_t ageMs = millis() - lastTime;
if (ageMs > 11 * 60 * 1000) {
    ESP_LOGW(TAG, "Sensor data too old!");
}
```

### Scenario 2: "How do I get the latest sensor data?"
```cpp
sensorData reading;
if (SensorTaskManager::getData(reading, 0)) {  // Non-blocking
    // Process new sensor data
    float moisture = reading.data.soil.soilMoisture;
} else {
    // No new data available, try again later
}
```

### Scenario 3: "Why is sensor data sometimes missing?"
Main loop runs ~10 times per second, sensor reads every 10 minutes. Most loop iterations get no new data. That's normal.

```cpp
// Instead of expecting data every iteration:
if (SensorTaskManager::getData(reading, 0)) {  // Non-blocking
    // Process when available (rare)
} else {
    // Most of the time - use previous data or skip
}
```

### Scenario 4: "How do I wait for sensor data?"
```cpp
// Non-blocking (usual):
sensorData reading;
SensorTaskManager::getData(reading, 0);  // Returns immediately

// Blocking with timeout (rare):
sensorData reading;
if (SensorTaskManager::getData(reading, 5000)) {  // Wait up to 5 seconds
    // Got data
} else {
    // Timed out - sensor may be offline
}
```

### Scenario 5: "Can I change the read interval?"
Yes, edit `sensor_task.cpp` line 20:
```cpp
// For 5-minute interval:
#define SENSOR_READ_INTERVAL_MS (5 * 60 * 1000)

// For 1-minute interval (testing):
#define SENSOR_READ_INTERVAL_MS (60 * 1000)
```
Then recompile.

---

## Data Structure

Each sensor reading contains:
```cpp
struct sensorData {
    bool error;                           // Read failed?
    uint32_t timestamp;                   // When read (ms since boot)
    DeviceType deviceType;                // SOIL_SENSOR or ENV_SENSOR
    uint32_t counter;                     // Packet sequence
    uint16_t nodeId;                      // Node address
    float battery;                        // Battery voltage
    
    union {
        // SOIL_SENSOR (7 parameters):
        struct {
            float soilMoisture;           // 0-100%
            float soilTemperature;        // °C
            float pH;                     // pH units
            float conductivity;           // EC µS/cm
            float nitrogen;               // mg/kg
            float phosphorus;             // mg/kg
            float potassium;              // mg/kg
            uint16_t capacity;            // Soil capacity
        } soil;
        
        // ENV_SENSOR (2 parameters):
        struct {
            float temperature;            // °C
            float humidity;               // %
        } environment;
    } data;
};
```

---

## Compilation

**Full build:**
```bash
pio run -e esp32-node
```

**Clean build:**
```bash
pio run -e esp32-node --target clean
pio run -e esp32-node
```

**Build output should show:**
```
src/components/rs485_soil_sensor/src/sensor_task.cpp  ← Compiling
... (no errors)
Built target esp32-node
```

---

## Error Codes & Troubleshooting

| Error | Cause | Fix |
|-------|-------|-----|
| `sensor_task.h not found` | Wrong include path | Check `node_app.cpp` line 5 |
| Sensor data always stale | Queue not initialized | Call `SensorTaskManager::initialize()` first |
| Queue depth >= 2 | Main loop too slow | Not usually an issue |
| Failed read count > 0 | RS485 connection issue | Check wiring: TX=21, RX=20, DE=42 |
| Task not running | Initialization failed | Check FreeRTOS memory |

---

## File Locations

- **Task Interface:** `src/components/rs485_soil_sensor/include/sensor_task.h`
- **Task Implementation:** `src/components/rs485_soil_sensor/src/sensor_task.cpp`
- **Test Functions:** `src/application/app_node/sensor_test.cpp`
- **Node Integration:** `src/application/app_node/node_app.cpp` (lines 5, 280, 370)
- **Component Config:** `src/components/rs485_soil_sensor/CMakeLists.txt`

---

## Key Advantages

✅ Main loop never blocked by sensor read  
✅ Predictable 10-minute sensor intervals  
✅ Dedicated core 0 (WiFi/BLE on core 1 unaffected)  
✅ Queue-based thread-safe communication  
✅ Easy testing with `sensor_test_*` functions  
✅ Full error handling and diagnostics  

---

## Support Functions

```cpp
// Status checks
bool SensorTaskManager::isRunning();
uint32_t SensorTaskManager::getLastReadTime();
size_t SensorTaskManager::getQueueDepth();
uint32_t SensorTaskManager::getSuccessfulReadCount();
uint32_t SensorTaskManager::getFailedReadCount();

// Testing
void sensor_test_setup();
void sensor_test_read_single();
void sensor_test_continuous(uint8_t numReads = 5, uint8_t intervalSeconds = 10);
void sensor_test_diagnostics();
bool sensor_test_is_ready();
```

---

**Version:** 1.0  
**Status:** Production Ready  
**Compilation:** ✅ 0 Errors
