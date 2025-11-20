# RS485 Soil Sensor Testing Environment

Thư mục này chứa các test files cho RS485 Soil Sensor component.

## Cấu trúc Test

```
test/
├── test_soil_sensor_main.cpp    # Main test entry point (Arduino setup/loop)
├── test_soil_sensor.cpp          # Test functions implementation
└── README.md                     # Tài liệu này
```

## Build & Run Tests

### 1. Build Test Environment

```bash
# Build môi trường test
pio run -e esp32-sensor-test

# Build và upload
pio run -e esp32-sensor-test --target upload

# Monitor serial output
pio device monitor -e esp32-sensor-test
```

### 2. Hoặc sử dụng PlatformIO Test Framework

```bash
# Chạy tất cả tests
pio test -e esp32-sensor-test

# Chạy với verbose output
pio test -e esp32-sensor-test -v
```

## Test Modes

Có 3 chế độ test (chỉnh sửa trong `test_soil_sensor_main.cpp`):

### Mode 1: Single Read (Mặc định tắt)
```cpp
#define TEST_MODE_SINGLE
```
- Đọc sensor 1 lần
- Hiển thị tất cả 7 thông số
- Dừng lại (cần reset để chạy lại)

### Mode 2: Continuous Reads (Mặc định BẬT)
```cpp
#define TEST_MODE_CONTINUOUS
```
- Đọc sensor liên tục
- Cấu hình được số lần đọc và khoảng thời gian
- Chạy cho đến khi reset

### Mode 3: Diagnostics Only
```cpp
#define TEST_MODE_DIAGNOSTIC
```
- Chỉ hiển thị thông tin chẩn đoán
- Không đọc sensor
- Dùng để kiểm tra kết nối

## Cấu hình Test

Chỉnh sửa trong `test_soil_sensor_main.cpp`:

```cpp
// Số lần đọc mỗi chu kỳ
#define CONTINUOUS_NUM_READS      10

// Thời gian giữa các lần đọc (giây)
#define CONTINUOUS_INTERVAL_SEC   10

// Thời gian chờ giữa các chu kỳ (ms)
#define CONTINUOUS_CYCLE_DELAY_MS 5000
```

## Expected Output

### Single Read Test:
```
╔════════════════════════════════════════════════════════════╗
║         RS485 SOIL SENSOR TEST ENVIRONMENT                 ║
╚════════════════════════════════════════════════════════════╝

SENSOR-TEST: ========== SENSOR TEST SETUP ==========
SENSOR-TEST: Initializing RS485 Soil Sensor...
SENSOR-TEST: ✅ Sensor initialized successfully
SENSOR-TEST: Sensor Status: Connected
SENSOR-TEST: ========================================

TEST MODE: Single Read
═══════════════════════════════════════════════════════════

SENSOR-TEST: ====== SENSOR TEST: Single Read ======
SENSOR-TEST: Reading sensor (may take up to 125ms)...
SENSOR-TEST: ✅ Read successful (took 127ms)
SENSOR-TEST: Status: OK
SENSOR-TEST: Moisture: 45.3% (Capacity: 512)
SENSOR-TEST: Temperature: 28.5°C (Raw: 285)
SENSOR-TEST: pH: 7.20 (Raw: 72)
SENSOR-TEST: EC: 1500.0 µS/cm (Raw: 1500)
SENSOR-TEST: Nitrogen: 120.0 mg/kg (Raw: 120)
SENSOR-TEST: Phosphorus: 80.0 mg/kg (Raw: 80)
SENSOR-TEST: Potassium: 150.0 mg/kg (Raw: 150)
SENSOR-TEST: Timestamp: 123456 ms
SENSOR-TEST: ===================================

Test complete. System will halt.
Press RESET button to run again.
```

### Continuous Test:
```
TEST MODE: Continuous Reads
  Number of reads per cycle: 10
  Interval between reads: 10 seconds
  Delay between cycles: 5000 ms
═══════════════════════════════════════════════════════════

Starting continuous test loop...
Press RESET button to stop.

SENSOR-TEST: ====== SENSOR TEST: Continuous (10 readings) ======
SENSOR-TEST: Read #1 (t= 0s) | Moisture:  45.3% | Temp:  28.5°C | pH:  7.20 | ✅ OK
SENSOR-TEST: Read #2 (t=10s) | Moisture:  45.2% | Temp:  28.4°C | pH:  7.20 | ✅ OK
SENSOR-TEST: Read #3 (t=20s) | Moisture:  45.3% | Temp:  28.5°C | pH:  7.20 | ✅ OK
...
SENSOR-TEST: Stats: 10 OK, 0 Failed (100.0% success rate)
SENSOR-TEST: 🎉 TEST PASSED - All reads successful!

Waiting 5000 ms before next cycle...
```

## Hardware Requirements

- **Board:** ESP32-S3 Gen4 R8N16
- **RS485 Adapter:** Connected to:
  - TX: GPIO 21
  - RX: GPIO 20
  - DE: GPIO 42
- **Soil Sensor:** Modbus RTU (Address: 0x4B2, Baud: 9600)
- **Serial Port:** USB (115200 baud)

## Test Functions

Test functions được định nghĩa trong `test_soil_sensor.cpp`:

```cpp
void sensor_test_setup();                    // Initialize sensor
void sensor_test_read_single();              // Single read test
void sensor_test_continuous(uint8_t n,       // Continuous test
                           uint8_t interval);
void sensor_test_diagnostics();              // Show diagnostics
bool sensor_test_is_ready();                 // Check if ready
```

## Troubleshooting

### Lỗi: Sensor not connected
- Kiểm tra kết nối RS485 (TX, RX, DE pins)
- Kiểm tra nguồn cho sensor (thường 12V)
- Verify Modbus address (0x4B2) và baud rate (9600)

### Lỗi: Compilation error
- Kiểm tra platformio.ini có môi trường `esp32-sensor-test`
- Verify include paths trong build_flags
- Chạy `pio run -e esp32-sensor-test --target clean`

### Lỗi: Upload failed
- Kiểm tra COM port
- Nhấn BOOT button trên board khi upload
- Verify board settings trong platformio.ini

## PlatformIO Environment

Môi trường test được định nghĩa trong `platformio.ini`:

```ini
[env:esp32-sensor-test]
platform = espressif32
framework = arduino
board = 4d_systems_esp32s3_gen4_r8n16
monitor_speed = 115200
lib_deps = 
	jgromes/RadioLib@^6.6.0
	bblanchon/ArduinoJson@^7.0.4
build_flags = 
	-D CORE_DEBUG_LEVEL=5
	-D CONFIG_LOG_COLORS=1
	-D SENSOR_TEST_BUILD
	-I src/components/rs485_soil_sensor/include
build_src_filter = 
	+<components/rs485_soil_sensor/>
	-<components/lora_mesh_manager/>
	-<application/>
test_build_src = yes
```

## CI/CD Integration

Để tích hợp vào CI/CD pipeline:

```bash
# Script để chạy tests tự động
pio test -e esp32-sensor-test --without-uploading
```

## Notes

- Test environment này chỉ biên dịch RS485 sensor component
- Không bao gồm LoRa, BLE, hay Gateway code
- Phù hợp để test nhanh sensor mà không cần full system
- Có thể chạy trên hardware hoặc trong CI/CD (without-uploading mode)
