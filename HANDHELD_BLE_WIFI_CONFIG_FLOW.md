# WiFi Configuration via BLE Flow - Handheld Device

## Tổng quan

Thiết bị handheld sử dụng BLE (Bluetooth Low Energy) để nhận WiFi credentials từ ứng dụng di động KAGRI App, cho phép người dùng cấu hình WiFi thông qua Bluetooth thay vì phải kết nối WiFi trực tiếp.

## Luồng hoạt động (Flow)

### 1. **Kích hoạt chế độ cấu hình WiFi**
- Người dùng **nhấn nút 5 giây** trên màn hình HOME
- Thiết bị chuyển sang state `WIFI_CONFIG`
- **BLE bắt đầu quảng bá** (Advertising) với tên: `KAGRI_HHC-XXYY` (XXYY là 4 ký tự cuối của MAC address)

### 2. **Scan thiết bị từ ứng dụng di động**
- Mở KAGRI App trên điện thoại
- App hiển thị prompt "Enable Bluetooth" nếu chưa bật
- App quét (scan) các thiết bị BLE xung quanh
- Người dùng thấy thiết bị có tên `KAGRI_HHC-XXYY` (ví dụ: `KAGRI_HHC-AB12`)
- Người dùng **chọn thiết bị** để kết nối

### 3. **Hiển thị hướng dẫn trên thiết bị**
- Màn hình thiết bị hiển thị **"WiFi Config BLE"** screen với:
  - Header màu MAGENTA
  - Biểu tượng BLE (sóng Bluetooth)
  - **Tên thiết bị cần scan**: `KAGRI_HHC-XXYY`
  - Hướng dẫn:
    1. Mở KAGRI App
    2. Bật Bluetooth
    3. Scan & chọn thiết bị

### 4. **Mobile App kết nối BLE**
- App kết nối đến service UUID: `0000ffb0-0000-1000-8000-00805f9b34fb`
- App hiển thị form nhập:
  - SSID (tên WiFi network)
  - Password (mật khẩu WiFi)
  - UserUID (ID người dùng Firebase)
- Người dùng điền thông tin và nhấn **"Gửi"** (Send)

### 5. **Thiết bị nhận WiFi Credentials**
- App gửi JSON qua Bluetooth:
  ```json
  {
    "ssid": "MyWiFi",
    "password": "MyPassword123",
    "userUID": "user123@firebase"
  }
  ```
- Thiết bị nhận qua Characteristic `0000ffb1-0000-1000-8000-00805f9b34fb`
- Callback `onWiFiCredentialsReceived()` được gọi với dữ liệu

### 6. **Hiển thị xác nhận trên thiết bị**
- Màn hình chuyển sang **"Credentials Received"** screen
- Hiển thị:
  - SSID vừa nhận
  - Độ mạnh tín hiệu WiFi
  - Thông báo "Kết nối WiFi..."

### 7. **Lưu trữ & Kết nối WiFi** (TODO)
- Thiết bị lưu credentials vào NVS (Non-Volatile Storage)
- Bắt đầu kết nối đến WiFi network
- Hiển thị progress screen

### 8. **Kết thúc hoặc Timeout**
- **Thành công**: Hiển thị "WiFi Connected" screen với địa chỉ IP
- **Thất bại**: Hiển thị "WiFi Error" screen với thông báo lỗi
- **Timeout**: Nếu không nhận được credentials trong 60 giây, tự động quay về HOME screen

## Hardware & Technical Details

### BLE Configuration
- **Library**: NimBLE-Arduino v1.4.2 (h2zero/NimBLE-Arduino@^1.4.2)
- **Device Name**: `KAGRI_HHC-{MAC_LAST_2_BYTES}` (ví dụ: KAGRI_HHC-AB12)
- **Service UUID**: 0000ffb0-0000-1000-8000-00805f9b34fb
- **Characteristics**:
  - Command (Write): 0000ffb1-0000-1000-8000-00805f9b34fb
  - Response (Notify): 0000ffb2-0000-1000-8000-00805f9b34fb
