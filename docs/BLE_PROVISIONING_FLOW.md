# 🔐 BLE Gateway Provisioning Flow - Complete Analysis

## 📋 Tổng quan

Tài liệu này mô tả chi tiết luồng provisioning Gateway qua BLE từ Mobile App.

---

## ✅ TRẢ LỜI CÁC CÂU HỎI:

### 1️⃣ **App có cấp WiFi cho Gateway qua BLE?**
✅ **ĐÚNG** - App gửi WiFi credentials qua BLE

**Chi tiết:**
```dart
// Mobile App (ble_service.dart)
final payload = jsonEncode({
  'ssid': ssid,           // ✅ WiFi SSID
  'password': password,   // ✅ WiFi Password
  'userUID': uid,         // ✅ User UID
});
```

**Gateway nhận qua BLE:**
```cpp
// Gateway (ble_provisioning.cpp)
data.ssid = doc["ssid"] | "";
data.password = doc["password"] | "";
data.userUID = doc["userUID"] | "";
```

---

### 2️⃣ **Gateway có được cấp UID từ App qua BLE hay từ Firebase?**
✅ **QUA BLE** - App gửi UID trực tiếp qua BLE, **KHÔNG PHẢI** từ Firebase

**Chi tiết:**
```dart
// Mobile App lấy UID từ Firebase Auth
final uid = AuthService().currentUserUID;

// Gửi qua BLE
final payload = jsonEncode({
  'userUID': uid,  // ✅ UID của user đang đăng nhập
});
```

**Gateway lưu vào NVS:**
```cpp
// provision_manager.cpp
_prefs.putString(NVS_KEY_USER_UID, userUID);
```

**Sau đó Gateway dùng UID này cho:**
- Firebase paths: `users/{uid}/commands/{mac}/`
- Netkey derivation: `SHA256(uid + mac)`

---

### 3️⃣ **Gateway có sinh Netkey từ UID và MAC của chính nó?**
✅ **ĐÚNG** - Gateway tự sinh netkey từ `SHA256(UID + MAC)`

**Chi tiết:**

**a) Get Gateway MAC:**
```cpp
// crypto_utils.cpp
uint8_t mac[6];
esp_read_mac(mac, ESP_MAC_WIFI_STA);
```

**b) Derive Netkey:**
```cpp
// crypto_utils.cpp:7-36
bool CryptoUtils::deriveNetkey(const String& userUID, 
                               const uint8_t* gatewayMac, 
                               uint8_t* netkeyOut) {
    // Format: "userUID|AA:BB:CC:DD:EE:FF"
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             gatewayMac[0], gatewayMac[1], gatewayMac[2],
             gatewayMac[3], gatewayMac[4], gatewayMac[5]);
    
    String input = userUID + "|" + macStr;
    
    // Compute SHA-256
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char*)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    
    // Take first 16 bytes as netkey (128-bit AES key)
    memcpy(netkeyOut, hash, 16);
    
    return true;
}
```

**c) Example:**
```
Input:  "abc123xyz|A4:CF:12:34:56:78"
SHA256: 8f3a2b1c... (32 bytes)
Netkey: 8f3a2b1c4d5e6f7a... (first 16 bytes)
```

---

## 🔄 LUỒNG HOẠT ĐỘNG HOÀN CHỈNH

### **Phase 1: App Preparation**

```
┌─────────────┐
│ Mobile App  │
└──────┬──────┘
       │
       │ 1. User login Firebase
       │ 2. Get UID from AuthService
       │ 3. User enters WiFi credentials
       │ 4. Scan for Gateway via BLE
       │ 5. Find device "KAGRI-GW-XXXX"
       │
       v
```

### **Phase 2: BLE Connection**

```
┌─────────────┐         ┌─────────┐
│ Mobile App  │────────>│ Gateway │
└─────────────┘   BLE   └─────────┘
       │                      │
       │ Connect to device    │
       │ Discover services    │
       │ Find UUID: FFB0      │
       │ Write to UUID: FFB1  │
       │                      │
```

### **Phase 3: Data Transfer**

```
Mobile App sends JSON:
{
  "ssid": "MyWiFi",
  "password": "12345678",
  "userUID": "abc123xyz"
}

Gateway receives and validates:
✓ SSID not empty
✓ UserUID not empty
✓ Password (optional, can be empty)
```

### **Phase 4: Netkey Derivation**

```
┌─────────────────────────────────────┐
│ Gateway (provision_manager.cpp:52) │
└─────────────────┬───────────────────┘
                  │
    ┌─────────────v──────────────┐
    │ Get Gateway MAC address    │
    │ esp_read_mac(mac, ESP_MAC) │
    │ → A4:CF:12:34:56:78        │
    └─────────────┬──────────────┘
                  │
    ┌─────────────v──────────────────────┐
    │ CryptoUtils::deriveNetkey()        │
    │ Input: "abc123xyz|A4:CF:12:34..."  │
    │ SHA256 → 32 bytes                  │
    │ Take first 16 bytes → Netkey       │
    └─────────────┬──────────────────────┘
                  │
                  v
         Netkey: [16 bytes]
```

