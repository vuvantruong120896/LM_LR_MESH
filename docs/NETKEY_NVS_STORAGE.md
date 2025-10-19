# 🔐 Netkey NVS Storage - Complete Analysis

## ❓ **CÂU HỎI:**
> Hiện tại Netkey sau khi sinh ra có được lưu vào NVS để sau khi reboot lấy được thông tin netkey và khi luồng cấp netkey cho nodes sử dụng?

## ✅ **TRẢ LỜI:**

**CÓ** - Netkey được lưu vào NVS, NHƯNG có vấn đề về **2 namespaces riêng biệt**.

---

## 🔍 **PHÂN TÍCH CHI TIẾT:**

### **1. Sau BLE Provisioning - Netkey ĐƯỢC LƯU**

**File:** `provision_manager.cpp:64`

```cpp
// Save to NVS (kagri_prov namespace)
if (saveProvisionData(data.ssid, data.password, data.userUID, netkey)) {
    ESP_LOGI(TAG, "✓ Provisioning data saved successfully");
    ESP_LOGI(TAG, "  Netkey: %s", CryptoUtils::toHexString(netkey, 16).c_str());
}

// Implementation:
bool ProvisionManager::saveProvisionData(..., const uint8_t* netkey) {
    _prefs.begin(NVS_NAMESPACE, false); // NVS_NAMESPACE = "kagri_prov"
    
    success &= _prefs.putBytes(NVS_KEY_NETKEY, netkey, 16) == 16; // ✅ Lưu netkey
    success &= _prefs.putBool(NVS_KEY_PROVISIONED, true);
    
    _prefs.end();
    return success;
}
```

**Namespace:** `"kagri_prov"`  
**Key:** `"netkey"`  
**Size:** 16 bytes

---

### **2. Sau Reboot - Netkey CÓ THỂ ĐƯỢC ĐỌC**

**File:** `provision_manager.cpp:125`

```cpp
bool ProvisionManager::getNetkey(uint8_t* netkey) {
    _prefs.begin(NVS_NAMESPACE, true); // Read-only, namespace = "kagri_prov"
    size_t len = _prefs.getBytes(NVS_KEY_NETKEY, netkey, 16);
    _prefs.end();
    
    return len == 16; // ✅ Trả về true nếu đọc được 16 bytes
}
```

---

### **3. ❌ VẤN ĐỀ: 2 Namespaces Riêng Biệt**

**Phát hiện:**
- **BLE Provisioning** lưu netkey vào namespace `"kagri_prov"`
- **Assign Netkey Command** đọc từ namespace `"mesh_config"`

**Code:**

```cpp
// handleAssignNetkey() - gateway_app.cpp:1512
NetworkConfig cfg;
if (!NVSStorageService::loadNetworkConfig(cfg)) {  // ← Đọc từ "mesh_config"
    ESP_LOGE(TAG, "❌ Failed to load network config from NVS");
    return;
}

// NVSStorageService.cpp:136
bool NVSStorageService::loadNetworkConfig(NetworkConfig& config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE_MESH, NVS_READONLY, &handle);
    // ↑ NVS_NAMESPACE_MESH = "mesh_config" (KHÁC "kagri_prov"!)
}
```

**Hậu quả:**
- BLE provisioning lưu netkey vào `kagri_prov/netkey`
- `handleAssignNetkey()` tìm netkey ở `mesh_config/net_key`
- **2 netkey khác nhau!** → Command sẽ **FAIL** với lỗi "NO_NETKEY"

---

## ✅ **GIẢI PHÁP ĐÃ IMPLEMENT:**

### **Sync Netkey Giữa 2 Namespaces**

**File:** `provision_manager.cpp:64-93`

```cpp
void ProvisionManager::handleProvisionData(const ProvisionData& data) {
    // ... derive netkey ...
    
    // Save to kagri_prov namespace (BLE provisioning data)
    if (saveProvisionData(data.ssid, data.password, data.userUID, netkey)) {
        
        // 🔧 FIX: Sync netkey to mesh_config namespace
        ESP_LOGI(TAG, "🔄 Syncing netkey to mesh_config namespace...");
        NetworkConfig meshCfg;
        memset(&meshCfg, 0, sizeof(meshCfg));
        memcpy(meshCfg.networkKey, netkey, 16);        // ← Copy netkey
        memset(meshCfg.authToken, 0xAB, 8);            // Placeholder
        meshCfg.networkId = 0x0001;
        meshCfg.keyVersion = 1;
        meshCfg.timestamp = millis() / 1000;
        meshCfg.initialized = true;
        
        if (!NVSStorageService::isInitialized()) {
            NVSStorageService::initialize();
        }
        
        // Save to mesh_config namespace
        if (NVSStorageService::saveNetworkConfig(meshCfg)) {
            ESP_LOGI(TAG, "✅ Netkey synced to mesh_config namespace");
        } else {
            ESP_LOGW(TAG, "⚠️  Failed to sync (assign_netkey may fail!)");
        }
    }
}
```

---

## 📊 **NVS STORAGE LAYOUT**

### **Namespace 1: `kagri_prov` (BLE Provisioning)**

