# WiFi BLE Configuration Implementation Guide

## Completed: UI Screens

All 8 WiFi BLE configuration screens have been implemented in the Display Manager:

### 1. **drawWiFiConfigStartScreen()**
   - Shows device name (KAGRI-ESP32)
   - BLE icon indicator
   - Instructions to open app on phone
   - Status: "Waiting for connection..."

### 2. **drawBLEWaitingScreen(int timeoutSeconds)**
   - Animated BLE searching indicator
   - Shows timeout countdown
   - Instructions to enable Bluetooth
   - Animated progress dots

### 3. **drawBLEConnectedScreen(int progress)**
   - Green header indicating successful connection
   - Checkmark symbol
   - Shows progress bar for data reception (0-100%)
   - "Receiving WiFi data..." message

### 4. **drawCredentialsReceivedScreen(const char* ssid, int signalStrength)**
   - Displays received WiFi SSID
   - Shows signal strength as percentage bar
   - "Connecting to WiFi..." status
   - Magenta header

### 5. **drawWiFiConnectingScreen(int progress)**
   - Large animated progress bar (0-100%)
   - Animated dots showing activity
   - Shows connection percentage
   - Magenta header

### 6. **drawWiFiConnectSuccessScreen(const char* ipAddress)**
   - Green header for success
   - Checkmark symbol
   - Displays assigned IP address
   - Shows "Returning to HOME..." countdown
   - Ready to auto-return after 3 seconds

### 7. **drawWiFiConnectErrorScreen(const char* errorMsg)**
   - Red header indicating error
   - Shows error message
   - Lists possible causes:
     - Wrong Password
     - Network Unavailable
     - Signal Too Weak
   - Prompt to "Hold button to retry"

### 8. **drawBLEErrorScreen(int timeoutSeconds)**
   - Red header for BLE timeout
   - Shows error cause
   - Provides recovery instructions
   - Shows countdown before returning to HOME

---

## Integration Requirements

### State Machine Updates Needed:

Add to `handheld_app.h`:
```cpp
enum class WiFiConfigState {
    IDLE,
    BLE_WAIT,
    BLE_CONNECTED,
    RECEIVING_CREDS,
    WIFI_CONNECTING,
    WIFI_SUCCESS,
    WIFI_ERROR,
    BLE_ERROR
};
```

### BLE Configuration Handler Needed:

Implement in `handheld_app.cpp`:
```cpp
void onBLECredentialsReceived(const char* ssid, const char* password) {
    // Validate credentials
    // Start WiFi connection
    // Display appropriate screen based on result
}
```

### WiFi Connection Handler Needed:

```cpp
void connectToWiFi(const char* ssid, const char* password) {
    // Use ESP32 WiFi API: WiFi.begin(ssid, password)
    // Poll connection status
    // Update screen with progress (displayManager->drawWiFiConnectingScreen(progress))
    // On success: display IP address
    // On failure: show error message
    // Auto-return to HOME after timeout
}
```

### BLE Implementation Needed:

Add BLE service with 3 characteristics:
1. **SSID Characteristic**: String (128 bytes max)
2. **Password Characteristic**: String (64 bytes max) - encrypted recommended
3. **Trigger Characteristic**: Boolean to signal WiFi connection attempt

---

## Usage Example

### In Button Handler (5-second hold):
```cpp
void handleLongPress() {
    // Show WiFi config start screen
    displayManager->drawWiFiConfigStartScreen();
    changeState(AppState::WIFI_CONFIG);
    
    // Start BLE advertising (in separate task)
    startBLEServer();
}
```

