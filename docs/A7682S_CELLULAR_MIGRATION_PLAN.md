# Kế Hoạch Thay Thế WiFi Bằng Module SIM A7682S

**Ngày**: 10 Tháng 11, 2025  
**Dự Án**: LoRa Mesh Gateway  
**Mục Tiêu**: Thay thế hoàn toàn WiFi bằng kết nối cellular qua module A7682S  
**Trạng Thái**: 📋 Planning Phase

---

## 📋 Tóm Tắt Executive

Gateway hiện đang sử dụng **WiFi để kết nối Firebase**. Bạn muốn **THAY THẾ hoàn toàn WiFi** bằng module **SIM A7682S** (4G LTE Cat-1) để:
- Hoạt động ở vùng không có WiFi
- Kết nối internet qua SIM 4G
- Upload dữ liệu lên Firebase qua cellular network

**Đánh Giá**: ✅ **KHẢ THI** - Module A7682S rất phù hợp, hỗ trợ đầy đủ AT commands và TLS/SSL.

---

## 🔧 1. Thông Số Module A7682S

### Đặc Điểm Kỹ Thuật

| Thông Số | Giá Trị |
|----------|---------|
| **Model** | SIMCOM A7682S-H (4G LTE Cat-1) |
| **Frequency Bands** | LTE-FDD: B1/B3/B5/B7/B8/B20/B28 |
| **Tốc Độ Tối Đa** | DL: 10 Mbps, UL: 5 Mbps |
| **Interface** | UART (AT Commands) |
| **Baud Rate** | 115200 bps (default) |
| **Điện Áp** | 3.3V - 4.2V (tương thích ESP32) |
| **Dòng Điện** | Idle: ~20mA, TX: 400-600mA (peak) |
| **SSL/TLS** | Hỗ trợ TLS 1.2 (built-in) ✅ |
| **TCP/IP Stack** | Built-in (PPP mode) |
| **GNSS/GPS** | Tích hợp (optional) |
| **AT Command Set** | SIMCOM AT V1.0 |

### Ưu Điểm So Với WiFi

| Yếu Tố | WiFi | A7682S Cellular |
|--------|------|-----------------|
| **Phạm Vi** | ~50m indoor | Toàn quốc (có sóng) ✅ |
| **Ổn Định** | Phụ thuộc router | Độc lập ✅ |
| **Độ Trễ** | 10-50ms | 50-200ms |
| **Cấu Hình** | Cần SSID/password | Chỉ cần SIM card ✅ |
| **Chi Phí** | Phụ thuộc WiFi router | 50-100k/tháng (data) |
| **Bảo Mật** | WPA2 | Encrypted SIM + TLS ✅ |

---

## 🛠️ 2. Phân Tích Phần Cứng

### 2.1. Pin Configuration (ESP32-S3)

**Hiện Tại Đang Sử Dụng**:
```
SPI (LoRa):
  GPIO 18: SCK
  GPIO 19: MOSI  
  GPIO 16: MISO
  GPIO 5:  CS
  GPIO 4:  RST
  GPIO 6:  IRQ

WiFi: Tích hợp ESP32 (không cần GPIO)
```

**Chân Khả Dụng Cho A7682S**:
```
UART1 (Recommended):
  GPIO 17: TXD1 → A7682S RXD
  GPIO 18: RXD1 → A7682S TXD
  
⚠️ XUNG ĐỘT: GPIO 18 đang dùng cho SPI SCK!

UART2 (Alternative - FREE):
  GPIO 43: TXD2 → A7682S RXD ✅
  GPIO 44: RXD2 → A7682S TXD ✅

Control Pins:
  GPIO 9:  A7682S PWRKEY (power on/off)
  GPIO 10: A7682S RESET  
  GPIO 11: A7682S STATUS (read power status)
```

**✅ KHUYẾN NGHỊ: Dùng UART2 (GPIO 43/44) - Không xung đột**

### 2.2. Sơ Đồ Kết Nối

```
ESP32-S3                        A7682S Module
┌─────────────┐                ┌──────────────┐
│ GPIO 43 (TX)├───────────────>│ RXD          │
│ GPIO 44 (RX)│<───────────────┤ TXD          │
│ GPIO 9      ├───────────────>│ PWRKEY       │
│ GPIO 10     ├───────────────>│ RESET#       │
│ GPIO 11     │<───────────────┤ STATUS       │
│ 3.3V        ├───────────────>│ VBAT (3.3-4.2V)
│ GND         ├───────────────>│ GND          │
└─────────────┘                └──────────────┘

SIM Card: Cắm vào slot của A7682S
Antenna: Kết nối ăng-ten 4G vào cổng ANT
```

### 2.3. Cấp Nguồn

**Yêu Cầu**:
- A7682S peak current: **600mA** (khi TX)
- ESP32-S3 + LoRa: ~300mA
- **Tổng**: ~900mA peak

