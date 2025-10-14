# Phase 2: Add WiFi & Firebase Libraries

## 📁 Proposed File Structure (Hybrid Approach - Best Practice)

### **Rationale for Hybrid Structure**

```
Generic/Reusable Services → src/components/lora_mesh_manager/src/services/
Gateway-Specific Logic    → src/application/app_gateway/
```

**Why?**
1. ✅ **WiFiConnectionService** is generic - can be reused by node app for OTA updates
2. ✅ **FirebaseClient** is gateway-specific - tightly coupled to gateway business logic
3. ✅ **CloudSyncManager** orchestrates everything - gateway orchestration layer
4. ✅ Follows existing pattern (mesh services in components/, app logic in application/)

---

### **New Files to Create**

```
src/components/lora_mesh_manager/src/services/
├── WiFiConnectionService.h                ✨ NEW (125 lines)
└── WiFiConnectionService.cpp              ✨ NEW (230 lines)
    Purpose:
    - Generic WiFi connection/disconnection
    - Auto-reconnect with exponential backoff
    - Connection status monitoring
    - RSSI/signal quality tracking
    - Event callbacks (connected, disconnected)
    
    Reusable: YES - Node app can use for future OTA firmware updates
    Dependencies: Arduino WiFi library (built-in ESP32)

src/application/app_gateway/
├── firebase_client.h                      ✨ NEW (95 lines)
├── firebase_client.cpp                    ✨ NEW (380 lines)
│   Purpose:
│   - Firebase REST API wrapper
│   - Upload sensor data to /nodes/{nodeId}/sensorData
│   - Upload gateway status to /gateways/{gatewayId}/status
│   - Upload routing table to /gateways/{gatewayId}/routingTable
│   - Error handling & retry logic
│   - JSON serialization/deserialization
│   
│   Reusable: NO - Contains gateway-specific business logic
│   Dependencies: Firebase ESP32 Client library
│
└── cloud_sync_manager.h                   ✨ NEW (78 lines)
└── cloud_sync_manager.cpp                 ✨ NEW (185 lines)
    Purpose:
    - High-level orchestrator
    - Coordinates WiFi, Firebase, and Mesh
    - Manages upload queues & retry logic
    - Periodic status sync (every 30s)
    - Data buffering when WiFi down
    
    Reusable: NO - Gateway orchestration layer
    Dependencies: WiFiConnectionService + FirebaseClient + LoraMesher
```

**Total New Code**: ~1,100 lines (vs ~1,500 lines removed from UART)

---

## 📦 Step 1: Update platformio.ini

### **Add Required Libraries**

**File**: `platformio.ini`

**Modify** `[env:esp32-gateway]` section:

```ini
; Dependencies
lib_deps = 
    jgromes/RadioLib@^6.6.0                    ; ✅ Existing - LoRa driver
    bblanchon/ArduinoJson@^7.0.4               ; ✨ NEW - JSON parsing
    mobizt/Firebase ESP Client@^4.4.14         ; ✨ NEW - Firebase SDK
```

**Library Details**:

| Library | Size | Purpose | License |
|---------|------|---------|---------|
| **ArduinoJson** | ~25KB | JSON serialization for Firebase payloads | MIT |
| **Firebase ESP Client** | ~180KB | Firebase Realtime Database & Auth | MIT |

**Total Flash Impact**: +205KB (acceptable, ESP32 has 4MB flash)

---

### **Add WiFi Credentials Configuration**

**Option A: Environment Variables** (Recommended for development)

Add to `platformio.ini`:
```ini
build_flags =
    ; ... existing flags ...
    -D WIFI_SSID=\"${env.WIFI_SSID}\"         ; ✨ NEW - From environment
    -D WIFI_PASSWORD=\"${env.WIFI_PASSWORD}\" ; ✨ NEW - From environment
    -D FIREBASE_HOST=\"${env.FIREBASE_HOST}\" ; ✨ NEW - From environment
    -D FIREBASE_AUTH=\"${env.FIREBASE_AUTH}\" ; ✨ NEW - From environment
```

Then create `.env` file (add to `.gitignore`):
```bash
WIFI_SSID=MyNetworkName
WIFI_PASSWORD=MySecurePassword123
FIREBASE_HOST=my-project.firebaseio.com
FIREBASE_AUTH=your-database-secret-or-token
```

**Option B: NVS Storage** (Recommended for production)

Store credentials in encrypted NVS partition. Use existing `NVSStorageService`.

---

