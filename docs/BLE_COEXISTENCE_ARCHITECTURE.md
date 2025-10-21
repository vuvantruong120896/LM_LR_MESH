# BLE Architecture: Tránh Xung Đột Provisioning vs Data Sync

**Ngày**: Oct 21, 2025  
**Mục Đích**: Đảm bảo 2 dịch vụ BLE hoạt động an toàn cùng nhau

---

## 🎯 HIỆN TRẠNG

### **BLE Provisioning (Hiện Tại)**

```cpp
// File: provision_manager.h/cpp
class ProvisionManager {
    void startProvisioningIfNeeded();
    void handleProvisioningEvents();
    
    // Current BLE Stack
    BLEServer* pServer;
    BLEService* pProvisionService;  // UUID: 550e8400-e29b-41d4-...
    
    // Characteristics
    BLECharacteristic* ssidChar;     // WiFi SSID (write)
    BLECharacteristic* passwordChar; // WiFi Password (write)
    BLECharacteristic* uidChar;      // User UID (write)
    BLECharacteristic* statusChar;   // Status (notify)
};

// Dùng thread BLE callback để handle provisioning events
```

### **Vấn Đề**

1. ✅ Provisioning dùng BLE server, app (Kagri) connect để setup WiFi/UID
2. ❌ Nếu add thêm BLE Data Sync vào cùng server → có thể conflict?
   - **Không conflict về technical** (separate characteristics)
   - **CÓ risk về UX** (app có thể nhầm send provisioning data sang sync characteristic)
   - **CÓ risk về resource** (nếu đồng thời provisioning + sync = overload)

---

## 🏗️ SOLUTION: COEXISTENCE DESIGN

### **Architecture**

```
┌─────────────────────────────────────────────────────────┐
│              BLE Server (Single Instance)               │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │  SERVICE 1: Provisioning                         │   │
│  │  UUID: 550e8400-e29b-41d4-a716-446655440000     │   │
│  │                                                  │   │
│  │  Characteristics:                                │   │
│  │  ├─ [550e8400-...-0001] WiFi SSID (RW)         │   │
│  │  ├─ [550e8400-...-0002] WiFi Password (RW)     │   │
│  │  ├─ [550e8400-...-0003] User UID (RW)          │   │
│  │  └─ [550e8400-...-0004] Status (RN)            │   │
│  │                                                  │   │
│  │  Handler: ProvisionManager::onCharWrite()      │   │
│  │  Thread: "BLE_Provision" (via callback)        │   │
│  └──────────────────────────────────────────────────┘   │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │  SERVICE 2: Data Sync (NEW)                      │   │
│  │  UUID: 660e8400-e29b-41d4-a716-446655440000    │   │
│  │                                                  │   │
│  │  Characteristics:                                │   │
│  │  ├─ [660e8400-...-0001] Sync Command (RW)      │   │
│  │  ├─ [660e8400-...-0002] Sync Response (RN)     │   │
│  │  └─ [660e8400-...-0003] Sync Status (RN)       │   │
│  │                                                  │   │
│  │  Handler: BleSyncService::onCommand()          │   │
│  │  Thread: "BLE_Sync" (via callback)             │   │
│  └──────────────────────────────────────────────────┘   │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### **Key Points**

1. **Single BLE Server** (reuse existing)
   - ✅ Already initialized by ProvisionManager
   - ✅ Simpler than multiple servers
   - ✅ App can connect once → use both services

2. **Two Independent Services**
   - ✅ Different UUIDs
   - ✅ Different characteristics
   - ✅ Different callback handlers
   - ❌ SHARE SAME BLE connection (potential issue)

3. **State Management**
   ```cpp
   class BleSyncService {
       static bool provisioning_active_;      // Block sync if provisioning
       static bool data_sync_active_;         // Block provisioning if syncing
       static uint32_t sync_start_time_;      // Timeout detection
       
       // Conflict check before each operation
       static bool canStartSync() {
           return !provisioning_active_ && (millis() - sync_start_time_ > 30000);
       }
   };
   ```

---

## 🔀 CONFLICT SCENARIOS & HANDLING

### **Scenario 1: User Provisioning, then wants to Sync**

```
Timeline:
T0:00  User opens Kagri App → App connects via BLE
T0:05  User clicks "WiFi Settings" → Starts provisioning flow
T0:10  Provisioning completes ✅ → Gateway reboots (!)
T1:00  (After reboot) User clicks "Sync Data"
T1:05  Sync starts → Works fine ✅

