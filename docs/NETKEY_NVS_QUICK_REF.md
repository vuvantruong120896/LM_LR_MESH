# 🔐 Netkey NVS Storage - Quick Answer

## ❓ **Câu hỏi:**
> Netkey sau khi sinh ra có được lưu vào NVS để sau khi reboot lấy được thông tin netkey và khi luồng cấp netkey cho nodes sử dụng?

## ✅ **Trả lời:**

**CÓ** - Nhưng có 1 vấn đề đã được sửa.

---

## 📋 **TÓM TẮT:**

### **1. Netkey ĐƯỢC LƯU vào NVS**

```cpp
// provision_manager.cpp:95-101
bool saveProvisionData(..., const uint8_t* netkey) {
    _prefs.begin("kagri_prov", false);
    _prefs.putBytes("netkey", netkey, 16);  // ✅ Lưu 16 bytes
    _prefs.putBool("provisioned", true);
    _prefs.end();
}
```

**Lưu vào:** Namespace `"kagri_prov"`, Key `"netkey"`

---

### **2. Sau Reboot - Netkey CÓ THỂ ĐỌC**

```cpp
// provision_manager.cpp:125-130
bool getNetkey(uint8_t* netkey) {
    _prefs.begin("kagri_prov", true);
    size_t len = _prefs.getBytes("netkey", netkey, 16);
    _prefs.end();
    return len == 16;  // ✅ Đọc được 16 bytes
}
```

---

### **3. ❌ VẤN ĐỀ (ĐÃ SỬA):**

**Trước khi sửa:**
- BLE provisioning lưu vào namespace `"kagri_prov"`
- Assign netkey command đọc từ namespace `"mesh_config"`
- **2 namespace khác nhau** → Netkey không tìm thấy! ❌

**Sau khi sửa:**
```cpp
// provision_manager.cpp:70-90
// After saving to kagri_prov, SYNC to mesh_config
NetworkConfig meshCfg;
memcpy(meshCfg.networkKey, netkey, 16);  // Copy netkey
meshCfg.initialized = true;

NVSStorageService::saveNetworkConfig(meshCfg);  // ✅ Save to mesh_config
```

---

## 🔄 **LUỒNG HOÀN CHỈNH:**

```
BLE Provisioning
      ↓
Derive Netkey (SHA256)
      ↓
  ┌───┴───┐
  ↓       ↓
Save to Save to
kagri_prov  mesh_config  ← SYNCED!
      ↓       ↓
   Reboot  Assign Netkey Command
      ↓       ↓
Load WiFi  Load Netkey
Load UID   Distribute to Nodes
```

---

## ✅ **KẾT LUẬN:**

| Câu hỏi | Đáp án | Chi tiết |
|---------|--------|----------|
| **Lưu vào NVS?** | ✅ YES | Namespace `kagri_prov` + `mesh_config` |
| **Đọc sau reboot?** | ✅ YES | Method `getNetkey()` |
| **Dùng cho assign netkey?** | ✅ YES | Sau khi sync 2 namespaces |

---

**See full documentation:** `NETKEY_NVS_STORAGE.md`
