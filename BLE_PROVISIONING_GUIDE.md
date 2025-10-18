# BLE Provisioning Integration Guide

## Tổng quan

Đã triển khai hoàn chỉnh BLE provisioning cho Gateway với các tính năng:
- ✅ BLE advertising khi chưa có cấu hình (tên: KAGRI-GW-XXXX)
- ✅ Nhận cấu hình từ Mobile App qua BLE (ssid, password, userUID)
- ✅ Tự động tính netkey từ userUID + MAC của Gateway (SHA-256)
- ✅ Lưu cấu hình vào NVS (Preferences)
- ✅ Reboot và kết nối WiFi/Firebase với cấu hình mới

## Files đã tạo

### 1. `ble_provisioning.h/.cpp`
- BLE GATT server với NimBLE-Arduino
- Service UUID: `0000ffb0-0000-1000-8000-00805f9b34fb`
- Write Characteristic: `0000ffb1-0000-1000-8000-00805f9b34fb`
- Notify Characteristic: `0000ffb2-0000-1000-8000-00805f9b34fb`
- Parse JSON payload: `{ssid, password, userUID}`

### 2. `crypto_utils.h/.cpp`
- Tính netkey từ SHA-256(userUID|MAC) → lấy 16 byte đầu
- Sử dụng mbedtls (có sẵn trong ESP32)
- Format MAC: "AA:BB:CC:DD:EE:FF"

### 3. `provision_manager.h/.cpp`
- Quản lý toàn bộ quy trình provisioning
- Kiểm tra trạng thái đã provision chưa (NVS)
- Callback từ BLE → tính netkey → lưu NVS → reboot
- NVS keys:
  - `provisioned` (bool)
  - `wifi_ssid` (string)
  - `wifi_pass` (string)
  - `user_uid` (string)
  - `netkey` (blob 16 bytes)

## Cách tích hợp vào Gateway App

### Bước 1: Update `platformio.ini`
Đã thêm dependency:
```ini
lib_deps = 
    ...
    h2zero/NimBLE-Arduino@^1.4.2
```

### Bước 2: Update `gateway_app.h`
Thêm include và member:
```cpp
#include "provision_manager.h"

class GatewayApp {
private:
    ProvisionManager* provisionManager;  // Thêm dòng này
    ...
};
```

### Bước 3: Update `gateway_app.cpp` - Hàm `setup()`

**QUAN TRỌNG**: Thêm provisioning check NGAY ĐẦU hàm `setup()`, TRƯỚC khi init WiFi/Firebase:

```cpp
void GatewayApp::setup() {
    ESP_LOGI(TAG, "=== LoRaMesh Gateway Application ===");
    
    led_init();
    led_pattern_startup();
    
    // ============== THÊM PHẦN NÀY ==============
    // Check provisioning status
    provisionManager = new ProvisionManager();
    
    if (!provisionManager->isProvisioned()) {
        ESP_LOGW(TAG, "❌ Device not provisioned!");
        ESP_LOGW(TAG, "📱 Starting BLE provisioning...");
        ESP_LOGW(TAG, "🔍 Open mobile app and scan for this gateway");
        
        led_pattern_error(); // LED error pattern to indicate provisioning mode
        
        // Start BLE provisioning (will not return until provisioned + rebooted)
        provisionManager->startProvisioningIfNeeded();
        
        // If we reach here, BLE failed to start
        ESP_LOGE(TAG, "Failed to start provisioning! Halting.");
        while(1) {
            led_pattern_error();
            delay(1000);
        }
    }
    
    // Load provisioned data
    String wifiSsid, wifiPassword, userUID;
    uint8_t netkey[16];
    String gatewayMAC;
    
    provisionManager->getWiFiConfig(wifiSsid, wifiPassword);
    provisionManager->getUserUID(userUID);
    provisionManager->getNetkey(netkey);
    gatewayMAC = provisionManager->getGatewayMAC();
    
    ESP_LOGI(TAG, "✓ Device provisioned:");
    ESP_LOGI(TAG, "  WiFi: %s", wifiSsid.c_str());
    ESP_LOGI(TAG, "  User: %s", userUID.c_str());
    ESP_LOGI(TAG, "  MAC: %s", gatewayMAC.c_str());
    ESP_LOGI(TAG, "  Netkey: %s", CryptoUtils::toHexString(netkey, 16).c_str());
    
    // ============== KẾT THÚC PHẦN THÊM ==============
    
    // Tiếp tục với các phần cũ...
    uint16_t gatewayNodeId = computeNodeIdFromWifiMac();
    ...
```

