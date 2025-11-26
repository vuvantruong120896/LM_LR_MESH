# WiFi Configuration Screen Flow - Sub-State Logic

## Vấn đề cũ ❌
Trước đó, khi nhấn nút 5 giây:
- Chuyển sang WIFI_CONFIG state ngay
- Screen hiển thị `drawBLEWaitingScreen()` **ngay lập tức**
- Không có screen hướng dẫn `drawWiFiConfigStartScreen()`

## Giải pháp mới ✅
Thêm **WiFi Config Sub-States** để quản lý screen transitions đúng thứ tự:

```
Nhấn 5s
  ↓
WIFI_CONFIG state enters
  ├─ [0-5s] Sub-state: WAITING_FOR_APP
  │         Screen: drawWiFiConfigStartScreen()
  │         (Hướng dẫn mở app, bật Bluetooth, scan thiết bị KAGRI_HHC)
  │
  └─ [5s+] Sub-state: APP_CONNECTED
           Screen: drawBLEWaitingScreen()
           (Chờ app kết nối và gửi credentials)
```

## Implementation Details

### 1. Sub-States định nghĩa (handheld_app.h)

```cpp
enum class WiFiConfigState {
    WAITING_FOR_APP,      // 0-5s: Show start screen
    APP_CONNECTED,        // 5s+: Show BLE waiting screen
    CREDENTIALS_RECEIVED, // App đã gửi credentials
    CONNECTING_WIFI,      // Đang kết nối WiFi
    SUCCESS,              // Kết nối thành công
    ERROR                 // Kết nối thất bại
};
```

### 2. State Machine Logic (handheld_app.cpp)

**Loop function - WIFI_CONFIG case:**
```cpp
case AppState::WIFI_CONFIG: {
    uint32_t wifiConfigDuration = currentTime - stateChangeTime;
    
    // 0-5 seconds: Show start screen (WAITING_FOR_APP)
    if (wifiConfigDuration < 5000) {
        wifiConfigSubState = WiFiConfigState::WAITING_FOR_APP;
        displayManager->drawWiFiConfigStartScreen();
    } 
    // 5+ seconds: Show BLE waiting screen (APP_CONNECTED)
    else {
        wifiConfigSubState = WiFiConfigState::APP_CONNECTED;
        int remainingSeconds = (60000 - wifiConfigDuration) / 1000;
        displayManager->drawBLEWaitingScreen(remainingSeconds);
    }
    
    // Timeout after 60 seconds
    if (wifiConfigDuration > 60000) {
        displayManager->drawHomeScreen();
        changeState(AppState::IDLE);
    }
    break;
}
```

### 3. Transition khi nhận Credentials

**BLE Callback - onWiFiCredentialsReceived():**
```cpp
void HandheldApp::onWiFiCredentialsReceived(const BleProvisioning::ProvisionData& data) {
    // Chuyển sub-state
    changeWiFiConfigSubState(WiFiConfigState::CREDENTIALS_RECEIVED);
    
    // Hiển thị credentials received screen
    displayManager->drawCredentialsReceivedScreen(data.ssid.c_str(), -45);
    
    // TODO: Kết nối WiFi từ credentials
}
```

### 4. Sub-State Change Handler

```cpp
void HandheldApp::changeWiFiConfigSubState(WiFiConfigState newSubState) {
    switch (newSubState) {
        case WiFiConfigState::WAITING_FOR_APP:
            displayManager->drawWiFiConfigStartScreen();
            break;
        case WiFiConfigState::APP_CONNECTED:
            displayManager->drawBLEWaitingScreen(60);
            break;
        case WiFiConfigState::CREDENTIALS_RECEIVED:
            // Hiển thị ở callback
            break;
        case WiFiConfigState::CONNECTING_WIFI:
            displayManager->drawWiFiConnectingScreen(0);
            break;
        case WiFiConfigState::SUCCESS:
            displayManager->drawWiFiConnectSuccessScreen("IP_ADDRESS");
            break;
        case WiFiConfigState::ERROR:
            displayManager->drawWiFiConnectErrorScreen("Error message");
            break;
    }
}
```

## Luồng hoạt động chi tiết

### T0: Người dùng nhấn 5s
```
Button hold 5s detected
  ↓
changeState(AppState::WIFI_CONFIG)
  ↓
stateChangeTime = current_millis
wifiConfigSubState = WAITING_FOR_APP
BLE.begin() - start advertising
```

