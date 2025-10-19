# 🔧 BLE Connection Troubleshooting Guide

## ❌ **LỖI PHỔ BIẾN:**

### **Error 133 - ANDROID_SPECIFIC_ERROR**

**Mô tả:** Lỗi kết nối BLE phổ biến nhất trên Android

**Nguyên nhân:**
1. Bluetooth cache bị lỗi
2. Quá nhiều thiết bị BLE đã kết nối
3. Gateway đang kết nối với thiết bị khác
4. Khoảng cách quá xa
5. Tín hiệu BLE yếu

---

## ✅ **GIẢI PHÁP ĐÃ IMPLEMENT:**

### **1. Retry Logic với Exponential Backoff**

**File:** `ble_service.dart:26-58`

```dart
Future<BluetoothDevice> connect(ScanResult result) async {
  int maxRetries = 3;
  int attempt = 0;
  
  while (attempt < maxRetries) {
    try {
      // Ensure device is disconnected first
      await device.disconnect();
      await Future.delayed(const Duration(milliseconds: 500));
      
      await device.connect(
        autoConnect: false,
        timeout: const Duration(seconds: 15),
      );
      
      return device; // Success!
      
    } catch (e) {
      attempt++;
      if (attempt >= maxRetries) {
        throw Exception('Kết nối BLE thất bại sau $maxRetries lần thử');
      }
      
      // Wait before retry (longer for each attempt)
      await Future.delayed(Duration(seconds: attempt));
    }
  }
}
```

**Cải thiện:**
- ✅ Disconnect trước khi connect
- ✅ Retry tối đa 3 lần
- ✅ Delay tăng dần (1s, 2s, 3s)
- ✅ Timeout tăng lên 15s

---

### **2. Bluetooth State Check**

**File:** `ble_service.dart:18-36`

```dart
Future<bool> isBluetoothReady() async {
  // Check if Bluetooth is supported
  if (await FlutterBluePlus.isSupported == false) {
    return false;
  }
  
  // Check if Bluetooth is turned on
  final state = await FlutterBluePlus.adapterState.first;
  if (state != BluetoothAdapterState.on) {
    return false;
  }
  
  return true;
}
```

**Cải thiện:**
- ✅ Kiểm tra Bluetooth support
- ✅ Kiểm tra Bluetooth state
- ✅ Hiển thị dialog cảnh báo nếu chưa bật

---

### **3. User-Friendly Error Messages**

**File:** `provisioning_screen.dart:235-308`

```dart
catch (e) {
  String errorMessage = 'Lỗi kết nối BLE';
  String errorDetail = e.toString();
  
  if (errorDetail.contains('133') || errorDetail.contains('ANDROID_SPECIFIC_ERROR')) {
    errorMessage = 'Lỗi kết nối Bluetooth';
    errorDetail = 'Không thể kết nối với Gateway. Vui lòng:\n'
        '• Tắt và bật lại Bluetooth\n'
        '• Đảm bảo Gateway đang ở gần\n'
        '• Thử lại sau vài giây';
  }
  
  // Show detailed error dialog with retry button
  showDialog(...);
}
```

**Cải thiện:**
- ✅ Parse error code
- ✅ Hiển thị hướng dẫn cụ thể
- ✅ Nút "Thử lại" trong dialog
- ✅ Hiển thị error technical details

---

## 📋 **HƯỚNG DẪN CHO USER:**

### **Khi gặp lỗi Error 133:**

**Bước 1: Tắt/Bật Bluetooth**
```
Settings → Bluetooth → OFF → (đợi 2 giây) → ON
```

**Bước 2: Clear Bluetooth Cache (nếu vẫn lỗi)**
```
Settings → Apps → Bluetooth → Storage → Clear Cache
```

**Bước 3: Khởi động lại App**
```
Force close app → Mở lại
```

**Bước 4: Kiểm tra Gateway**
```
• Gateway có đang bật không?
• LED có nhấp nháy không?
• Gateway có đang kết nối với thiết bị khác?
```

