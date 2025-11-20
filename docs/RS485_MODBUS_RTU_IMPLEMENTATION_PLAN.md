# Kế Hoạch Triển Khai Cảm Biến Đất RS485 Modbus RTU

**Ngày tạo:** 2025-11-20  
**Mục tiêu:** Thay thế dữ liệu giả (fake data simulation) bằng dữ liệu thực từ cảm biến đất qua RS485 Modbus RTU

---

## 1. PHÂN TÍCH HIỆN TRẠNG

### 1.1 Cấu Trúc Dữ Liệu Hiện Tại
```cpp
struct sensorData {
    // Common fields
    DeviceType deviceType;
    uint32_t counter;
    float battery;
    uint32_t timestamp;
    uint16_t nodeId;
    
    // Soil sensor data (7 tham số)
    struct {
        float soilMoisture;      // Độ ẩm đất (%)
        float soilTemperature;   // Nhiệt độ đất (°C)
        float pH;                // pH đất [0-14]
        float ec;                // Điện dẫn suất (mS/cm)
        float nitrogen;          // Nito (mg/kg)
        float phosphorus;        // Lân (mg/kg)
        float potassium;         // Kali (mg/kg)
    } soil;
};
```

### 1.2 Hiện Tại Dữ Liệu Giả Ở Đâu?
- **File:** `node_app.cpp`
- **Function:** `sensorData simulateSensorData()`
- **Gọi từ:** Line 352 trong `NodeApp::loop()`
- **Config:** `#define ENABLE_SENSOR_SIMULATION true` trong `node_config.h`

### 1.3 Các Node Khác Cần Hỗ Trợ?
- ✅ **Node App** (app_node) - CHÍNH
- ⚠️ **Gateway** (app_gateway) - Có thể hỗ trợ sau (nếu cần)
- ⚠️ **Bridge** (app_bridge) - Có thể hỗ trợ sau (nếu cần)

---

## 2. KIẾN TRÚC TRIỂN KHAI

### 2.1 Phân Tầng (Layered Architecture)

```
┌─────────────────────────────────────┐
│  Node App Layer (node_app.cpp)      │  Gọi readSoilSensorData()
└────────────────┬────────────────────┘
                 │
┌────────────────▼────────────────────┐
│  Soil Sensor Service                │  Quản lý logic cảm biến
│  (new: soil_sensor_service.h/cpp)   │
└────────────────┬────────────────────┘
                 │
┌────────────────▼────────────────────┐
│  Modbus RTU Driver                  │  Giao tiếp RS485
│  (new: modbus_rtu_driver.h/cpp)     │
└────────────────┬────────────────────┘
                 │
┌────────────────▼────────────────────┐
│  ESP32 UART Hardware                │  Serial port
└─────────────────────────────────────┘
```

### 2.2 Các Component Cần Tạo

| File | Loại | Mục Đích |
|------|------|---------|
| `soil_sensor_service.h` | Header | Service quản lý cảm biến |
| `soil_sensor_service.cpp` | Source | Implement service |
| `modbus_rtu_driver.h` | Header | Driver Modbus RTU |
| `modbus_rtu_driver.cpp` | Source | Implement driver |
| `rs485_config.h` | Config | Pin, baud rate định nghĩa |

### 2.3 Luồng Dữ Liệu

```
[Node App Loop]
    ↓
[Check: Read Sensor?]
    ↓
[SoilSensorService::readData()]
    ├─→ Check: Simulation Mode?
    │   ├─ YES: Return fake data
    │   ├─ NO: Call readRealData()
    │
    ↓
[ModbusRTUDriver::readRegister()]
    ├─→ Build Modbus Request
    ├─→ Send via UART (RS485)
    ├─→ Wait Response (timeout: 500ms)
    ├─→ Parse Response
    ├─→ Return 7 parameters
    │
    ↓
[sensorData filled with real values]
    ↓
[Send to Gateway via LoRa]
```

---

## 3. CHI TIẾT TRIỂN KHAI

### 3.1 RS485 Pin Configuration

**File:** `rs485_config.h` (NEW)