**Giải Pháp**:
```
Option 1: USB 5V/2A adapter (khuyến nghị)
  - Dùng DC-DC buck converter 5V → 3.8V cho A7682S
  - Đảm bảo đủ dòng cho peak current

Option 2: LiPo 3.7V/2000mAh battery
  - Kết nối trực tiếp VBAT (3.3-4.2V range)
  - Cần charging circuit
```

---

## 🏗️ 3. Kiến Trúc Phần Mềm

### 3.1. Kiến Trúc Hiện Tại (WiFi)

```
┌─────────────────────────────────┐
│      Gateway Application        │
│    (gateway_app.cpp/.h)         │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│   WiFiConnectionService         │  <- Cần thay thế
│   (wifi_connection_service.h)   │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│    Arduino WiFi Library         │  <- Cần loại bỏ
│    (#include <WiFi.h>)          │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│  ESP-IDF WiFi Stack (CPU0)      │
│  TCP/IP, lwIP, TLS              │
└─────────────────────────────────┘
             │
      Firebase Realtime DB
```

### 3.2. Kiến Trúc Mới (A7682S Cellular) - ĐỀ XUẤT

```
┌─────────────────────────────────┐
│      Gateway Application        │
│    (gateway_app.cpp/.h)         │  [KHÔNG THAY ĐỔI]
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│  CellularConnectionService      │  [MỚI - Thay WiFiConnectionService]
│  (cellular_connection_service.h)│
│  - AT command interface         │
│  - Connection management        │
│  - Signal monitoring            │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│    TinyGSM Library              │  [MỚI - Thư viện driver]
│    (#include <TinyGsmClient.h>) │
│    - A7682S AT commands         │
│    - TCP/IP over cellular       │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│   A7682S Module (UART2)         │  [PHẦN CỨNG]
│   - TCP/IP stack (built-in)     │
│   - TLS 1.2 encryption          │
│   - PPP protocol                │
└────────────┬────────────────────┘
             │
┌────────────▼────────────────────┐
│   Cellular Network (4G LTE)     │
│   - SIM card authentication     │
│   - Data plan                   │
└─────────────────────────────────┘
             │
      Firebase Realtime DB
```

### 3.3. Firebase Client - Cần Refactoring

**Vấn Đề**: Firebase library hiện tại hardcoded WiFi:
```cpp
// firebase_client.h - Line 1-10 (HIỆN TẠI)
#include <FirebaseESP32.h>    // <- Phụ thuộc WiFi
#include <WiFi.h>              // <- Chỉ WiFi

// Cần thay đổi thành:
#include <FirebaseESP32.h>
// KHÔNG include <WiFi.h>
// Dùng TinyGsmClient thay thế
```

**Giải Pháp**: Firebase ESP32 library hỗ trợ custom network client:
```cpp
// Sau khi tích hợp TinyGSM
TinyGsm modem(Serial2);         // UART2 cho A7682S
TinyGsmClient client(modem);    // TCP client qua cellular

// Firebase sẽ dùng client này thay vì WiFiClient
Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH, &client);
```

---

## 📝 4. Kế Hoạch Triển Khai Chi Tiết

### Phase 1: Chuẩn Bị Môi Trường (4-6 giờ)

#### Task 1.1: Cập Nhật platformio.ini
**Thời gian**: 30 phút  
**Mục tiêu**: Thêm TinyGSM library

```ini
[env:esp32-gateway]
lib_deps = 
    jgromes/RadioLib@^6.6.0
    bblanchon/ArduinoJson@^7.0.4
    mobizt/Firebase ESP32 Client@^4.4.17
    h2zero/NimBLE-Arduino@^1.4.2
    vshymanskyy/TinyGSM@^0.11.7        ; MỚI - A7682S driver
    
build_flags = 
    ... (giữ nguyên)
    -D TINY_GSM_MODEM_SIM7600          ; A7682S tương thích SIM7600
    -D TINY_GSM_RX_BUFFER=1024         ; Buffer cho AT response
```

#### Task 1.2: Tạo File Config Cho A7682S
**Thời gian**: 30 phút  
**File**: `src/application/app_gateway/cellular_config.h`

```cpp
#ifndef _CELLULAR_CONFIG_H
#define _CELLULAR_CONFIG_H

// A7682S UART Configuration (ESP32-S3)
#define CELLULAR_UART_NUM       2           // UART2
#define CELLULAR_TX_PIN         43          // GPIO 43 → A7682S RXD
#define CELLULAR_RX_PIN         44          // GPIO 44 → A7682S TXD
#define CELLULAR_BAUD_RATE      115200      // Default A7682S baud

// A7682S Control Pins
#define CELLULAR_PWRKEY_PIN     9           // Power on/off
#define CELLULAR_RESET_PIN      10          // Hardware reset
#define CELLULAR_STATUS_PIN     11          // Power status readback

// SIM Card & APN Configuration
#define SIM_APN                 "v-internet"    // Vinaphone
// #define SIM_APN              "internet"      // Viettel
// #define SIM_APN              "m-wap"         // Mobifone
#define SIM_USER                ""              // Thường để trống
#define SIM_PASSWORD            ""              // Thường để trống
#define SIM_PIN                 ""              // SIM PIN (nếu có)

// Connection Settings
#define CELLULAR_CONNECT_TIMEOUT    30000   // 30s timeout
#define CELLULAR_RETRY_INTERVAL     10000   // 10s giữa các lần retry
#define CELLULAR_MAX_RETRIES        5       // Tối đa 5 lần thử

// Network Monitoring
#define SIGNAL_CHECK_INTERVAL       30000   // Check signal mỗi 30s
#define MIN_SIGNAL_QUALITY          10      // CSQ >= 10 (acceptable)

#endif // _CELLULAR_CONFIG_H
```