### **Phase 5: Save to NVS**

```
┌──────────────────────────────────────┐
│ NVS Storage (Flash memory)           │
├──────────────────────────────────────┤
│ Namespace: "kagri_prov"              │
│                                      │
│ Keys:                                │
│ - provisioned:  true                 │
│ - wifi_ssid:    "MyWiFi"             │
│ - wifi_pass:    "12345678"           │
│ - user_uid:     "abc123xyz"          │
│ - netkey:       [16 bytes]           │
└──────────────────────────────────────┘
```

### **Phase 6: Response & Restart**

```
Gateway → App (via BLE notify):
{
  "status": "success",
  "message": "Provisioning received"
}

Gateway internal:
- Set _provisionSucceeded = true
- Set _needsRestart = true
- Stop BLE
- Main loop detects flag
- LED blinks success pattern
- ESP_RESTART (after 3 seconds)
```

### **Phase 7: After Restart**

```
┌─────────┐
│ Gateway │
└────┬────┘
     │
     │ 1. Load provisioning data from NVS
     │ 2. Connect WiFi using saved credentials
     │ 3. Load userUID from NVS
     │ 4. Initialize Firebase with UID + MAC
     │ 5. Start command polling
     │ 6. Upload gateway info to Firebase
     │
     v
Firebase paths:
- users/{uid}/commands/{mac}/
- nodes/{uid}/{mac}/
- gateways/{uid}/{mac}/
```

---

## 📊 DATA FLOW DIAGRAM

```
┌─────────────────────────────────────────────────────────────┐
│                   BLE PROVISIONING FLOW                      │
└─────────────────────────────────────────────────────────────┘

   Mobile App                 Gateway                 NVS
       │                         │                     │
       │  1. Scan BLE            │                     │
       │────────────────────────>│                     │
       │                         │                     │
       │  2. Connect to device   │                     │
       │────────────────────────>│                     │
       │                         │                     │
       │  3. Write JSON:         │                     │
       │     {ssid, pass, uid}   │                     │
       │────────────────────────>│                     │
       │                         │                     │
       │                         │ 4. Get MAC address  │
       │                         │ esp_read_mac()      │
       │                         │                     │
       │                         │ 5. Derive netkey    │
       │                         │ SHA256(uid+mac)     │
       │                         │                     │
       │                         │ 6. Save all data    │
       │                         │────────────────────>│
       │                         │                     │
       │  7. Response: success   │                     │
       │<────────────────────────│                     │
       │                         │                     │
       │  8. Disconnect          │ 9. Restart ESP32    │
       │<────────────────────────│                     │
       │                         │                     │
       │                         │ 10. Load from NVS   │
       │                         │<────────────────────│
       │                         │                     │
       │                         │ 11. Connect WiFi    │
       │                         │ 12. Connect Firebase│
       │                         │ 13. Start operation │
       │                         │                     │
```

---

## 🔑 NETKEY SECURITY

### **Why derive netkey instead of receiving from App?**

1. **Security:** Netkey never transmitted over BLE (even encrypted BLE can be sniffed)
2. **Deterministic:** Same UID + MAC always produces same netkey
3. **Gateway Independence:** Gateway can regenerate netkey without App
4. **Firebase Safety:** Netkey never stored in Firebase database

### **What if user deletes App?**

No problem! As long as:
- User logs in with same Firebase account (same UID)
- Gateway has same MAC address
- Netkey will be identical

### **What if Gateway reset to factory?**

- All NVS data erased
- Must provision again via BLE
- Same UID + MAC → Same netkey derived
- Network keys match automatically

---

## 📝 CODE LOCATIONS

### **Mobile App (Flutter)**

| File | Purpose |
|------|---------|
| `lib/services/ble_service.dart` | BLE scanning, connection, provisioning |
| `lib/screens/provisioning_screen.dart` | UI for WiFi input and provisioning |
| `lib/services/auth_service.dart` | Get current user UID |

**Key method:**
```dart
Future<bool> provisionGateway({
  required BluetoothDevice device,
  required String ssid,
  required String password,
}) async {
  final uid = AuthService().currentUserUID;
  
  final payload = jsonEncode({
    'ssid': ssid,
    'password': password,
    'userUID': uid,  // ✅ UID from Firebase Auth
  });
  
  await commandChar.write(data);
}
```

### **Gateway Firmware (ESP32)**