Decision: ✅ SEQUENTIAL - No problem
Reason: Provisioning triggers reboot, so no overlap
```

**Code**:
```cpp
// In ProvisionManager
void ProvisionManager::completeProvisioning() {
    // ... save WiFi/UID ...
    esp_restart();  // Reboot → all threads reset
}
```

---

### **Scenario 2: User in Provisioning, App tries Background Sync**

```
Timeline:
T0:00  User in Provisioning screen (WiFi change)
T0:05  Provisioning writes WiFi SSID to Gateway
T0:10  App (in background) tries to Sync Data via BLE
T0:15  Gateway gets Sync request → REJECT (provisioning active)
T0:20  App retry → Still rejected
T0:25  Provisioning completes → Reboot
T1:00  After reboot, Sync works ✅

Decision: ✅ BLOCK SYNC - Show message to user
Message: "Cannot sync while provisioning. Please wait..."
```

**Code**:
```cpp
// In BleSyncService::onSyncCommand()
if (ProvisionManager::isProvisioningActive()) {
    ESP_LOGW(TAG, "Provisioning active, reject sync request");
    sendErrorResponse(0x02);  // Error status code
    return;
}

// In App (Kagri):
Future<void> syncOfflineData() async {
    try {
        final response = await bleSync.getBufferedData(offset: 0);
        
        if (response.status == 0x02) {  // Error
            // Check if provisioning is ongoing
            showSnackBar("Gateway busy with provisioning. Try again later.");
            return;
        }
        
        // Process response
        processData(response);
    } catch (e) {
        showSnackBar("Sync failed: $e");
    }
}
```

---

### **Scenario 3: Provisioning During Sync (Edge Case)**

```
Timeline:
T0:00  Sync starts → App requests GET_BUFFERED_DATA (offset=0)
T0:02  Gateway sending 10 items...
T0:03  User clicks WiFi change → Starts provisioning
T0:04  ProvisionManager sets provisioning_active_ = true
T0:05  Gateway finishes BLE response (partial?)
T0:08  Provisioning completes → Reboot ✅

Decision: ✅ AUTO-RECOVERY via REBOOT
Reason: Provisioning always triggers reboot, so sync is abandoned
App: Should detect connection drop, show "Retry" button
```

**Code**:
```cpp
// In ProvisionManager::onUIDWrite()
void ProvisionManager::onUIDWrite(BLECharacteristic* pCharacteristic) {
    String uid = pCharacteristic->getValue().c_str();
    
    ESP_LOGI(TAG, "Provisioning: Setting UID = %s", uid.c_str());
    
    // Mark as active (blocks new sync)
    provisioning_active_ = true;
    
    // Save to NVS (may trigger reboot)
    saveProvisionData(uid);
    
    delay(500);
    esp_restart();  // Reboot → automatic recovery
}

// In App (detect connection drop)
// BLE characteristic notification drops → App shows "Connection lost"
```

---

## 🛡️ IMPLEMENTATION DETAILS

### **File: ble_sync_service.h**

```cpp
#pragma once

#include <BLEServer.h>
#include <string>
#include <vector>

/**
 * @brief BLE Data Sync Service
 * 
 * Handles offline sensor data sync via BLE
 * COEXISTS with ProvisionManager (separate service)
 * 
 * Conflict prevention:
 * - Checks if provisioning active before sync
 * - Does NOT block provisioning (provisioning auto-reboots)
 */
class BleSyncService {
public:
    // Lifecycle
    static bool init(BLEServer* bleServer);
    static void shutdown();
    
    // Data operations
    static std::vector<uint8_t> handleGetBufferedData(uint16_t offset, uint16_t maxCount);
    static bool handleClearBuffer(uint16_t count);
    
    // Status queries
    static bool isProvisioningActive();
    static uint16_t getPendingDataCount();
    static bool isSyncing();
    static uint8_t getLastErrorCode();
    
    // Callback (called by BLE characteristic write)
    static void onSyncCommandWrite(BLECharacteristic* pCharacteristic);
    
private:
    // State
    static bool initialized_;
    static uint8_t last_error_code_;
    static uint32_t sync_start_time_;
    static const uint32_t SYNC_TIMEOUT_MS = 30000;  // Max 30s per sync
    
    // Conflict detection
    static bool canProcessSync();
    static void handleConflict();
    
    // GATT
    static BLECharacteristic* command_char_;
    static BLECharacteristic* response_char_;
    static BLECharacteristic* status_char_;
};
```

### **File: ble_sync_service.cpp (Core Logic)**

```cpp
#include "ble_sync_service.h"
#include "provision_manager.h"
#include "offline_data_buffer.h"