```cpp
// UART Hardware
#define RS485_UART_NUM          UART_NUM_2      // UART2 (tách biệt với USB log)
#define RS485_RX_PIN            16              // GPIO 16 (RX)
#define RS485_TX_PIN            17              // GPIO 17 (TX)
#define RS485_RTS_PIN           4               // GPIO 4 (Request-to-Send, control RS485 direction)

// Modbus RTU Configuration
#define MODBUS_BAUD_RATE        9600            // Cảm biến đất thường 9600 bps
#define MODBUS_SLAVE_ADDRESS    0x01            // Địa chỉ cảm biến (thường 1)
#define MODBUS_READ_TIMEOUT_MS  500             // Timeout chờ response

// Modbus Function Code
#define MODBUS_READ_INPUT_REG   0x04            // Function 04: Read Input Registers

// Register Addresses (phụ thuộc vào cảm biến, ví dụ)
#define REG_MOISTURE            0x0000          // Độ ẩm
#define REG_TEMPERATURE         0x0001          // Nhiệt độ
#define REG_pH                  0x0002          // pH
#define REG_EC                  0x0003          // EC
#define REG_NITROGEN            0x0004          // N
#define REG_PHOSPHORUS          0x0005          // P
#define REG_POTASSIUM           0x0006          // K
```

> **⚠️ LƯU Ý:** Các địa chỉ register cần confirm từ datasheet cảm biến thực tế

### 3.2 Modbus RTU Driver

**File:** `modbus_rtu_driver.h` (NEW)

```cpp
#ifndef _MODBUS_RTU_DRIVER_H
#define _MODBUS_RTU_DRIVER_H

#include <Arduino.h>
#include "rs485_config.h"

class ModbusRTUDriver {
public:
    // Initialize UART and RS485
    static bool initialize();
    
    // Read 16-bit register from device
    // Returns: value if success, -1 if timeout/error
    static int16_t readInputRegister(uint16_t registerAddr);
    
    // Read multiple registers at once (more efficient)
    // Returns: true if success
    static bool readInputRegisters(
        uint16_t startAddr,
        uint16_t count,
        float* outputValues
    );
    
    // Get last error message
    static String getLastError();
    
private:
    static UART_HandleTypeDef UartHandle;
    static String lastError;
    
    // Internal helpers
    static bool sendRequest(uint8_t* request, uint16_t length);
    static bool readResponse(uint8_t* response, uint16_t* length);
    static uint16_t calculateCRC(const uint8_t* data, uint16_t length);
    static void setRTSHigh();   // Enable transmitter
    static void setRTSLow();    // Enable receiver
};

#endif
```

### 3.3 Soil Sensor Service

**File:** `soil_sensor_service.h` (NEW)

```cpp
#ifndef _SOIL_SENSOR_SERVICE_H
#define _SOIL_SENSOR_SERVICE_H

#include <Arduino.h>
#include "modbus_rtu_driver.h"
#include "../common/mesh_utils.h"

class SoilSensorService {
public:
    // Initialize sensor communication
    static bool initialize();
    
    // Read all 7 soil parameters
    // Returns: struct với 7 giá trị sensor
    static sensorData readData();
    
    // Get sensor status
    static bool isConnected();
    
    // Set simulation mode (for testing)
    static void setSimulationMode(bool enabled);
    
private:
    static bool simulationMode;
    static bool isInitialized;
    
    // Real sensor read
    static sensorData readRealSensorData();
    
    // Simulated data (fallback)
    static sensorData readFakeSensorData();
};

#endif
```

### 3.4 Integration Point - Thay Đổi node_app.cpp

**Hiện tại (Line 352):**
```cpp
sensorData s = simulateSensorData();
```

**Sau khi triển khai:**
```cpp
sensorData s = SoilSensorService::readData();  // Automatic: real or fake
```

---

## 4. CHI TIẾT MODBUS RTU PROTOCOL

### 4.1 Modbus Request Frame

