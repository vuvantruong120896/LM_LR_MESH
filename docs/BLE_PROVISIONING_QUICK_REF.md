# 🔐 BLE Provisioning - Quick Reference

## ✅ TRẢ LỜI NHANH

1. **App có cấp WiFi qua BLE?**  
   → ✅ **YES** - SSID + Password qua BLE

2. **UID từ BLE hay Firebase?**  
   → ✅ **QUA BLE** - App gửi UID (từ Firebase Auth) qua BLE cho Gateway

3. **Gateway sinh netkey từ UID + MAC?**  
   → ✅ **YES** - `Netkey = SHA256(UID + MAC)`[:16 bytes]

---

## 🔄 LUỒNG ĐƠN GIẢN

```
┌─────────┐                              ┌─────────┐
│   App   │                              │ Gateway │
└────┬────┘                              └────┬────┘
     │                                        │
     │ 1. Get UID from Firebase Auth         │
     │    (user logged in)                   │
     │                                        │
     │ 2. BLE Connect                         │
     │───────────────────────────────────────>│
     │                                        │
     │ 3. Send JSON:                          │
     │    {ssid, password, userUID}           │
     │───────────────────────────────────────>│
     │                                        │
     │                                        │ 4. Get MAC address
     │                                        │    esp_read_mac()
     │                                        │
     │                                        │ 5. Derive Netkey
     │                                        │    SHA256(uid|mac)
     │                                        │
     │                                        │ 6. Save to NVS:
     │                                        │    - wifi_ssid
     │                                        │    - wifi_pass
     │                                        │    - user_uid
     │                                        │    - netkey
     │                                        │
     │ 7. Response: success                   │
     │<───────────────────────────────────────│
     │                                        │
     │                                        │ 8. Restart
     │                                        │
     │                                        │ 9. Load from NVS
     │                                        │ 10. Connect WiFi
     │                                        │ 11. Init Firebase
     │                                        │     with UID + MAC
     │                                        │
```

---

## 🔑 NETKEY DERIVATION

```cpp
// Input
String userUID = "abc123xyz";         // From App (BLE)
uint8_t mac[6] = {0xA4, 0xCF, ...};  // From esp_read_mac()

// Format
String input = "abc123xyz|A4:CF:12:34:56:78";

// SHA256
uint8_t hash[32] = SHA256(input);

// Netkey (first 16 bytes)
uint8_t netkey[16] = hash[0..15];
```

**Example:**
```
Input:  "abc123xyz|A4:CF:12:34:56:78"
SHA256: 8f3a2b1c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a
Netkey: 8f3a2b1c4d5e6f7a8b9c0d1e2f3a4b5c (128-bit)
```

---

## 📦 NVS STORAGE

```
Namespace: "kagri_prov"

Keys:
┌──────────────┬────────────────────┐
│ Key          │ Value              │
├──────────────┼────────────────────┤
│ provisioned  │ true               │
│ wifi_ssid    │ "MyWiFi"           │
│ wifi_pass    │ "12345678"         │
│ user_uid     │ "abc123xyz"        │ ← From BLE
│ netkey       │ [16 bytes]         │ ← Derived
└──────────────┴────────────────────┘
```

---

## 🎯 KEY POINTS

✅ **UID từ App qua BLE** - KHÔNG phải từ Firebase  
✅ **Gateway tự sinh netkey** - SHA256(UID + MAC)  
✅ **Netkey không bao giờ truyền qua BLE**  
✅ **Deterministic** - Same UID + Same MAC = Same Netkey  

---

## 📝 CODE REFERENCES

**Mobile App:**
```dart
// lib/services/ble_service.dart:48-52
final uid = AuthService().currentUserUID;  // ← Get UID from Firebase Auth

final payload = jsonEncode({
  'ssid': ssid,
  'password': password,
  'userUID': uid,  // ← Send via BLE
});
```

**Gateway:**
```cpp
// provision_manager.cpp:52-59
void handleProvisionData(const ProvisionData& data) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);  // ← Get MAC
    
    uint8_t netkey[16];
    CryptoUtils::deriveNetkey(data.userUID, mac, netkey);  // ← Derive netkey
    
    saveProvisionData(..., netkey);  // ← Save to NVS
}
```

---

**See full documentation:** `BLE_PROVISIONING_FLOW.md`