#### Task 1.3: Tạo CellularConnectionService Class
**Thời gian**: 3-4 giờ  
**File**: `src/components/lora_mesh_manager/include/cellular_connection_service.h`

```cpp
#ifndef _CELLULAR_CONNECTION_SERVICE_H
#define _CELLULAR_CONNECTION_SERVICE_H

#include <Arduino.h>
#include <TinyGsmClient.h>

// Forward declaration
class TinyGsm;
class TinyGsmClient;

// Connection status
enum CellularStatus {
    CELLULAR_DISCONNECTED,
    CELLULAR_CONNECTING,
    CELLULAR_CONNECTED,
    CELLULAR_RECONNECTING,
    CELLULAR_ERROR
};

// Event callbacks (tương tự WiFiConnectionService)
typedef std::function<void()> CellularEventCallback;

class CellularConnectionService {
public:
    CellularConnectionService(
        const char* apn, 
        const char* user = "", 
        const char* password = "",
        bool autoReconnect = true,
        uint32_t reconnectInterval = 10000
    );
    
    ~CellularConnectionService();
    
    // Lifecycle
    bool initialize();
    bool connect();
    void disconnect();
    void update();  // Gọi trong loop() - kiểm tra kết nối
    
    // Status
    bool isConnected();
    CellularStatus getStatus();
    int getSignalQuality();  // CSQ (0-31)
    String getOperatorName();
    String getIMEI();
    
    // Network info
    String getLocalIP();
    uint32_t getUptime();
    
    // Event callbacks
    void onConnected(CellularEventCallback callback);
    void onDisconnected(CellularEventCallback callback);
    void onReconnecting(CellularEventCallback callback);
    void onSignalLow(CellularEventCallback callback);
    
    // Network client for Firebase
    TinyGsmClient* getClient();  // Trả về client cho Firebase
    
private:
    // Modem control
    bool powerOn();
    bool powerOff();
    bool reset();
    bool waitForNetwork(uint32_t timeout_ms);
    
    // Connection management
    void handleReconnect();
    void checkSignalQuality();
    
    // Configuration
    const char* _apn;
    const char* _user;
    const char* _password;
    bool _autoReconnect;
    uint32_t _reconnectInterval;
    
    // State
    CellularStatus _status;
    uint32_t _lastReconnectAttempt;
    uint32_t _connectionStartTime;
    int _signalQuality;
    
    // Modem objects
    TinyGsm* _modem;
    TinyGsmClient* _client;
    HardwareSerial* _serial;
    
    // Callbacks
    CellularEventCallback _onConnected;
    CellularEventCallback _onDisconnected;
    CellularEventCallback _onReconnecting;
    CellularEventCallback _onSignalLow;
};

#endif // _CELLULAR_CONNECTION_SERVICE_H
```

#### Task 1.4: Implement CellularConnectionService
**Thời gian**: 4-6 giờ  
**File**: `src/components/lora_mesh_manager/src/cellular_connection_service.cpp`