```
┌───────────────────────────────────────────┐
│ Modbus RTU Read Input Registers Request   │
├─────────────┬────────────────────────────┤
│ Byte 0      │ Slave Address (0x01)       │
│ Byte 1      │ Function Code (0x04)       │
│ Byte 2-3    │ Starting Address (big-endian) │
│ Byte 4-5    │ Quantity of Registers      │
│ Byte 6-7    │ CRC-16 (low byte, high byte) │
└─────────────┴────────────────────────────┘

Ví dụ: Read 1 register từ address 0x0000
01 04 00 00 00 01 30 0A
│  │  │  │  │  │  │  └─ CRC high
│  │  │  │  │  │  └──── CRC low
│  │  │  │  │  └─────── Qty registers
│  │  │  └──────────── Start addr
│  │  └────────────── Function code
│  └─────────────── Slave ID
```

### 4.2 Modbus Response Frame

```
┌────────────────────────────────────┐
│ Modbus RTU Response                │
├──────────┬──────────────────────┤
│ Byte 0   │ Slave Address        │
│ Byte 1   │ Function Code        │
│ Byte 2   │ Byte Count (= count*2) │
│ Byte 3.. │ Register Data        │
│ Byte -2:-1 │ CRC-16             │
└──────────┴──────────────────────┘

Ví dụ: Response 1 register (value 0x1234)
01 04 02 12 34 4C 48
```

### 4.3 Trường Hợp Lỗi

- **Timeout (> 500ms):** Không nhận response
- **CRC Error:** Dữ liệu lỗi
- **Exception Response:** Cảm biến trả về lỗi (Function Code | 0x80)

---

## 5. CÁC THAM SỐ MODBUS (CẦN XÁC NHẬN)

Đây là **ví dụ** - cần confirm từ datasheet cảm biến thực:

| Tham số | Register | Format | Đơn vị | Range |
|---------|----------|--------|--------|-------|
| Độ ẩm | 0x0000 | Float | % | 0-100 |
| Nhiệt độ | 0x0001 | Float | °C | -10 to 60 |
| pH | 0x0002 | Float | - | 0-14 |
| EC | 0x0003 | Float | mS/cm | 0-10 |
| Nito | 0x0004 | Float | mg/kg | 0-300 |
| Lân | 0x0005 | Float | mg/kg | 0-200 |
| Kali | 0x0006 | Float | mg/kg | 0-300 |

> ⚠️ **CẢNH BÁORAOCUMENT CẦN KHAI BÁOCÁC CHỈ SỐ THỰC TẾ**

---

## 6. LỘ TRÌNH TRIỂN KHAI

### Phase 1: Chuẩn Bị (1-2 ngày)
- [ ] Xác nhận datasheet cảm biến
- [ ] Xác nhận các register address
- [ ] Xác nhận RS485 pin trên board
- [ ] Kiểm tra UART2 availability

### Phase 2: Implement Driver (2-3 ngày)
- [ ] Tạo `rs485_config.h`
- [ ] Tạo `modbus_rtu_driver.h/cpp`
  - [ ] Implement UART init
  - [ ] Implement CRC-16
  - [ ] Implement request/response
  - [ ] Unit test với mock device

### Phase 3: Implement Service (1-2 ngày)
- [ ] Tạo `soil_sensor_service.h/cpp`
- [ ] Implement `readRealSensorData()`
- [ ] Implement fallback simulation
- [ ] Add logging

### Phase 4: Integration (1 ngày)
- [ ] Update `node_app.cpp`
- [ ] Remove `simulateSensorData()` call
- [ ] Add initialization
- [ ] Compile & verify no errors

### Phase 5: Testing (2-3 ngày)
- [ ] Unit test Modbus driver
- [ ] Integration test với cảm biến thực
- [ ] Test fallback simulation
- [ ] Test end-to-end (Node → Gateway)

---

## 7. CONFIGURATION CHANGES

### 7.1 node_config.h - Thay Đổi

**Hiện tại:**
```cpp
#define ENABLE_SENSOR_SIMULATION true
```