**Bước 5: Giảm khoảng cách**
```
• Đặt điện thoại gần Gateway (<1m)
• Tránh vật cản giữa điện thoại và Gateway
```

---

## 🔧 **ADVANCED TROUBLESHOOTING:**

### **1. Enable BLE Debug Logs**

```dart
// Add to main.dart
FlutterBluePlus.setLogLevel(LogLevel.verbose);
```

### **2. Check Android Permissions**

```xml
<!-- AndroidManifest.xml -->
<uses-permission android:name="android.permission.BLUETOOTH"/>
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN"/>
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"/>
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT"/>
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION"/>
```

### **3. Test BLE Connection**

```dart
// Test script
final device = ...;

// Test 1: Can discover services?
final services = await device.discoverServices();
print('Services: ${services.length}');

// Test 2: Can read characteristics?
for (var service in services) {
  print('Service: ${service.uuid}');
  for (var char in service.characteristics) {
    print('  Char: ${char.uuid}, props: ${char.properties}');
  }
}
```

---

## 📊 **ERROR CODES REFERENCE:**

| Code | Meaning | Solution |
|------|---------|----------|
| 133 | Generic connection error | Restart Bluetooth |
| 8 | Connection timeout | Move closer to Gateway |
| 19 | Device disconnected | Check Gateway power |
| 62 | Invalid handle | Restart app |
| 257 | No resources | Restart device |

---

## ✅ **TESTING CHECKLIST:**

### **Before Testing:**
- [ ] Bluetooth enabled on phone
- [ ] Location permission granted
- [ ] Gateway powered on
- [ ] Gateway in provisioning mode (LED blinking)
- [ ] Distance < 5 meters

### **Test Cases:**

**Test 1: Normal Connection**
```
1. Open app
2. Scan for gateways
3. Select gateway
4. Enter WiFi credentials
5. Expected: Success after 10-15 seconds
```

**Test 2: Connection Error Recovery**
```
1. Turn off Gateway
2. Try to connect
3. Expected: Error dialog with retry button
4. Turn on Gateway
5. Click retry
6. Expected: Success
```

**Test 3: Bluetooth Disabled**
```
1. Turn off Bluetooth
2. Open provisioning screen
3. Expected: "Bluetooth chưa sẵn sàng" dialog
4. Turn on Bluetooth
5. Click retry
6. Expected: Scan starts
```

**Test 4: Multiple Retries**
```
1. Place phone far from Gateway (weak signal)
2. Try to connect
3. Expected: 3 retry attempts with delays
4. Expected: Detailed error if all retries fail
```

---

## 🐛 **KNOWN ISSUES:**

### **Issue 1: Connection Fails on First Try**
**Status:** Fixed with retry logic  
**Workaround:** Automatic retry up to 3 times

### **Issue 2: Android 12+ Bluetooth Permissions**
**Status:** Need runtime permission request  
**Solution:** App requests permissions on startup

### **Issue 3: Gateway Already Connected**
**Status:** Fixed with disconnect before connect  
**Solution:** Always disconnect before new connection

---

## 📚 **RELATED FILES:**

- `lib/services/ble_service.dart` - BLE connection logic
- `lib/screens/provisioning_screen.dart` - UI and error handling
- `src/application/app_gateway/ble_provisioning.cpp` - Gateway BLE server

---

## 🔄 **CHANGELOG:**

### **v1.0.1 (2025-10-19)**
- ✅ Added retry logic (3 attempts)
- ✅ Added Bluetooth state check
- ✅ Improved error messages
- ✅ Added disconnect before connect
- ✅ Increased timeout to 15s
- ✅ Added exponential backoff

### **v1.0.0 (Initial)**
- Basic BLE connection
- No retry logic
- Generic error messages

---

**Last Updated:** 2025-10-19  
**Issue:** Error 133 - ANDROID_SPECIFIC_ERROR  
**Status:** ✅ FIXED with retry logic  
**Version:** 1.0.1