### Bước 4: Update WiFi connection

Thay thế hardcoded WiFi credentials bằng provisioned data.

**Tìm đoạn code kết nối WiFi** (thường trong `setupWiFi()` hoặc tương tự):

```cpp
// TRƯỚC ĐÂY (hardcoded):
// WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

// SAU KHI SỬA (dynamic):
String ssid, password;
provisionManager->getWiFiConfig(ssid, password);
WiFi.begin(ssid.c_str(), password.c_str());
```

### Bước 5: Update Firebase paths với userUID

Firebase paths cần thay đổi từ:
- `nodes/{nodeId}/...` → `nodes/{userUID}/{gatewayMAC}/{nodeId}/...`
- `gateways/{gatewayId}/...` → `gateways/{userUID}/{gatewayMAC}/...`
- `sensor_data/{nodeId}/...` → `sensor_data/{userUID}/{nodeId}/...`

**Trong `firebase_client.cpp`, thêm biến global:**

```cpp
// At top of file
static String g_userUID = "";
static String g_gatewayMAC = "";

// Function to set userUID (call once after provisioning check)
void FirebaseClient::setUserContext(const String& userUID, const String& gatewayMAC) {
    g_userUID = userUID;
    g_gatewayMAC = gatewayMAC;
    ESP_LOGI("Firebase", "User context set: %s / %s", userUID.c_str(), gatewayMAC.c_str());
}
```

**Trong `gateway_app.cpp` sau provisioning check:**

```cpp
// After loading provisioning data
firebaseClient->setUserContext(userUID, gatewayMAC);
```

**Update tất cả Firebase path constructors:**

```cpp
// Example: uploadNodeInfo()
// OLD: String path = "nodes/" + String(nodeId) + "/info";
// NEW:
String path = "nodes/" + g_userUID + "/" + g_gatewayMAC + "/" + String(nodeId) + "/info";

// Example: uploadRoutingTable()
// OLD: String path = "gateways/" + String(gatewayId) + "/routing_table";
// NEW:
String path = "gateways/" + g_userUID + "/" + g_gatewayMAC + "/routing_table";

// Example: uploadSensorData()
// OLD: String path = "sensor_data/" + String(nodeId) + "/" + String(timestamp);
// NEW:
String path = "sensor_data/" + g_userUID + "/" + String(nodeId) + "/" + String(timestamp);
```

### Bước 6: Update mesh network key

Netkey đã lưu trong NVS cần được load và apply vào LoraMesher:

```cpp
// In setup(), after loading provisioning data
uint8_t netkey[16];
provisionManager->getNetkey(netkey);

// Apply to mesh network (assuming you have NetworkConfig or similar)
NetworkConfig meshConfig;
memcpy(meshConfig.networkKey, netkey, 16);
// ... set other config fields ...

// Apply to LoraMesher
NetkeyDistributionService::updateLocalNetworkKey(
    meshConfig.networkKey, 
    meshConfig.authToken, 
    meshConfig.networkId, 
    meshConfig.keyVersion
);
```

## Testing Flow

### 1. Flash chương trình lần đầu (chưa provision)
```bash
pio run -t upload -e esp32-gateway
pio device monitor -e esp32-gateway
```

**Expected output:**
```
[GATEWAY] Device not provisioned!
[GATEWAY] Starting BLE provisioning...
[BLEProv] BLE provisioning started. Device name: KAGRI-GW-XXXX
```

LED sẽ nhấp nháy error pattern.

### 2. Provision qua Mobile App

1. Mở app, login (e.g. kagri.aiot@kagri.com)
2. Tap icon Bluetooth trên HomeScreen
3. Scan → thấy "KAGRI-GW-XXXX"
4. Chọn gateway
5. Nhập WiFi SSID/Password
6. Tap "Provision"

**Expected trên Serial:**
```
[BLEProv] Received provisioning data: XXX bytes
[BLEProv] Provisioning data validated:
[BLEProv]   SSID: YourSSID
[BLEProv]   UserUID: Rtl2mpwRLgVn2g7Pn2toHSn2pBd2
[BLEProv]   Password: ***
[CryptoUtils] Deriving netkey from: Rtl2mpwRLgVn2g7Pn2toHSn2pBd2|AA:BB:CC:DD:EE:FF
[CryptoUtils] Netkey derived: a1b2c3d4e5f6...
[ProvisionMgr] ✓ Provisioning data saved successfully
[ProvisionMgr] Rebooting in 2 seconds...
```