## 🔧 Step 2: Create WiFiConnectionService (Generic Service)

### **2.1. Header File**

**File**: `src/components/lora_mesh_manager/src/services/WiFiConnectionService.h`

```cpp
#ifndef _WIFI_CONNECTION_SERVICE_H
#define _WIFI_CONNECTION_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>

/**
 * @brief WiFi Connection Status
 */
enum class WiFiConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED
};

/**
 * @brief WiFi Connection Event Type
 */
enum class WiFiEventType {
    CONNECTED,
    DISCONNECTED,
    CONNECTION_FAILED,
    RECONNECTING
};

/**
 * @brief Generic WiFi Connection Manager
 * 
 * Provides low-level WiFi connection management with:
 * - Automatic reconnection with exponential backoff
 * - Connection status monitoring
 * - RSSI tracking
 * - Event callbacks
 * 
 * This service is generic and can be reused by any application
 * (gateway, node with OTA, etc.)
 */
class WiFiConnectionService {
public:
    /**
     * @brief WiFi event callback function type
     * @param event Event type
     * @param data Optional event data (IP address for CONNECTED)
     */
    using WiFiEventCallback = void (*)(WiFiEventType event, const char* data);

    /**
     * @brief Initialize WiFi service with credentials
     * @param ssid WiFi network SSID
     * @param password WiFi network password
     * @param autoReconnect Enable automatic reconnection
     * @return true if initialized successfully
     */
    static bool initialize(const char* ssid, const char* password, bool autoReconnect = true);

    /**
     * @brief Connect to WiFi network
     * @param timeoutMs Connection timeout in milliseconds (default: 10000)
     * @return true if connected successfully
     */
    static bool connect(uint32_t timeoutMs = 10000);

    /**
     * @brief Disconnect from WiFi network
     */
    static void disconnect();

    /**
     * @brief Check if currently connected to WiFi
     * @return true if connected
     */
    static bool isConnected();

    /**
     * @brief Get current connection status
     * @return WiFiConnectionStatus enum value
     */
    static WiFiConnectionStatus getStatus();

    /**
     * @brief Get WiFi signal strength (RSSI)
     * @return RSSI in dBm (-30 to -100), or 0 if disconnected
     */
    static int getRSSI();

    /**
     * @brief Get local IP address
     * @return IP address as string (e.g., "192.168.1.100")
     */
    static String getIPAddress();

    /**
     * @brief Get MAC address
     * @return MAC address as string (e.g., "AA:BB:CC:DD:EE:FF")
     */
    static String getMACAddress();

    /**
     * @brief Get connected SSID
     * @return SSID string
     */
    static String getSSID();

    /**
     * @brief Update connection status (call in main loop)
     * Handles automatic reconnection if enabled
     */
    static void update();

    /**
     * @brief Set event callback
     * @param callback Function to call on WiFi events
     */
    static void setEventCallback(WiFiEventCallback callback);

    /**
     * @brief Enable/disable automatic reconnection
     * @param enable true to enable auto-reconnect
     */
    static void setAutoReconnect(bool enable);

    /**
     * @brief Get connection uptime
     * @return Milliseconds since last successful connection
     */
    static uint32_t getConnectionUptime();

private:
    static const char* TAG;
    
    static String ssid;
    static String password;
    static bool autoReconnectEnabled;
    static WiFiConnectionStatus status;
    static WiFiEventCallback eventCallback;
    
    static uint32_t lastConnectionAttempt;
    static uint32_t reconnectDelay;
    static uint8_t reconnectAttempts;
    static uint32_t connectionStartTime;
    
    static const uint32_t MIN_RECONNECT_DELAY = 1000;      // 1 second
    static const uint32_t MAX_RECONNECT_DELAY = 60000;     // 60 seconds
    static const uint8_t MAX_RECONNECT_ATTEMPTS = 10;
    
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
    static void handleReconnect();
};

#endif // _WIFI_CONNECTION_SERVICE_H
```

---

### **2.2. Implementation File**

**File**: `src/components/lora_mesh_manager/src/services/WiFiConnectionService.cpp`