### T0-T5s: Hướng dẫn người dùng
```
Loop iteration:
  wifiConfigDuration = 0-5000ms
  ↓
  if (wifiConfigDuration < 5000):
    displayManager->drawWiFiConfigStartScreen()
  
Screen hiển thị:
  - Header: "WiFi Config BLE"
  - BLE icon (sóng Bluetooth)
  - "Scan for device: KAGRI_HHC-XXYY"
  - Instructions:
    1. Open KAGRI App
    2. Enable Bluetooth
    3. Scan & select device
  - Status: "Waiting for connection..."
```

### T5s: Chuyển sang waiting screen
```
Loop iteration (T ≥ 5s):
  wifiConfigDuration = 5000+ms
  ↓
  if (wifiConfigDuration >= 5000):
    wifiConfigSubState = APP_CONNECTED
    remainingSeconds = (60000 - duration) / 1000
    displayManager->drawBLEWaitingScreen(remainingSeconds)

Screen hiển thị:
  - Header: "SEARCHING..."
  - BLE icon (animated waves)
  - "Searching for device..."
  - "Enable Bluetooth on phone"
  - "Time left: 55s" (countdown from 60)
  - Animated dots "[*  *  *]"
```

### T5s+: App kết nối và gửi credentials
```
Mobile app connected BLE
  ↓
Send JSON: {ssid: "WiFi", password: "...", userUID: "..."}
  ↓
BLE characteristic write callback
  ↓
onWiFiCredentialsReceived() called
  ↓
changeWiFiConfigSubState(CREDENTIALS_RECEIVED)
  ↓
Screen chuyển sang: drawCredentialsReceivedScreen()
  
Screen hiển thị:
  - "CREDENTIALS"
  - "WiFi Network: MyWiFi"
  - "Signal: 75%"
  - "Connecting to WiFi..."
```

### T60s: Timeout
```
Loop iteration (T ≥ 60s):
  if (wifiConfigDuration > 60000):
    displayManager->drawHomeScreen()
    changeState(AppState::IDLE)
    BLE stop
```

## Debug Log Output

### Start (T=0s)
```
[HandheldApp] State change: 0 -> 6  (IDLE -> WIFI_CONFIG)
[HandheldApp] WiFi config sub-state: WAITING_FOR_APP (show start screen)
[HandheldApp] Starting BLE provisioning...
[BLEProvHHC] BLE provisioning started. Device name: KAGRI_HHC-AB12
```

### Transition (T=5s)
```
[HandheldApp] WIFI_CONFIG: duration=5100 ms, subState=1, BLE active=1
```

### Credentials Received
```
[BLEProvHHC] Received provisioning data: XX bytes
[BLEProvHHC] Provisioning data received:
[BLEProvHHC]   SSID: MyWiFi
[BLEProvHHC]   UserUID: user123@firebase
[HandheldApp] WiFi credentials received from BLE:
[HandheldApp]   SSID: MyWiFi
[HandheldApp]   UserUID: user123@firebase
[HandheldApp] WiFi config sub-state change: 1 -> 2 (APP_CONNECTED -> CREDENTIALS_RECEIVED)
```

## Diagram Thời Gian

```
Time    Duration   Sub-State        Screen                    Action
0ms     0s         WAITING_FOR_APP  drawWiFiConfigStartScreen() -
1000ms  1s         WAITING_FOR_APP  drawWiFiConfigStartScreen() -
2000ms  2s         WAITING_FOR_APP  drawWiFiConfigStartScreen() -
3000ms  3s         WAITING_FOR_APP  drawWiFiConfigStartScreen() -
4000ms  4s         WAITING_FOR_APP  drawWiFiConfigStartScreen() -
5000ms  5s         APP_CONNECTED    drawBLEWaitingScreen(55)   ← Transition!
6000ms  6s         APP_CONNECTED    drawBLEWaitingScreen(54)   -
...
T_app   ?          APP_CONNECTED    drawBLEWaitingScreen()     ← App connects
T_cred  ?          CREDENTIALS_RECEIVED drawCredentialsReceivedScreen() ← Credentials!
60000ms 60s        (timeout)        drawHomeScreen()            ← Return to HOME
```

## Lợi ích của Sub-State Logic

✅ **Rõ ràng**: Luồng UX được định nghĩa rõ ràng
✅ **Dễ mở rộng**: Có thể thêm sub-states khác (CONNECTING_WIFI, SUCCESS, ERROR)
✅ **Debug dễ**: Log thấy được chính xác ở sub-state nào
✅ **Screen transitions**: Chuyển screen đúng thời điểm
✅ **User guidance**: 5s đầu hướng dẫn, sau đó mới show waiting screen

---

**Cập nhật**: 27/11/2025  
**Build Status**: ✅ SUCCESS - 35.4% Flash, 16.6% RAM