```cpp
#include "cellular_connection_service.h"
#include "cellular_config.h"
#include <TinyGsmClient.h>

#define TINY_GSM_MODEM_SIM7600  // A7682S compatible

static const char* TAG = "CellularService";

CellularConnectionService::CellularConnectionService(
    const char* apn, const char* user, const char* password,
    bool autoReconnect, uint32_t reconnectInterval
) : _apn(apn), _user(user), _password(password),
    _autoReconnect(autoReconnect), _reconnectInterval(reconnectInterval),
    _status(CELLULAR_DISCONNECTED), _lastReconnectAttempt(0),
    _connectionStartTime(0), _signalQuality(0),
    _modem(nullptr), _client(nullptr), _serial(nullptr) {
}

CellularConnectionService::~CellularConnectionService() {
    disconnect();
    if (_client) delete _client;
    if (_modem) delete _modem;
    if (_serial) delete _serial;
}

bool CellularConnectionService::initialize() {
    ESP_LOGI(TAG, "Initializing A7682S cellular module...");
    
    // Setup control pins
    pinMode(CELLULAR_PWRKEY_PIN, OUTPUT);
    pinMode(CELLULAR_RESET_PIN, OUTPUT);
    pinMode(CELLULAR_STATUS_PIN, INPUT);
    
    digitalWrite(CELLULAR_RESET_PIN, HIGH);  // Không reset
    digitalWrite(CELLULAR_PWRKEY_PIN, LOW);  // Idle
    
    // Setup UART2
    _serial = new HardwareSerial(CELLULAR_UART_NUM);
    _serial->begin(CELLULAR_BAUD_RATE, SERIAL_8N1, 
                   CELLULAR_RX_PIN, CELLULAR_TX_PIN);
    
    delay(1000);  // Chờ serial ổn định
    
    // Create modem instance
    _modem = new TinyGsm(*_serial);
    _client = new TinyGsmClient(*_modem);
    
    // Power on module
    if (!powerOn()) {
        ESP_LOGE(TAG, "Failed to power on A7682S");
        return false;
    }
    
    // Test AT communication
    ESP_LOGI(TAG, "Testing AT communication...");
    _modem->restart();  // Software restart
    
    String modemInfo = _modem->getModemInfo();
    ESP_LOGI(TAG, "Modem Info: %s", modemInfo.c_str());
    
    // Get IMEI
    String imei = _modem->getIMEI();
    ESP_LOGI(TAG, "IMEI: %s", imei.c_str());
    
    ESP_LOGI(TAG, "✅ A7682S initialized successfully");
    return true;
}

bool CellularConnectionService::powerOn() {
    ESP_LOGI(TAG, "Powering on A7682S...");
    
    // Check if already on
    if (digitalRead(CELLULAR_STATUS_PIN) == HIGH) {
        ESP_LOGI(TAG, "Module already powered on");
        return true;
    }
    
    // Power on sequence: Pull PWRKEY low for 1-2 seconds
    digitalWrite(CELLULAR_PWRKEY_PIN, HIGH);
    delay(1500);
    digitalWrite(CELLULAR_PWRKEY_PIN, LOW);
    
    // Wait for STATUS pin to go high
    uint32_t start = millis();
    while (digitalRead(CELLULAR_STATUS_PIN) == LOW) {
        if (millis() - start > 10000) {
            ESP_LOGE(TAG, "Power on timeout");
            return false;
        }
        delay(100);
    }
    
    ESP_LOGI(TAG, "✅ Module powered on");
    delay(5000);  // Chờ module boot
    return true;
}

bool CellularConnectionService::connect() {
    if (_status == CELLULAR_CONNECTED) {
        ESP_LOGW(TAG, "Already connected");
        return true;
    }
    
    ESP_LOGI(TAG, "Connecting to cellular network...");
    _status = CELLULAR_CONNECTING;
    
    // Wait for network registration
    ESP_LOGI(TAG, "Waiting for network...");
    if (!waitForNetwork(CELLULAR_CONNECT_TIMEOUT)) {
        ESP_LOGE(TAG, "Network registration failed");
        _status = CELLULAR_ERROR;
        return false;
    }
    
    // Check signal quality
    _signalQuality = _modem->getSignalQuality();
    ESP_LOGI(TAG, "Signal Quality: %d (CSQ)", _signalQuality);
    
    if (_signalQuality < MIN_SIGNAL_QUALITY) {
        ESP_LOGW(TAG, "⚠️ Weak signal: %d", _signalQuality);
    }
    
    // Get operator name
    String operatorName = _modem->getOperator();
    ESP_LOGI(TAG, "Operator: %s", operatorName.c_str());
    
    // Connect GPRS/LTE data
    ESP_LOGI(TAG, "Connecting GPRS with APN: %s", _apn);
    if (!_modem->gprsConnect(_apn, _user, _password)) {
        ESP_LOGE(TAG, "GPRS connection failed");
        _status = CELLULAR_ERROR;
        return false;
    }
    
    // Get IP address
    String localIP = _modem->getLocalIP();
    ESP_LOGI(TAG, "✅ Connected! IP: %s", localIP.c_str());
    
    _status = CELLULAR_CONNECTED;
    _connectionStartTime = millis();
    
    // Trigger callback
    if (_onConnected) {
        _onConnected();
    }
    
    return true;
}

void CellularConnectionService::disconnect() {
    if (_status == CELLULAR_DISCONNECTED) return;
    
    ESP_LOGI(TAG, "Disconnecting from cellular network...");
    
    if (_modem) {
        _modem->gprsDisconnect();
    }
    
    _status = CELLULAR_DISCONNECTED;
    
    if (_onDisconnected) {
        _onDisconnected();
    }
}

void CellularConnectionService::update() {
    static uint32_t lastSignalCheck = 0;
    uint32_t now = millis();
    
    // Check signal quality periodically
    if (now - lastSignalCheck >= SIGNAL_CHECK_INTERVAL) {
        checkSignalQuality();
        lastSignalCheck = now;
    }
    
    // Auto-reconnect if disconnected
    if (_status != CELLULAR_CONNECTED && _autoReconnect) {
        if (now - _lastReconnectAttempt >= _reconnectInterval) {
            handleReconnect();
            _lastReconnectAttempt = now;
        }
    }
}

bool CellularConnectionService::isConnected() {
    if (!_modem) return false;
    
    bool connected = _modem->isGprsConnected();
    
    if (!connected && _status == CELLULAR_CONNECTED) {
        ESP_LOGW(TAG, "Connection lost!");
        _status = CELLULAR_DISCONNECTED;
        if (_onDisconnected) {
            _onDisconnected();
        }
    }
    
    return connected;
}

bool CellularConnectionService::waitForNetwork(uint32_t timeout_ms) {
    uint32_t start = millis();
    
    while (!_modem->isNetworkConnected()) {
        if (millis() - start > timeout_ms) {
            return false;
        }
        delay(500);
        ESP_LOGI(TAG, "Waiting for network registration...");
    }
    
    return true;
}

void CellularConnectionService::checkSignalQuality() {
    if (!_modem) return;
    
    int csq = _modem->getSignalQuality();
    
    if (csq != _signalQuality) {
        ESP_LOGI(TAG, "Signal Quality: %d → %d", _signalQuality, csq);
        _signalQuality = csq;
        
        if (csq < MIN_SIGNAL_QUALITY && _onSignalLow) {
            _onSignalLow();
        }
    }
}

void CellularConnectionService::handleReconnect() {
    ESP_LOGI(TAG, "Attempting to reconnect...");
    _status = CELLULAR_RECONNECTING;
    
    if (_onReconnecting) {
        _onReconnecting();
    }
    
    connect();
}

TinyGsmClient* CellularConnectionService::getClient() {
    return _client;
}

String CellularConnectionService::getLocalIP() {
    if (!_modem) return "";
    return _modem->getLocalIP();
}

int CellularConnectionService::getSignalQuality() {
    return _signalQuality;
}

String CellularConnectionService::getOperatorName() {
    if (!_modem) return "";
    return _modem->getOperator();
}

String CellularConnectionService::getIMEI() {
    if (!_modem) return "";
    return _modem->getIMEI();
}

CellularStatus CellularConnectionService::getStatus() {
    return _status;
}

uint32_t CellularConnectionService::getUptime() {
    if (_status != CELLULAR_CONNECTED) return 0;
    return (millis() - _connectionStartTime) / 1000;
}

// Event callback setters
void CellularConnectionService::onConnected(CellularEventCallback callback) {
    _onConnected = callback;
}

void CellularConnectionService::onDisconnected(CellularEventCallback callback) {
    _onDisconnected = callback;
}

void CellularConnectionService::onReconnecting(CellularEventCallback callback) {
    _onReconnecting = callback;
}

void CellularConnectionService::onSignalLow(CellularEventCallback callback) {
    _onSignalLow = callback;
}
```