bool BleSyncService::initialized_ = false;
uint8_t BleSyncService::last_error_code_ = 0x00;
uint32_t BleSyncService::sync_start_time_ = 0;
BLECharacteristic* BleSyncService::command_char_ = nullptr;
BLECharacteristic* BleSyncService::response_char_ = nullptr;
BLECharacteristic* BleSyncService::status_char_ = nullptr;

bool BleSyncService::init(BLEServer* bleServer) {
    if (!bleServer) {
        ESP_LOGE(TAG, "BleSyncService: BLE server is null");
        return false;
    }
    
    ESP_LOGI(TAG, "Initializing BLE Data Sync Service...");
    
    // Create Data Sync Service (different UUID from Provisioning)
    BLEService* pSyncService = bleServer->createService("660e8400-e29b-41d4-a716-446655440000");
    
    // Command characteristic (write from app)
    command_char_ = pSyncService->createCharacteristic(
        "660e8400-e29b-41d4-a716-446655440001",
        BLECharacteristic::PROPERTY_WRITE
    );
    command_char_->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);
    command_char_->setWriteProperty(true);
    command_char_->setCallbacks(new BleCommandCallback());  // Handle writes
    
    // Response characteristic (read/notify to app)
    response_char_ = pSyncService->createCharacteristic(
        "660e8400-e29b-41d4-a716-446655440002",
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    response_char_->setAccessPermissions(ESP_GATT_PERM_READ);
    
    // Status characteristic (notify progress)
    status_char_ = pSyncService->createCharacteristic(
        "660e8400-e29b-41d4-a716-446655440003",
        BLECharacteristic::PROPERTY_NOTIFY
    );
    status_char_->setAccessPermissions(ESP_GATT_PERM_READ);
    
    pSyncService->start();
    
    initialized_ = true;
    ESP_LOGI(TAG, "✅ BLE Data Sync Service initialized");
    return true;
}

bool BleSyncService::canProcessSync() {
    // Check 1: Provisioning is not active
    if (ProvisionManager::isProvisioningActive()) {
        ESP_LOGW(TAG, "BleSyncService: Provisioning active, cannot sync");
        last_error_code_ = 0x02;  // Error: Busy
        return false;
    }
    
    // Check 2: Previous sync not timed out
    if (sync_start_time_ > 0 && 
        (millis() - sync_start_time_) < SYNC_TIMEOUT_MS) {
        ESP_LOGW(TAG, "BleSyncService: Previous sync still in progress");
        last_error_code_ = 0x02;  // Error: Busy
        return false;
    }
    
    // Check 3: Offline buffer has data
    uint16_t count = OfflineDataBuffer::getBufferedCount();
    if (count == 0) {
        ESP_LOGI(TAG, "BleSyncService: No pending data");
        last_error_code_ = 0x00;  // OK, but no data
        return true;  // Still allow, just no data
    }
    
    return true;
}

std::vector<uint8_t> BleSyncService::handleGetBufferedData(
    uint16_t offset, 
    uint16_t maxCount) {
    
    // ============ Conflict Check ============
    if (!canProcessSync()) {
        // Return error response
        std::vector<uint8_t> response;
        response.push_back(last_error_code_);  // Status
        response.push_back(0x00);              // Count
        // ... rest of response ...
        return response;
    }
    
    // ============ Get Data from Buffer ============
    sync_start_time_ = millis();  // Mark sync start
    
    uint16_t count = OfflineDataBuffer::getBufferedCount();
    uint16_t actualCount = (offset + maxCount <= count) ? maxCount : (count - offset);
    
    // Build response
    std::vector<uint8_t> response;
    response.push_back(0x00);              // Status: OK
    response.push_back(actualCount);       // Count
    response.push_back((count >> 8) & 0xFF);      // Total (high byte)
    response.push_back(count & 0xFF);             // Total (low byte)
    
    // Add sensor data to response
    for (uint16_t i = 0; i < actualCount; i++) {
        String nodeId;
        sensorData data;
        if (OfflineDataBuffer::getDataAt(offset + i, nodeId, data)) {
            // Serialize to bytes and append
            // ... serialization logic ...
        }
    }
    
    // Calculate checksum
    uint16_t checksum = calculateCRC16(response);
    response.push_back((checksum >> 8) & 0xFF);
    response.push_back(checksum & 0xFF);
    
    ESP_LOGI(TAG, "BleSyncService: Sent %u items (offset %u, total %u)",
             actualCount, offset, count);
    
    return response;
}