### In Main Loop - WiFi Config State:
```cpp
case AppState::WIFI_CONFIG:
    switch(wifiConfigState) {
        case WiFiConfigState::BLE_WAIT:
            displayManager->drawBLEWaitingScreen(timeRemaining);
            if (bleConnected) {
                wifiConfigState = WiFiConfigState::BLE_CONNECTED;
            }
            if (timeRemaining <= 0) {
                wifiConfigState = WiFiConfigState::BLE_ERROR;
            }
            break;
            
        case WiFiConfigState::BLE_CONNECTED:
            displayManager->drawBLEConnectedScreen(dataReceivedPercent);
            if (credentialsReceived) {
                wifiConfigState = WiFiConfigState::WIFI_CONNECTING;
            }
            break;
            
        case WiFiConfigState::WIFI_CONNECTING:
            displayManager->drawWiFiConnectingScreen(connectionProgress);
            if (wifiConnected) {
                wifiConfigState = WiFiConfigState::WIFI_SUCCESS;
            } else if (connectionFailed) {
                wifiConfigState = WiFiConfigState::WIFI_ERROR;
            }
            break;
            
        case WiFiConfigState::WIFI_SUCCESS:
            displayManager->drawWiFiConnectSuccessScreen(ipAddress);
            if (autoReturnTimer > 3000) {  // 3 seconds
                changeState(AppState::IDLE);
                displayManager->drawHomeScreen();
            }
            break;
    }
    break;
```

---

## Color Coding Reference

| State | Header Color | Meaning |
|-------|-------------|---------|
| BLE Waiting | Magenta | Configuration in progress |
| BLE Connected | Green | Device connected successfully |
| Credentials Received | Magenta | WiFi details received |
| WiFi Connecting | Magenta | Connection attempt in progress |
| WiFi Success | Green | Successfully connected |
| WiFi Error | Red | Connection failed |
| BLE Error | Red | BLE timeout/failure |

---

## Screen Timeouts

- BLE Waiting: **60 seconds** (auto-fail if no connection)
- WiFi Connecting: **30 seconds** (auto-fail if no connection)
- WiFi Success: **3 seconds** (auto-return to HOME)
- WiFi Error: **10 seconds** (auto-return to HOME, allow retry)
- BLE Error: **10 seconds** (auto-return to HOME)

---

## Next Implementation Steps

1. **Create WiFi Configuration BLE Service**
   - Define service UUID and characteristic UUIDs
   - Configure BLE server startup on 5-second button press
   - Implement credential receiving callbacks

2. **Add WiFi Connection Logic**
   - Use ESP32 WiFi API to connect to received credentials
   - Implement connection timeout and retry logic
   - Retrieve and display assigned IP address

3. **Update State Machine**
   - Add WiFi configuration substates
   - Handle state transitions based on BLE/WiFi events
   - Implement auto-timeout and auto-return logic

4. **Add Progress Tracking**
   - Track BLE data reception progress
   - Track WiFi connection progress
   - Update display screens with real-time progress

5. **Test on Device**
   - Verify each screen displays correctly
   - Test BLE connection flow
   - Test WiFi connection with valid and invalid credentials
   - Verify timeouts and error handling

---

## Files Modified

- ✅ `tft_display_manager_lvgl.h` - Added 8 new method declarations
- ✅ `tft_display_manager_lvgl.cpp` - Implemented 8 new screen functions
- 📝 `WIFI_BLE_CONFIG_SCENARIO.md` - Detailed UI specifications
- 📝 `WIFI_BLE_CONFIG_IMPLEMENTATION.md` - This guide

---

## Example: Complete Flow

```
HOME Screen (user holds button 5s)
    ↓
drawWiFiConfigStartScreen() [Magenta]
    ↓
Wait for BLE connection (timeout: 60s)
    ↓
drawBLEWaitingScreen() [Magenta, animated]
    ↓
BLE Phone App connects
    ↓
drawBLEConnectedScreen() [Green]
    ↓
User enters WiFi credentials in app
    ↓
Credentials received via BLE
    ↓
drawCredentialsReceivedScreen() [Magenta]
    ↓
Connect to WiFi (timeout: 30s)
    ↓
drawWiFiConnectingScreen() [Magenta, progress bar]
    ↓
WiFi Connected ✓
    ↓
drawWiFiConnectSuccessScreen() [Green, 3s countdown]
    ↓
Auto-return to HOME Screen
```

Or if error occurs:
```
WiFi Connection Failed ✗
    ↓
drawWiFiConnectErrorScreen() [Red]
    ↓
User can hold button again to retry
```

Or if BLE timeout:
```
No BLE connection (60s passed)
    ↓
drawBLEErrorScreen() [Red]
    ↓
Instructions to retry
    ↓
Auto-return to HOME after 10s
```