---

### Phase 2: Cập Nhật Gateway Application (6-8 giờ)

#### Task 2.1: Cập Nhật gateway_config.h
**Thời gian**: 15 phút

```cpp
// gateway_config.h - Thêm phần này

// Network configuration - CHỌN 1 TRONG 2
#define USE_CELLULAR_NETWORK  1    // Dùng cellular (A7682S)
// #define USE_WIFI_NETWORK   1    // Dùng WiFi (comment out)

#ifdef USE_CELLULAR_NETWORK
    #include "cellular_config.h"
#else
    // WiFi configuration (giữ nguyên)
    #ifndef WIFI_SSID
    #define WIFI_SSID "OXII"
    #endif
    // ... rest of WiFi config ...
#endif
```

#### Task 2.2: Sửa gateway_app.h
**Thời gian**: 30 phút

```cpp
// gateway_app.h - Thay đổi
#ifdef USE_CELLULAR_NETWORK
    #include "cellular_connection_service.h"
#else
    #include "wifi_connection_service.h"
#endif

class GatewayApp {
private:
    #ifdef USE_CELLULAR_NETWORK
        CellularConnectionService* cellularService;  // MỚI
    #else
        WiFiConnectionService* wifiService;          // Cũ
    #endif
    
    FirebaseClient* firebaseClient;
    // ... rest unchanged ...
```

#### Task 2.3: Sửa gateway_app.cpp - setupWiFi()
**Thời gian**: 2 giờ  
**Thay đổi**: Đổi tên thành `setupNetwork()` và hỗ trợ cả 2 loại