- **MTU**: 512 bytes (cho payloads lớn)

### File Components

**ble_provisioning.h / ble_provisioning.cpp**
- BleProvisioning class
- Quản lý BLE Server, Characteristics, Callbacks
- Nhận JSON từ mobile app
- Callback khi có credentials

**handheld_app.h / handheld_app.cpp**
- Khởi tạo BLE provisioning trong `initializeComponents()`
- Kích hoạt BLE khi vào state WIFI_CONFIG
- Tắt BLE khi rời WIFI_CONFIG
- Callback `onWiFiCredentialsReceived()` xử lý credentials

**tft_display_manager_lvgl.cpp**
- `drawWiFiConfigStartScreen()` - Hiển thị tên BLE KAGRI_HHC-XXYY
- `drawBLEWaitingScreen()` - Màn chờ kết nối với countdown
- `drawCredentialsReceivedScreen()` - Xác nhận nhận được credentials
- `drawWiFiConnectingScreen()` - Khi đang kết nối WiFi
- `drawWiFiConnectSuccessScreen()` - Thành công kết nối
- `drawWiFiConnectErrorScreen()` - Lỗi kết nối

## State Machine

```
HOME (IDLE) 
    ↓ (Button 5s)
WIFI_CONFIG 
    ↓ (BLE Start)
    [Advertising: KAGRI_HHC-XXYY]
    ↓ (Mobile App Connect)
    [Display: Waiting screen with countdown]
    ↓ (Credentials received)
    [Display: Credentials Received]
    ↓ (60s timeout or user manual return)
    [Display: Home screen]
    ↓
HOME (IDLE)
```

## Timeouts & Durations

- **BLE Start**: Ngay khi vào WIFI_CONFIG
- **WIFI_CONFIG Timeout**: 60 giây (60000ms)
- **Screen Update**: Mỗi 1 giây
- **Debug Log**: Mỗi 5 giây

## Verification Checklist

- [x] BLE provisioning component tạo
- [x] NimBLE library thêm vào platformio.ini
- [x] BLE khởi tạo trong initializeComponents()
- [x] BLE start/stop khi vào/rời WIFI_CONFIG
- [x] Display screen cập nhật hiển thị tên BLE KAGRI_HHC
- [x] Firmware build thành công
- [ ] Device test: Scan KAGRI_HHC-XXYY trên ứng dụng
- [ ] Device test: Nhận credentials và hiển thị xác nhận
- [ ] WiFi connection implementation (TODO)
- [ ] Error handling & recovery (TODO)

## Next Steps

1. **Mobile App Integration**: Cập nhật KAGRI App để gửi credentials JSON
2. **WiFi Connection**: Implement logic kết nối WiFi từ credentials
3. **NVS Storage**: Lưu trữ credentials an toàn
4. **Error Handling**: Xử lý các lỗi kết nối
5. **Testing**: Full end-to-end test với mobile app

## Debug & Troubleshooting

### Xem BLE advertise
```
UART log sẽ show:
[BLEProvHHC] BLE provisioning started. Device name: KAGRI_HHC-AB12
[BLEProvHHC] Mobile app should scan for device named: KAGRI_HHC-AB12
```

### Xem credentials nhận
```
[BLEProvHHC] Received provisioning data: XX bytes
[BLEProvHHC] Provisioning data received:
  SSID: MyWiFi
  UserUID: user123@firebase
  Password: ***
```

### BLE Connection Issues
- Kiểm tra Bluetooth bật trên điện thoại
- Kiểm tra app permissions (BLE access)
- Đảm bảo device name match: KAGRI_HHC-XXYY
- Kiểm tra MTU=512 có support trên mobile app

---

**Cập nhật lần cuối**: 27/11/2025  
**Trạng thái**: BLE infrastructure ready, WiFi connection pending