void BleSyncService::onSyncCommandWrite(BLECharacteristic* pCharacteristic) {
    std::vector<uint8_t> command = pCharacteristic->getData();
    
    if (command.size() < 1) {
        ESP_LOGW(TAG, "BleSyncService: Invalid command size");
        return;
    }
    
    uint8_t cmd_id = command[0];
    
    if (cmd_id == 0x01) {  // GET_BUFFERED_DATA
        uint16_t offset = (command.size() > 1) ? 
            ((command[1] << 8) | command[2]) : 0;
        
        auto response = handleGetBufferedData(offset, 10);
        
        if (response_char_) {
            response_char_->setValue(response);
            response_char_->notify();
        }
        
    } else if (cmd_id == 0x02) {  // CLEAR_BUFFER
        bool success = handleClearBuffer(0);  // Clear all
        
        std::vector<uint8_t> response;
        response.push_back(success ? 0x00 : 0x02);  // Status
        
        if (response_char_) {
            response_char_->setValue(response);
            response_char_->notify();
        }
    }
}

// Helper
bool BleSyncService::isProvisioningActive() {
    return ProvisionManager::isProvisioningActive();
}
```

### **Integration: gateway_app.cpp**

```cpp
// In GatewayApp::setupFirebase()

void GatewayApp::setupFirebase() {
    // ... existing code ...
    
    // NEW: Initialize BLE Data Sync Service
    if (provisionManager && provisionManager->getBLEServer()) {
        if (BleSyncService::init(provisionManager->getBLEServer())) {
            ESP_LOGI(TAG, "✅ BLE Data Sync Service initialized");
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to initialize BLE Data Sync Service");
        }
    } else {
        ESP_LOGW(TAG, "⚠️ BLE Server not available, skipping Sync Service");
    }
}

// In GatewayApp::loop()

void GatewayApp::loop() {
    // ... existing code ...
    
    // Update status buffer every 10s (local only, no BLE send yet)
    static uint32_t lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate >= 10000) {
        OfflineStatusBuffer::update();
        lastStatusUpdate = millis();
    }
}
```

---

## 📱 APP SIDE: Conflict Handling

### **Kagri App Integration**

```dart
// lib/features/device_detail/data/datasources/ble_data_sync_datasource.dart

class BleDataSyncDatasource {
  // Retry logic with exponential backoff
  Future<BleDataResponse> getBufferedData({
    required int offset,
    int maxRetries = 2,
  }) async {
    int retryCount = 0;
    
    while (retryCount < maxRetries) {
      try {
        final characteristic = await bleService.readCharacteristic(
          serviceId: 'sync_service',
          characteristicId: 'sync_response',
          timeout: Duration(seconds: 10),
        );
        
        final data = characteristic.value;
        final status = data[0];
        
        if (status == 0x02) {  // Error: Provisioning active
          _showProvisioningBusyMessage();
          
          // Wait and retry
          await Future.delayed(Duration(seconds: 3));
          retryCount++;
          continue;
        }
        
        if (status == 0x00) {  // OK
          return parseBleResponse(data);
        }
        
      } on BleException catch (e) {
        if (e.message.contains('Disconnected')) {
          _showConnectionLostMessage();
        }
        
        retryCount++;
        if (retryCount < maxRetries) {
          await Future.delayed(Duration(seconds: 2 ^ retryCount));
        }
      }
    }
    
    throw SyncFailedException('Failed after $maxRetries retries');
  }
  
  void _showProvisioningBusyMessage() {
    // Show SnackBar: "Gateway is provisioning, try again later"
  }
}
```

---

## ✅ VERIFICATION CHECKLIST

### **Before Integration**

- [ ] Both BLE services have unique UUIDs
- [ ] Both services have separate characteristics
- [ ] ProvisionManager::isProvisioningActive() works correctly
- [ ] BleSyncService::canProcessSync() blocks when provisioning active
- [ ] No shared state between services (except BLEServer)
- [ ] Unit tests for conflict scenarios

### **After Integration**

- [ ] Provisioning still works (regression test)
- [ ] Data sync works when NOT provisioning
- [ ] Data sync gracefully rejected when provisioning active
- [ ] App shows appropriate error messages
- [ ] No crashes, memory leaks, or deadlocks
- [ ] BLE connection stable for extended time
- [ ] Load testing: 50 items sync + provisioning in sequence

---

## 🎯 SUMMARY

| Aspect | Design |
|--------|--------|
| **Architecture** | Coexistent services (separate UUIDs) |
| **Conflict Prevention** | State flags + early return |
| **Provisioning Precedence** | Higher (can block sync) |
| **Data Sync Precedence** | Lower (graceful rejection) |
| **Recovery** | Provisioning reboot = auto-recovery |
| **User Experience** | Show error msg if provisioning active |
| **Testing** | Unit + E2E + edge cases |