```cpp
// gateway_app.cpp

#ifdef USE_CELLULAR_NETWORK
void GatewayApp::setupNetwork() {
    ESP_LOGI(TAG, "Setting up Cellular network (A7682S)...");
    
    cellularService = new CellularConnectionService(
        SIM_APN,
        SIM_USER,
        SIM_PASSWORD,
        true,          // Auto-reconnect
        10000          // Retry every 10s
    );
    
    // Initialize module
    if (!cellularService->initialize()) {
        ESP_LOGE(TAG, "❌ Failed to initialize A7682S");
        led_pattern_error();
        return;
    }
    
    // Event callbacks
    cellularService->onConnected([]() {
        ESP_LOGI(TAG, "📶 Cellular connected!");
        gatewayState.wifiConnected = true;  // Reuse flag name
        led_pattern_connected();
    });
    
    cellularService->onDisconnected([]() {
        ESP_LOGW(TAG, "📵 Cellular disconnected!");
        gatewayState.wifiConnected = false;
        gatewayState.firebaseConnected = false;
        led_pattern_error();
    });
    
    cellularService->onSignalLow([]() {
        ESP_LOGW(TAG, "⚠️ Weak signal detected");
    });
    
    // Connect
    ESP_LOGI(TAG, "Connecting to cellular network...");
    if (cellularService->connect()) {
        gatewayState.wifiConnected = true;
        ESP_LOGI(TAG, "✅ Cellular connected!");
        ESP_LOGI(TAG, "IP Address: %s", cellularService->getLocalIP().c_str());
        ESP_LOGI(TAG, "Operator: %s", cellularService->getOperatorName().c_str());
        ESP_LOGI(TAG, "Signal: %d/31", cellularService->getSignalQuality());
    } else {
        ESP_LOGE(TAG, "❌ Cellular connection failed");
        led_pattern_error();
    }
}
#else
// Giữ nguyên setupWiFi() cũ
void GatewayApp::setupWiFi() {
    // ... code WiFi hiện tại ...
}
#endif
```

#### Task 2.4: Sửa Firebase Client Initialization
**Thời gian**: 2-3 giờ  
**File**: `src/application/app_gateway/firebase_client.cpp`

```cpp
// firebase_client.cpp - initialize()

bool FirebaseClient::initialize() {
    ESP_LOGI(TAG, "Initializing Firebase client...");
    
    #ifdef USE_CELLULAR_NETWORK
        // Get cellular client
        auto* cellularClient = gatewayApp->getCellularClient();
        if (!cellularClient) {
            ESP_LOGE(TAG, "Cellular client not available");
            return false;
        }
        
        // Configure Firebase to use cellular client
        config.signer.tokens.legacy_token = FIREBASE_AUTH;
        Firebase.begin(&config, &auth, cellularClient);  // Use TinyGsmClient
        
        ESP_LOGI(TAG, "Firebase using Cellular network");
    #else
        // WiFi client (existing code)
        config.signer.tokens.legacy_token = FIREBASE_AUTH;
        Firebase.begin(&config, &auth);
        
        ESP_LOGI(TAG, "Firebase using WiFi network");
    #endif
    
    Firebase.reconnectWiFi(true);  // Auto-reconnect (works for cellular too)
    
    return true;
}
```

#### Task 2.5: Cập Nhật loop() - Network Monitoring
**Thời gian**: 1 giờ

```cpp
// gateway_app.cpp - loop()

void GatewayApp::loop() {
    // ... existing code ...
    
    #ifdef USE_CELLULAR_NETWORK
        // Update cellular service (kiểm tra kết nối)
        if (cellularService) {
            cellularService->update();
        }
        
        // Log signal quality periodically
        static uint32_t lastSignalLog = 0;
        if (millis() - lastSignalLog > 60000) {  // Mỗi 1 phút
            ESP_LOGI(TAG, "📶 Signal: %d/31, Operator: %s, IP: %s",
                     cellularService->getSignalQuality(),
                     cellularService->getOperatorName().c_str(),
                     cellularService->getLocalIP().c_str());
            lastSignalLog = millis();
        }
    #else
        // Update WiFi service (existing)
        if (wifiService) {
            wifiService->update();
        }
    #endif
    
    // ... rest of loop unchanged ...
}
```

---

### Phase 3: Testing & Validation (4-6 giờ)

#### Task 3.1: Hardware Setup Test
**Thời gian**: 1 giờ

**Checklist**:
- [ ] Kiểm tra kết nối UART (TX/RX đúng chân)
- [ ] Kiểm tra nguồn 3.3V ổn định
- [ ] Cắm SIM card có data
- [ ] Kết nối ăng-ten 4G
- [ ] Test AT commands thủ công (Serial Monitor)

**AT Commands Test**:
```
AT                          → OK
AT+CGMM                     → A7682S
AT+CPIN?                    → +CPIN: READY
AT+CSQ                      → +CSQ: 15,99 (signal quality)
AT+COPS?                    → +COPS: 0,0,"VINAPHONE"
AT+CGATT?                   → +CGATT: 1 (attached to network)
```

#### Task 3.2: Connection Test
**Thời gian**: 2 giờ

**Test Cases**:
1. **Power On Test**: Module khởi động đúng
2. **Network Registration**: Đăng ký mạng thành công
3. **GPRS Connection**: Kết nối data thành công
4. **IP Assignment**: Nhận được IP từ carrier
5. **DNS Resolution**: Resolve firebase domain
6. **Firebase Connect**: Kết nối Firebase thành công