```cpp
#include "WiFiConnectionService.h"

// Static member initialization
const char* WiFiConnectionService::TAG = "WiFiConnSvc";
String WiFiConnectionService::ssid = "";
String WiFiConnectionService::password = "";
bool WiFiConnectionService::autoReconnectEnabled = true;
WiFiConnectionStatus WiFiConnectionService::status = WiFiConnectionStatus::DISCONNECTED;
WiFiConnectionService::WiFiEventCallback WiFiConnectionService::eventCallback = nullptr;
uint32_t WiFiConnectionService::lastConnectionAttempt = 0;
uint32_t WiFiConnectionService::reconnectDelay = MIN_RECONNECT_DELAY;
uint8_t WiFiConnectionService::reconnectAttempts = 0;
uint32_t WiFiConnectionService::connectionStartTime = 0;

bool WiFiConnectionService::initialize(const char* ssid, const char* password, bool autoReconnect) {
    ESP_LOGI(TAG, "Initializing WiFi service");
    ESP_LOGI(TAG, "SSID: %s", ssid);
    
    WiFiConnectionService::ssid = String(ssid);
    WiFiConnectionService::password = String(password);
    WiFiConnectionService::autoReconnectEnabled = autoReconnect;
    
    // Set WiFi mode to station (client)
    WiFi.mode(WIFI_STA);
    
    // Disable auto-connect (we handle it manually)
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    
    // Register event handlers
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        onWiFiEvent(event, info);
    });
    
    ESP_LOGI(TAG, "WiFi service initialized");
    return true;
}

bool WiFiConnectionService::connect(uint32_t timeoutMs) {
    if (status == WiFiConnectionStatus::CONNECTING) {
        ESP_LOGW(TAG, "Already connecting...");
        return false;
    }
    
    if (ssid.isEmpty()) {
        ESP_LOGE(TAG, "SSID not set");
        return false;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
    status = WiFiConnectionStatus::CONNECTING;
    lastConnectionAttempt = millis();
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Wait for connection
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        status = WiFiConnectionStatus::CONNECTED;
        connectionStartTime = millis();
        reconnectAttempts = 0;
        reconnectDelay = MIN_RECONNECT_DELAY;
        
        ESP_LOGI(TAG, "WiFi connected!");
        ESP_LOGI(TAG, "IP address: %s", WiFi.localIP().toString().c_str());
        ESP_LOGI(TAG, "RSSI: %d dBm", WiFi.RSSI());
        
        if (eventCallback) {
            eventCallback(WiFiEventType::CONNECTED, WiFi.localIP().toString().c_str());
        }
        
        return true;
    } else {
        status = WiFiConnectionStatus::FAILED;
        reconnectAttempts++;
        
        ESP_LOGW(TAG, "WiFi connection failed (attempt %d)", reconnectAttempts);
        
        if (eventCallback) {
            eventCallback(WiFiEventType::CONNECTION_FAILED, nullptr);
        }
        
        return false;
    }
}

void WiFiConnectionService::disconnect() {
    ESP_LOGI(TAG, "Disconnecting WiFi");
    WiFi.disconnect(true);
    status = WiFiConnectionStatus::DISCONNECTED;
}

bool WiFiConnectionService::isConnected() {
    return WiFi.status() == WL_CONNECTED && status == WiFiConnectionStatus::CONNECTED;
}

WiFiConnectionStatus WiFiConnectionService::getStatus() {
    return status;
}

int WiFiConnectionService::getRSSI() {
    if (!isConnected()) return 0;
    return WiFi.RSSI();
}

String WiFiConnectionService::getIPAddress() {
    if (!isConnected()) return "0.0.0.0";
    return WiFi.localIP().toString();
}

String WiFiConnectionService::getMACAddress() {
    return WiFi.macAddress();
}

String WiFiConnectionService::getSSID() {
    if (!isConnected()) return "";
    return WiFi.SSID();
}

void WiFiConnectionService::update() {
    // Check if connection lost
    if (status == WiFiConnectionStatus::CONNECTED && WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi connection lost!");
        status = WiFiConnectionStatus::DISCONNECTED;
        
        if (eventCallback) {
            eventCallback(WiFiEventType::DISCONNECTED, nullptr);
        }
    }
    
    // Handle automatic reconnection
    if (autoReconnectEnabled && !isConnected()) {
        handleReconnect();
    }
}

void WiFiConnectionService::setEventCallback(WiFiEventCallback callback) {
    eventCallback = callback;
}

void WiFiConnectionService::setAutoReconnect(bool enable) {
    autoReconnectEnabled = enable;
    ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");
}

uint32_t WiFiConnectionService::getConnectionUptime() {
    if (!isConnected() || connectionStartTime == 0) return 0;
    return millis() - connectionStartTime;
}

void WiFiConnectionService::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi event: CONNECTED");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "WiFi event: GOT_IP");
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi event: DISCONNECTED (reason: %d)", info.wifi_sta_disconnected.reason);
            break;
            
        default:
            break;
    }
}

void WiFiConnectionService::handleReconnect() {
    // Don't attempt reconnect if disabled or max attempts reached
    if (!autoReconnectEnabled || reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        return;
    }
    
    // Check if enough time has passed since last attempt
    uint32_t currentTime = millis();
    if (currentTime - lastConnectionAttempt < reconnectDelay) {
        return;
    }
    
    ESP_LOGI(TAG, "Attempting reconnection (attempt %d, delay %dms)", 
             reconnectAttempts + 1, reconnectDelay);
    
    if (eventCallback) {
        eventCallback(WiFiEventType::RECONNECTING, nullptr);
    }
    
    // Exponential backoff
    reconnectDelay = min(reconnectDelay * 2, MAX_RECONNECT_DELAY);
    
    connect();
}
```