| File | Purpose |
|------|---------|
| `src/application/app_gateway/ble_provisioning.cpp` | BLE server, handle incoming data |
| `src/application/app_gateway/provision_manager.cpp` | Process provision data, save to NVS |
| `src/application/app_gateway/crypto_utils.cpp` | Netkey derivation (SHA256) |
| `src/application/app_gateway/gateway_app.cpp` | Main app, load from NVS, Firebase init |

**Key flow:**
```cpp
// 1. BLE receives data
void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    JsonDocument doc;
    deserializeJson(doc, value);
    
    data.ssid = doc["ssid"];
    data.password = doc["password"];
    data.userUID = doc["userUID"];  // ✅ UID from App
    
    _parent->_provisionCallback(data);
}

// 2. Process provision data
void handleProvisionData(const ProvisionData& data) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    uint8_t netkey[16];
    CryptoUtils::deriveNetkey(data.userUID, mac, netkey);  // ✅ Generate netkey
    
    saveProvisionData(data.ssid, data.password, data.userUID, netkey);
    
    _needsRestart = true;
}

// 3. After restart - load and use
void GatewayApp::setup() {
    String userUID;
    provisionManager->getUserUID(userUID);  // ✅ Load from NVS
    
    firebaseClient->setUserContext(userUID, gatewayMAC);  // ✅ Use for Firebase paths
}
```

---

## 🧪 TESTING

### **Test Case 1: First Time Provisioning**

```
Prerequisites:
- Fresh Gateway (factory reset)
- Mobile App logged in

Steps:
1. App scans BLE → finds "KAGRI-GW-XXXX"
2. App connects to Gateway
3. User enters WiFi: SSID="TestWiFi", Pass="test1234"
4. App sends provision data
5. Gateway derives netkey
6. Gateway saves to NVS
7. Gateway responds success
8. Gateway restarts
9. Gateway connects WiFi
10. Gateway connects Firebase

Expected:
✅ Gateway online in Firebase
✅ Firebase path: users/{uid}/gateways/{mac}/
✅ Can send commands from App
```

### **Test Case 2: Re-provisioning (Factory Reset)**

```
Prerequisites:
- Gateway was provisioned before
- Factory reset executed

Steps:
1. Gateway boots → NVS empty → isProvisioned() = false
2. Gateway starts BLE advertising
3. App provisions again with SAME user account
4. Gateway derives netkey from SAME uid + SAME mac
5. Netkey identical to before

Expected:
✅ Netkey matches previous netkey
✅ Can communicate with old nodes (same network)
✅ Firebase paths remain consistent
```

### **Test Case 3: Different User**

```
Prerequisites:
- Gateway provisioned with UserA (uid="abc123")
- Factory reset
- Provision with UserB (uid="xyz789")

Steps:
1. App (UserB) provisions Gateway
2. Gateway derives NEW netkey (different UID)
3. Gateway uses NEW Firebase paths

Expected:
✅ Different netkey generated
✅ Firebase path: users/xyz789/gateways/{mac}/
✅ Old nodes need re-provisioning (different netkey)
```

---

## 🔒 SECURITY CONSIDERATIONS

### **1. BLE Security**

- ✅ Netkey **NEVER** transmitted over BLE
- ✅ Only WiFi credentials + UID sent
- ⚠️ BLE not encrypted by default (consider pairing for production)

### **2. WiFi Password**

- ⚠️ Stored in NVS plaintext (ESP32 limitation)
- ✅ NVS encrypted at rest if enabled in menuconfig
- Consider: `CONFIG_NVS_ENCRYPTION=y`

### **3. UserUID**

- ✅ Safe to transmit (public identifier)
- ✅ Used only for Firebase path scoping
- ✅ No sensitive data exposed

### **4. Netkey**

- ✅ Never leaves Gateway
- ✅ Derived deterministically (reproducible)
- ✅ 128-bit AES-grade security (SHA256 truncated)
- ✅ Stored in NVS (encrypted if enabled)

---

## 🚀 SUMMARY

| Question | Answer |
|----------|--------|
| **1. App cấp WiFi qua BLE?** | ✅ **YES** - SSID + Password qua BLE |
| **2. UID từ BLE hay Firebase?** | ✅ **BLE** - App gửi UID qua BLE, không lấy từ Firebase |
| **3. Gateway sinh netkey?** | ✅ **YES** - SHA256(UID + MAC) → 16 bytes |

**Architecture:**
```
App (UID from Auth) → BLE → Gateway → Derive Netkey → Save NVS → Restart → Firebase
```

**Key Insight:**
- Gateway **tự sinh** netkey, không nhận từ bên ngoài
- UID chỉ dùng để **derive** netkey và **scope** Firebase paths
- Netkey **deterministic**: Same UID + Same MAC = Same Netkey

---

**Last Updated:** 2025-10-19  
**Author:** GitHub Copilot  
**Version:** 1.0.0