**Logging**:
```cpp
ESP_LOGI(TAG, "✅ Module powered on");
ESP_LOGI(TAG, "✅ Network registered: VINAPHONE");
ESP_LOGI(TAG, "✅ GPRS connected");
ESP_LOGI(TAG, "✅ IP: 10.123.45.67");
ESP_LOGI(TAG, "✅ Firebase connected");
```

#### Task 3.3: Firebase Upload Test
**Thời gian**: 2 giờ

**Test Scenarios**:
1. Upload sensor data → Kiểm tra Firebase console
2. Upload routing table → Xác minh dữ liệu
3. Upload gateway status → Verify timestamp
4. Command polling → Test remote command từ Firebase
5. Offline buffer → Ngắt kết nối, reconnect, sync data

#### Task 3.4: Long-Duration Test
**Thời gian**: 24 giờ (chạy nền)

**Monitoring**:
- Kết nối ổn định >24h
- Không bị memory leak
- Auto-reconnect hoạt động
- Signal quality tracking
- Data upload success rate >95%

---

### Phase 4: Optimization & Deployment (4-6 giờ)

#### Task 4.1: Power Management
**Thời gian**: 2 giờ

```cpp
// Tối ưu tiêu thụ điện
void CellularConnectionService::enterSleepMode() {
    // A7682S sleep mode (giảm từ 20mA xuống 2mA)
    _modem->sendAT("+CSCLK=1");  // Enable sleep
}

void CellularConnectionService::exitSleepMode() {
    _modem->sendAT("+CSCLK=0");  // Disable sleep
}
```

#### Task 4.2: Error Handling & Recovery
**Thời gian**: 2 giờ

**Scenarios**:
1. SIM card không có data
2. Sóng yếu (CSQ < 10)
3. Network timeout
4. Firebase connection lost
5. Module hang (watchdog reset)

```cpp
// Auto-recovery mechanism
void CellularConnectionService::handleError() {
    static uint8_t errorCount = 0;
    
    errorCount++;
    
    if (errorCount > 5) {
        ESP_LOGW(TAG, "Too many errors, resetting module...");
        reset();
        errorCount = 0;
    }
}
```

#### Task 4.3: Monitoring & Logging
**Thời gian**: 1 giờ

```cpp
// Thêm vào gateway status upload
void GatewayApp::queueGatewayStatusUpload(uint8_t priority) {
    #ifdef USE_CELLULAR_NETWORK
        int signalQuality = cellularService->getSignalQuality();
        String operatorName = cellularService->getOperatorName();
        String localIP = cellularService->getLocalIP();
        
        // Upload thêm thông tin cellular
        firebaseClient->uploadCellularStatus(
            signalQuality,
            operatorName,
            localIP
        );
    #endif
}
```

---

## 📊 5. So Sánh WiFi vs Cellular

| Tiêu Chí | WiFi (Hiện Tại) | A7682S Cellular (Mới) |
|----------|----------------|----------------------|
| **Setup Complexity** | Dễ (chỉ cần SSID/password) | Trung bình (cần SIM + config) |
| **Phạm Vi** | ~50m | Toàn quốc ✅ |
| **Độ Ổn Định** | Phụ thuộc router | Độc lập ✅ |
| **Latency** | 10-50ms | 50-200ms |
| **Chi Phí Hàng Tháng** | 0 (có sẵn WiFi) | 50-100k VNĐ (data) |
| **Tiêu Thụ Điện** | 150-200mA | 20-600mA (peak) |
| **Code Changes** | 0 | ~500-800 lines |
| **Hardware Cost** | $0 | $30-40 (module + sim) |
| **Development Time** | 0 | ~20-30 giờ |

---

## ⚠️ 6. Rủi Ro & Mitigation

| Rủi Ro | Mức Độ | Xác Suất | Giải Pháp |
|--------|--------|----------|-----------|
| **Pin conflict** | HIGH | LOW | ✅ Dùng UART2 (GPIO 43/44) |
| **Nguồn không đủ** | HIGH | MEDIUM | External 5V/2A PSU + buck converter |
| **SIM không có data** | MEDIUM | LOW | Test trước với điện thoại |
| **Firebase incompatible** | CRITICAL | LOW | ✅ Firebase hỗ trợ custom client |
| **Sóng yếu ở vùng xa** | MEDIUM | MEDIUM | Implement retry + offline buffer |
| **Module hang** | LOW | LOW | Watchdog + auto-reset |
| **Chi phí data cao** | LOW | HIGH | Compress JSON, giảm tần suất upload |

---

## 💰 7. Chi Phí Ước Tính

### Hardware (1 lần)
| Item | Số Lượng | Giá | Tổng |
|------|----------|-----|------|
| Module A7682S | 1 | $35 | $35 |
| SIM Card Holder | 1 | $2 | $2 |
| Ăng-ten 4G | 1 | $5 | $5 |
| Buck Converter 5V→3.8V | 1 | $3 | $3 |
| External PSU 5V/2A | 1 | $8 | $8 |
| Dây kết nối | 1 set | $2 | $2 |
| **Tổng Hardware** | - | - | **~$55** |