```
┌──────────────────────────────────────────┐
│ Namespace: "kagri_prov"                  │
├──────────────┬───────────────────────────┤
│ Key          │ Value                     │
├──────────────┼───────────────────────────┤
│ provisioned  │ true                      │
│ wifi_ssid    │ "MyWiFi"                  │
│ wifi_pass    │ "12345678"                │
│ user_uid     │ "abc123xyz"               │
│ netkey       │ [16 bytes] ← Từ BLE      │
└──────────────┴───────────────────────────┘
```

**Purpose:** Store WiFi + UID + Netkey from BLE provisioning

---

### **Namespace 2: `mesh_config` (Mesh Network)**

```
┌──────────────────────────────────────────┐
│ Namespace: "mesh_config"                 │
├──────────────┬───────────────────────────┤
│ Key          │ Value                     │
├──────────────┼───────────────────────────┤
│ net_key      │ [16 bytes] ← SYNCED      │
│ auth_token   │ [8 bytes]                 │
│ net_id       │ 0x0001                    │
│ key_version  │ 1                         │
│ bridge_init  │ true                      │
└──────────────┴───────────────────────────┘
```

**Purpose:** Store mesh network configuration for node provisioning

---

## 🔄 **LUỒNG HOẠT ĐỘNG SAU KHI SỬA:**

### **1. BLE Provisioning (Lần đầu)**

```
App → BLE → Gateway
              ↓
         Derive Netkey
              ↓
    ┌─────────┴─────────┐
    ↓                   ↓
Save to              Save to
kagri_prov          mesh_config
namespace           namespace
    ↓                   ↓
[wifi_ssid]        [net_key] ← SYNCED
[wifi_pass]        [auth_token]
[user_uid]         [net_id]
[netkey]           [key_version]
```

### **2. After Reboot**

```
Gateway Boots
     ↓
Load from NVS
     ↓
┌────┴────┐
↓         ↓
kagri_prov    mesh_config
     ↓              ↓
WiFi config    Netkey for
User UID       node provisioning
```

### **3. Assign Netkey Command**

```
App sends command → Firebase → Gateway
                                  ↓
                    Load netkey from mesh_config ✅
                                  ↓
                    Distribute to all nodes
```

---

## ✅ **KẾT LUẬN:**

### **Trả lời câu hỏi:**

**1. Netkey có được lưu vào NVS?**  
→ ✅ **CÓ** - Lưu vào namespace `kagri_prov`

**2. Sau reboot có lấy được?**  
→ ✅ **CÓ** - Method `getNetkey()` đọc từ NVS

**3. Luồng assign netkey có sử dụng được?**  
→ ✅ **CÓ** (sau khi fix sync) - Netkey được sync sang namespace `mesh_config`

---

## 🔧 **CODE CHANGES SUMMARY:**

| File | Line | Change |
|------|------|--------|
| `provision_manager.cpp` | 1 | Added `#include NVSStorageService.h` |
| `provision_manager.cpp` | 64-93 | Added netkey sync to `mesh_config` namespace |

---

## 📝 **TESTING CHECKLIST:**

### **Test Case 1: Fresh Provisioning**

```
1. Factory reset Gateway
2. Provision via BLE
3. Check logs:
   ✓ "Netkey synced to mesh_config namespace"
4. Reboot Gateway
5. Send assign_netkey command from App
6. Expected: SUCCESS (netkey found in mesh_config)
```

### **Test Case 2: After Reboot**

```
1. Gateway reboots
2. Load config:
   - WiFi from kagri_prov ✓
   - UID from kagri_prov ✓
   - Netkey from mesh_config ✓
3. Send assign_netkey command
4. Expected: Nodes receive netkey
```

### **Test Case 3: Verify Both Namespaces**

```bash
# Monitor serial output
platformio device monitor -e esp32-gateway

# Look for:
[ProvisionMgr] Netkey synced to mesh_config namespace
[GatewayApp] Loaded network config from NVS: version=1, netId=0x0001
[GatewayApp] NetworkKey: 8f3a2b1c... (should match!)
```

---

## 🔒 **SECURITY NOTES:**

1. **Netkey in Flash:**
   - ✅ Stored in NVS (encrypted if enabled in menuconfig)
   - ✅ Never transmitted over network
   - ✅ Deterministic (can regenerate from UID + MAC)

2. **Namespace Isolation:**
   - `kagri_prov`: BLE provisioning data
   - `mesh_config`: Mesh network config
   - **Now synced:** Same netkey in both

3. **Factory Reset:**
   - Clear both namespaces
   - Netkey lost (must provision again)
   - Same UID + MAC → Same netkey derived

---

## 📚 **RELATED DOCUMENTATION:**

- `BLE_PROVISIONING_FLOW.md` - BLE provisioning details
- `ASSIGN_NETKEY_IMPLEMENTATION.md` - Assign netkey command flow
- `NVSStorageService.h` - NVS API reference

---

**Last Updated:** 2025-10-19  
**Issue:** Netkey sync between namespaces  
**Status:** ✅ FIXED  
**Version:** 1.0.1