---

## 📝 Step 3: Update Build Configuration

### **Add Include Paths**

**File**: `platformio.ini`

**Modify** `build_flags` section for `[env:esp32-gateway]`:

```ini
build_flags =
    ; ... existing flags ...
    -I src/components/lora_mesh_manager/src/services  ; ✅ Already exists
    ; WiFiConnectionService is in services/, so no new include needed!
```

No changes needed! The service is already in the included path.

---

## ✅ Step 4: Test WiFi Connection Independently

### **Create Test Sketch**

**File**: `test/test_wifi_connection.cpp` (temporary, for testing only)

```cpp
#include <Arduino.h>
#include "WiFiConnectionService.h"

void onWiFiEvent(WiFiEventType event, const char* data) {
    switch (event) {
        case WiFiEventType::CONNECTED:
            Serial.printf("✅ WiFi Connected! IP: %s\n", data);
            break;
        case WiFiEventType::DISCONNECTED:
            Serial.println("❌ WiFi Disconnected");
            break;
        case WiFiEventType::CONNECTION_FAILED:
            Serial.println("⚠️  WiFi Connection Failed");
            break;
        case WiFiEventType::RECONNECTING:
            Serial.println("🔄 WiFi Reconnecting...");
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("=== WiFi Connection Service Test ===");
    
    // Initialize WiFi service
    WiFiConnectionService::initialize("YOUR_SSID", "YOUR_PASSWORD", true);
    WiFiConnectionService::setEventCallback(onWiFiEvent);
    
    // Connect
    if (WiFiConnectionService::connect(10000)) {
        Serial.println("✅ Initial connection successful");
    } else {
        Serial.println("❌ Initial connection failed");
    }
}

void loop() {
    // Update WiFi status (handles auto-reconnect)
    WiFiConnectionService::update();
    
    // Print status every 5 seconds
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        
        if (WiFiConnectionService::isConnected()) {
            Serial.printf("📶 WiFi Status: CONNECTED\n");
            Serial.printf("   IP: %s\n", WiFiConnectionService::getIPAddress().c_str());
            Serial.printf("   RSSI: %d dBm\n", WiFiConnectionService::getRSSI());
            Serial.printf("   Uptime: %d seconds\n", 
                         WiFiConnectionService::getConnectionUptime() / 1000);
        } else {
            Serial.println("📶 WiFi Status: DISCONNECTED");
        }
    }
    
    delay(100);
}
```

---

## 🎯 Summary of Phase 2 Progress

### **Files Created**:
✅ `WiFiConnectionService.h` (~125 lines)  
✅ `WiFiConnectionService.cpp` (~230 lines)  
✅ Test sketch for validation

### **Files Modified**:
⚠️ `platformio.ini` - Added Firebase & JSON libraries

### **What's Next** (Phase 3):
✨ Create `FirebaseClient` in `src/application/app_gateway/`  
✨ Create `CloudSyncManager` in `src/application/app_gateway/`  
✨ Integrate with `GatewayApp`

---

## 📋 Verification Checklist

Before proceeding to Phase 3:

- [ ] `platformio.ini` updated with Firebase library
- [ ] `WiFiConnectionService.h` created in services/
- [ ] `WiFiConnectionService.cpp` created in services/
- [ ] Test sketch compiles successfully
- [ ] WiFi connects to your network
- [ ] Auto-reconnect works (test by disabling router)
- [ ] Event callbacks work correctly
- [ ] RSSI monitoring works

---

**Ready to proceed to Phase 3: Create Firebase Service?** 🚀