### Recurring (Hàng Tháng)
| Item | Giá/Tháng |
|------|-----------|
| SIM Data (2GB/tháng) | 50-70k VNĐ |
| Firebase free tier | 0 VNĐ |
| **Tổng/Tháng** | **~50-70k VNĐ** |

### Development Cost (1 lần)
| Phase | Thời Gian | Giá (nếu outsource) |
|-------|----------|---------------------|
| Phase 1 | 6 giờ | 3.000k |
| Phase 2 | 8 giờ | 4.000k |
| Phase 3 | 6 giờ | 3.000k |
| Phase 4 | 6 giờ | 3.000k |
| **Tổng Dev** | **26 giờ** | **~13.000k VNĐ** |

---

## 📋 8. Checklist Triển Khai

### Trước Khi Bắt Đầu
- [ ] Mua module A7682S (hoặc kiểm tra module hiện có)
- [ ] Mua SIM card có data (Viettel/Vinaphone/Mobifone)
- [ ] Chuẩn bị ăng-ten 4G
- [ ] Kiểm tra ESP32-S3 GPIO available (43/44)
- [ ] Backup code hiện tại (WiFi version)

### Phase 1 - Setup
- [ ] Cập nhật platformio.ini với TinyGSM
- [ ] Tạo cellular_config.h
- [ ] Implement CellularConnectionService.h
- [ ] Implement CellularConnectionService.cpp
- [ ] Test compile (no errors)

### Phase 2 - Integration
- [ ] Cập nhật gateway_config.h
- [ ] Sửa gateway_app.h (add cellular support)
- [ ] Sửa gateway_app.cpp (setupNetwork)
- [ ] Sửa firebase_client.cpp (custom client)
- [ ] Test compile again

### Phase 3 - Testing
- [ ] Hardware connection test
- [ ] AT commands manual test
- [ ] Network registration test
- [ ] Firebase connection test
- [ ] Data upload test (sensor/routing table)
- [ ] Command polling test
- [ ] 24h stability test

### Phase 4 - Deployment
- [ ] Power optimization
- [ ] Error handling
- [ ] Monitoring & logging
- [ ] Documentation
- [ ] Production deployment

---

## 🎯 9. Kết Luận & Khuyến Nghị

### ✅ Khả Thi
Việc thay thế WiFi bằng A7682S hoàn toàn **KHẢ THI** với:
- Hardware tương thích (UART2 free)
- Software hỗ trợ (TinyGSM + Firebase)
- Chi phí hợp lý (~$55 + 50k/tháng)

### 🎯 Khuyến Nghị

**OPTION 1: Migration Hoàn Toàn (Cellular Only)**
- Xóa toàn bộ WiFi code
- Chỉ dùng A7682S
- Phù hợp: Vùng xa không có WiFi

**OPTION 2: Dual Mode (WiFi + Cellular Backup)** ⭐ KHUYẾN NGHỊ
- Giữ cả WiFi và Cellular
- WiFi primary, Cellular backup
- Tự động failover
- Chi phí cao hơn ~2 giờ dev
- Độ tin cậy cao nhất

**OPTION 3: Switchable Mode**
- Cho phép chọn WiFi hoặc Cellular qua config
- Dễ test và debug
- Flexibility cao

### 📅 Timeline Đề Xuất

**Nếu làm full-time (8h/ngày)**:
- Ngày 1-2: Phase 1 (Setup)
- Ngày 3: Phase 2 (Integration)
- Ngày 4: Phase 3 (Testing)
- Ngày 5: Phase 4 (Optimization)
- **Total: 5 ngày làm việc**

**Nếu làm part-time (4h/ngày)**:
- **Total: 10 ngày làm việc**

---

## 📞 10. Support & Resources

### Tài Liệu Tham Khảo
- [A7682S AT Command Manual](https://simcom.ee/documents/A7682E/A7682E_Series_AT_Command_Manual_V1.02.pdf)
- [TinyGSM Library GitHub](https://github.com/vshymanskyy/TinyGSM)
- [Firebase ESP32 Custom Client](https://github.com/mobizt/Firebase-ESP32)

### Nhà Cung Cấp Module
- Lazada: "Module A7682S 4G LTE"
- Shopee: "SIMCOM A7682S"
- Alibaba: Bulk order (10+ units)

### SIM Card Khuyến Nghị
| Nhà Mạng | Gói Data | Giá | Tốc Độ |
|----------|----------|-----|--------|
| Viettel | D50 (2GB/tháng) | 50k | 4G ✅ |
| Vinaphone | V90 (3GB/tháng) | 90k | 4G ✅ |
| Mobifone | C90 (3GB/tháng) | 90k | 4G ✅ |

---

**Document Version**: 1.0  
**Last Updated**: November 10, 2025  
**Author**: GitHub Copilot  
**Status**: ✅ Ready for Implementation