**Sau khi triển khai:**
```cpp
// Sensor Mode Configuration
#define SENSOR_MODE_REAL        0
#define SENSOR_MODE_SIMULATION  1
#define SENSOR_MODE             SENSOR_MODE_REAL  // Switch between modes
```

### 7.2 CMakeLists.txt - Thêm File Source

```cmake
# Soil Sensor Components
list(APPEND SOURCES
    "${COMPONENT_DIR}/../../application/app_node/soil_sensor_service.cpp"
    "${COMPONENT_DIR}/../../application/app_node/modbus_rtu_driver.cpp"
)
```

---

## 8. ERROR HANDLING & FALLBACK

### 8.1 Khi Cảm Biến Lỗi

```cpp
if (!SoilSensorService::isConnected()) {
    // Fallback: Dùng dữ liệu giả
    ESP_LOGW(TAG, "⚠️ Sensor disconnected, using simulated data");
    s = SoilSensorService::readData();  // Tự động fallback
}
```

### 8.2 Retry Logic

- Retry 3 lần nếu timeout
- Exponential backoff: 100ms → 200ms → 400ms
- Sau 3 lần lỗi: Fallback simulation

### 8.3 Logging

```cpp
// Debug: In ra giá trị đọc được
ESP_LOGI(TAG, "🌱 Soil Data (REAL):");
ESP_LOGI(TAG, "   Moisture: %.1f%%", soilData.soil.soilMoisture);
ESP_LOGI(TAG, "   Temp: %.1f°C", soilData.soil.soilTemperature);
// ... etc
```

---

## 9. TESTING PLAN

### 9.1 Unit Tests

**Test 1: CRC Calculation**
- Input: Known data
- Expected: Known CRC value

**Test 2: Modbus Request Build**
- Input: Register address, count
- Expected: Correct hex frame

**Test 3: Modbus Response Parse**
- Input: Valid response frame
- Expected: Correct float values extracted

### 9.2 Integration Tests

**Test 1: Read Single Register**
- Send: Read Moisture (0x0000)
- Verify: Receive valid float

**Test 2: Read All 7 Registers**
- Send: Read 0x0000-0x0006
- Verify: All 7 values extracted correctly

**Test 3: Timeout Handling**
- Unplug sensor
- Verify: Timeout after 500ms
- Verify: Fallback to simulation

**Test 4: CRC Error Handling**
- Inject CRC error
- Verify: Retry logic works

---

## 10. DOCUMENTS CẦN CHUẨN BỊ

1. **Datasheet Cảm Biến**
   - Register mapping
   - Baud rate
   - Modbus function support
   - Error codes

2. **RS485 Transceiver Datasheet**
   - Pin configuration
   - Timing requirements
   - Termination resistors

3. **Hardware Schematic**
   - UART2 pins on ESP32
   - RS485 connection diagram

---

## 11. POTENTIAL ISSUES & SOLUTIONS

| Issue | Cause | Giải Pháp |
|-------|-------|----------|
| Cảm biến không phản hồi | Baud rate sai | Confirm datasheet |
| CRC error liên tục | Clock drift | Add CRC retry |
| Data fluctuation | Noise on RS485 | Add filtering/averaging |
| Timeout thường xuyên | Cảm biến busy | Increase timeout / add retry |

---

## 12. TIMELINE TỔNG CỘNG

**Ước tính:** 1-2 tuần (tùy testing)

| Week | Tiến độ |
|------|---------|
| W1 D1-2 | Phase 1: Chuẩn bị |
| W1 D3-5 | Phase 2: Driver |
| W2 D1-2 | Phase 3: Service |
| W2 D3 | Phase 4: Integration |
| W2 D4-5 | Phase 5: Testing |

---

## 13. NEXT STEPS

1. ✅ **Confirm Sensor Datasheet** - ĐỦ CAO TIÊN
2. ✅ **Get Hardware Schematic** - CẦN NGAY
3. ✅ **Review Pin Assignment** - CẦN NGAY
4. [ ] Start Phase 1 - Chuẩn Bị

---

**Lưu ý:** Kế hoạch này sẵn sàng cho việc mở rộng sang Gateway/Bridge sau này.