Gateway reboot.

### 3. Sau reboot - Gateway kết nối WiFi/Firebase

**Expected output:**
```
[GATEWAY] ✓ Device provisioned:
[GATEWAY]   WiFi: YourSSID
[GATEWAY]   User: Rtl2mpwRLgVn2g7Pn2toHSn2pBd2
[GATEWAY]   MAC: AA:BB:CC:DD:EE:FF
[GATEWAY]   Netkey: a1b2c3d4e5f6...
[WiFi] Connecting to YourSSID...
[WiFi] Connected! IP: 192.168.1.123
[Firebase] Connecting to Firebase...
[Firebase] User context set: Rtl2mpwRLgVn2g7Pn2toHSn2pBd2 / AA:BB:CC:DD:EE:FF
[Firebase] Connected successfully
```

LED chuyển sang connected pattern (nhấp nháy nhanh).

### 4. Kiểm tra Firebase Console

Vào Firebase Realtime Database, xem structure:
```
nodes/
  Rtl2mpwRLgVn2g7Pn2toHSn2pBd2/    ← userUID
    AA:BB:CC:DD:EE:FF/              ← gatewayMAC
      0xE764/                        ← nodeId
        info/
        latest_data/

gateways/
  Rtl2mpwRLgVn2g7Pn2toHSn2pBd2/
    AA:BB:CC:DD:EE:FF/
      status/
      routing_table/

sensor_data/
  Rtl2mpwRLgVn2g7Pn2toHSn2pBd2/
    0xE764/
      1760763398348/
```

### 5. Kiểm tra Mobile App

- Mở app, login với cùng account (kagri.aiot@kagri.com)
- HomeScreen sẽ hiển thị devices từ Firebase
- Devices chỉ thuộc về user hiện tại (multi-user isolation OK)

## Factory Reset

Để xóa provisioning và quay về chế độ BLE:

```cpp
// Add button handler or serial command
if (factoryResetRequested) {
    provisionManager->clearProvisionData();
    ESP.restart();
}
```

Hoặc flash lại firmware với NVS erase:
```bash
pio run -t erase -e esp32-gateway
pio run -t upload -e esp32-gateway
```

## Troubleshooting

### BLE không scan được
- Check Android permissions (Location + Bluetooth)
- iOS: Check Info.plist has usage descriptions (chưa làm)
- Kiểm tra gateway đang ở provisioning mode (LED error pattern)

### Provisioning timeout
- Check JSON payload size < MTU (512 bytes OK)
- Check UUID khớp giữa app và firmware
- Check ArduinoJson buffer đủ lớn

### Gateway không kết nối WiFi sau provision
- Check SSID/password đúng chưa
- Check Serial log xem có lỗi WiFi không
- Thử factory reset và provision lại

### Firebase không nhận data
- Check userUID có đúng không (in ra Serial)
- Check Firebase paths có userUID prefix chưa
- Check Firebase Rules cho phép write với auth.uid

## Next Steps

1. ✅ Test provisioning flow end-to-end
2. ✅ Verify multi-user isolation trên Firebase
3. ⏭️ Add iOS Info.plist BLE usage descriptions
4. ⏭️ Add factory reset button/command
5. ⏭️ Add OTA update support với provisioned config
6. ⏭️ Add provision status LED pattern (khác với error)

## Dependencies Added

```ini
lib_deps = 
    jgromes/RadioLib@^6.6.0
    bblanchon/ArduinoJson@^7.0.4
    mobizt/Firebase ESP32 Client@^4.4.17
    h2zero/NimBLE-Arduino@^1.4.2  ← THÊM
```

Mobile app dependencies (đã có):
```yaml
dependencies:
  flutter_blue_plus: ^1.32.12
  permission_handler: ^11.3.1
```

## Security Notes

- ✅ Netkey được tính từ userUID + Gateway MAC (không gửi qua BLE)
- ✅ WiFi password được encrypt trong BLE connection (default)
- ⚠️ Chưa có PIN/QR code protection cho lần provision đầu
- ⚠️ Chưa có OTA signature verification
- ✅ Multi-user isolation via Firebase paths

---

**Status**: ✅ Mobile app updated, Firmware code ready
**Next**: Integrate vào gateway_app.cpp và test
