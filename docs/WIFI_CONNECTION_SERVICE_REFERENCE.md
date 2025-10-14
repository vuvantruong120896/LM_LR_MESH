# WiFiConnectionService - Quick Reference Card

**Version**: 1.0  
**Date**: October 14, 2025  
**Location**: `src/components/lora_mesh_manager/src/services/`

---

## 🚀 Quick Start (30 seconds)

```cpp
#include "wifi_connection_service.h"

// Create instance
WiFiConnectionService wifi("MySSID", "MyPassword");

// Initialize
wifi.initialize();

// Connect
wifi.connect();

// In main loop
void loop() {
    wifi.update();  // REQUIRED for auto-reconnect!
}
```

---

## 📖 Constructor

```cpp
WiFiConnectionService(
    const char* ssid,           // WiFi network name
    const char* password,       // WiFi password
    bool autoReconnect = true,  // Enable auto-reconnect
    uint32_t reconnectIntervalMs = 1000  // Initial retry interval
);
```

**Example**:
```cpp
WiFiConnectionService wifi("MyNetwork", "Pass123", true, 2000);
```

---

## 🔧 Core Methods

### **initialize()**
```cpp
bool initialize();
```
Sets WiFi mode to station, disables auto-reconnect (we handle it), sets hostname.

**Usage**:
```cpp
if (!wifi.initialize()) {
    Serial.println("Init failed!");
}
```

---

### **connect()**
```cpp
bool connect(uint32_t timeoutMs = 10000);
```
Connect to WiFi with timeout. Blocks until connected or timeout.

**Usage**:
```cpp
if (wifi.connect(15000)) {  // 15-second timeout
    Serial.println("Connected!");
}
```

---

### **disconnect()**
```cpp
void disconnect();
```
Gracefully disconnect from WiFi.

**Usage**:
```cpp
wifi.disconnect();
```

---

### **isConnected()**
```cpp
bool isConnected() const;
```
Check if currently connected to WiFi.

**Usage**:
```cpp
if (wifi.isConnected()) {
    // Upload data
}
```

---

### **update()** ⚠️ **CRITICAL**
```cpp
void update();
```
**MUST be called in main loop!** Handles auto-reconnect and RSSI monitoring.

**Usage**:
```cpp
void loop() {
    wifi.update();  // Call every loop iteration
    delay(100);
}
```

---

## 📡 Information Methods

### **getRSSI()**
```cpp
int8_t getRSSI() const;
```
Get current signal strength in dBm.

**Usage**:
```cpp
int8_t rssi = wifi.getRSSI();
Serial.printf("Signal: %d dBm\n", rssi);
```

**Interpretation**:
- `-30 to -50`: Excellent
- `-50 to -60`: Good
- `-60 to -70`: Fair
- `-70 to -80`: Weak
- `< -80`: Very weak

---

### **getLocalIP()**
```cpp
String getLocalIP() const;
```
Get assigned IP address (empty if disconnected).

**Usage**:
```cpp
String ip = wifi.getLocalIP();
Serial.println("IP: " + ip);
```

---

### **getMACAddress()**
```cpp
String getMACAddress() const;
```
Get device MAC address.

**Usage**:
```cpp
String mac = wifi.getMACAddress();
Serial.println("MAC: " + mac);
```

---

### **getStatus()**
```cpp
WiFiStatus getStatus() const;
```
Get current connection status.

**Status Values**:
- `DISCONNECTED`: Not connected
- `CONNECTING`: Connection in progress
- `CONNECTED`: Successfully connected
- `RECONNECTING`: Reconnection in progress
- `FAILED`: Connection failed

**Usage**:
```cpp
auto status = wifi.getStatus();
if (status == WiFiConnectionService::WiFiStatus::CONNECTED) {
    // ...
}
```

---

## 📊 Statistics

### **getStats()**
```cpp
WiFiStats getStats() const;
```

**WiFiStats Structure**:
```cpp
struct WiFiStats {
    uint32_t connectionAttempts;    // Total attempts
    uint32_t successfulConnections; // Successful count
    uint32_t disconnections;        // Disconnect count
    uint32_t reconnectAttempts;     // Reconnect count
    uint32_t uptimeSeconds;         // Total uptime
    int8_t currentRSSI;             // Current signal
    int8_t averageRSSI;             // Average signal
    uint32_t lastConnectTime;       // Last connect (millis)
    uint32_t lastDisconnectTime;    // Last disconnect (millis)
};
```

**Usage**:
```cpp
WiFiConnectionService::WiFiStats stats = wifi.getStats();
Serial.printf("Uptime: %lu seconds\n", stats.uptimeSeconds);
Serial.printf("Attempts: %lu\n", stats.connectionAttempts);
Serial.printf("Avg RSSI: %d dBm\n", stats.averageRSSI);
```

---

### **resetStats()**
```cpp
void resetStats();
```
Reset all statistics to zero.

**Usage**:
```cpp
wifi.resetStats();
```

---

## 🎯 Event System

### **onEvent()**
```cpp
void onEvent(EventCallback callback);
```

**Callback Signature**:
```cpp
std::function<void(WiFiEvent event, int8_t rssi)>
```

**Event Types**:
- `CONNECTED`: Successfully connected
- `DISCONNECTED`: Disconnected from WiFi
- `RECONNECTING`: Attempting reconnect
- `CONNECTION_FAILED`: Connection failed
- `RSSI_LOW`: Signal dropped below threshold

**Usage**:
```cpp
wifi.onEvent([](WiFiConnectionService::WiFiEvent event, int8_t rssi) {
    switch (event) {
        case WiFiConnectionService::WiFiEvent::CONNECTED:
            Serial.printf("✅ Connected! RSSI: %d dBm\n", rssi);
            break;
        case WiFiConnectionService::WiFiEvent::DISCONNECTED:
            Serial.println("❌ Disconnected!");
            break;
        case WiFiConnectionService::WiFiEvent::RECONNECTING:
            Serial.println("⏳ Reconnecting...");
            break;
        case WiFiConnectionService::WiFiEvent::CONNECTION_FAILED:
            Serial.println("❌ Connection failed!");
            break;
        case WiFiConnectionService::WiFiEvent::RSSI_LOW:
            Serial.printf("⚠️ Low signal: %d dBm\n", rssi);
            break;
    }
});
```

**Multiple Callbacks**:
```cpp
wifi.onEvent(callback1);
wifi.onEvent(callback2);  // Both will be called
```

---

## ⚙️ Configuration

### **setAutoReconnect()**
```cpp
void setAutoReconnect(bool enabled);
```
Enable/disable automatic reconnection.

**Usage**:
```cpp
wifi.setAutoReconnect(true);   // Enable (default)
wifi.setAutoReconnect(false);  // Disable
```

---

### **getAutoReconnect()**
```cpp
bool getAutoReconnect() const;
```
Check if auto-reconnect is enabled.

**Usage**:
```cpp
if (wifi.getAutoReconnect()) {
    Serial.println("Auto-reconnect enabled");
}
```

---

### **setRSSIThreshold()**
```cpp
void setRSSIThreshold(int8_t threshold);
```
Set threshold for RSSI_LOW event (default: -80 dBm).

**Usage**:
```cpp
wifi.setRSSIThreshold(-75);  // More sensitive
wifi.setRSSIThreshold(-90);  // Less sensitive
```

---

## 📋 Common Patterns

### **Pattern 1: Basic Connection**
```cpp
WiFiConnectionService wifi("SSID", "Password");

void setup() {
    wifi.initialize();
    wifi.connect();
}

void loop() {
    wifi.update();
}
```

---

### **Pattern 2: Wait for Connection**
```cpp
WiFiConnectionService wifi("SSID", "Password");

void setup() {
    wifi.initialize();
    
    // Block until connected
    while (!wifi.connect(10000)) {
        Serial.println("Retry in 5s...");
        delay(5000);
    }
    
    Serial.println("Ready!");
}
```

---

### **Pattern 3: Connection-Aware Loop**
```cpp
void loop() {
    wifi.update();
    
    if (wifi.isConnected()) {
        // Upload data to cloud
        uploadData();
    } else {
        // Buffer data locally
        bufferData();
    }
}
```

---

### **Pattern 4: Event-Driven**
```cpp
bool canUpload = false;

void setup() {
    wifi.onEvent([](WiFiEvent event, int8_t rssi) {
        if (event == WiFiEvent::CONNECTED) {
            canUpload = true;
        } else if (event == WiFiEvent::DISCONNECTED) {
            canUpload = false;
        }
    });
    
    wifi.initialize();
    wifi.connect();
}

void loop() {
    wifi.update();
    
    if (canUpload) {
        uploadData();
    }
}
```

---

### **Pattern 5: Manual Reconnect Control**
```cpp
WiFiConnectionService wifi("SSID", "Password");

void setup() {
    wifi.setAutoReconnect(false);  // Manual control
    wifi.initialize();
    wifi.connect();
}

void loop() {
    wifi.update();
    
    // Custom reconnect logic
    if (!wifi.isConnected()) {
        static uint32_t lastAttempt = 0;
        if (millis() - lastAttempt > 30000) {  // Try every 30s
            wifi.connect();
            lastAttempt = millis();
        }
    }
}
```

---

## ⚡ Performance

### **Memory Usage**
- **Flash**: ~5KB (code)
- **RAM**: ~300 bytes per instance
- **Stack**: Minimal (no recursion)

### **Timing**
- **Connection**: 5-10 seconds (typical)
- **RSSI Update**: Every 5 seconds (when connected)
- **Auto-Reconnect**: Exponential backoff (1s → 60s max)

### **Reconnect Schedule**
```
Attempt 1: 1 second
Attempt 2: 2 seconds
Attempt 3: 4 seconds
Attempt 4: 8 seconds
Attempt 5: 16 seconds
Attempt 6: 32 seconds
Attempt 7+: 60 seconds (capped)
```

---

## 🐛 Troubleshooting

### **Problem**: Doesn't reconnect automatically

**Solution**:
```cpp
// Make sure update() is called!
void loop() {
    wifi.update();  // ← REQUIRED
}

// Check auto-reconnect is enabled
Serial.println(wifi.getAutoReconnect());
```

---

### **Problem**: Events not firing

**Solution**:
```cpp
// Register callback BEFORE connecting
wifi.onEvent(myCallback);  // ← First
wifi.initialize();
wifi.connect();            // ← After
```

---

### **Problem**: Connection times out

**Solution**:
```cpp
// Increase timeout
wifi.connect(30000);  // 30 seconds

// Check credentials
Serial.println("SSID: " + String(WIFI_SSID));

// Check signal strength
Serial.println(wifi.getRSSI());
```

---

### **Problem**: Frequent disconnections

**Solution**:
```cpp
// Monitor RSSI
wifi.onEvent([](WiFiEvent event, int8_t rssi) {
    if (event == WiFiEvent::RSSI_LOW) {
        Serial.printf("Weak signal: %d dBm\n", rssi);
    }
});

// Check router stability
WiFiStats stats = wifi.getStats();
Serial.printf("Disconnects: %lu\n", stats.disconnections);
```

---

## 🔒 Security Notes

### **❌ NEVER do this**:
```cpp
const char* ssid = "MyNetwork";
const char* password = "MyPassword123";
```

### **✅ Use NVS Storage (Production)**:
```cpp
String ssid = NVSStorageService::getWiFiSSID();
String password = NVSStorageService::getWiFiPassword();
WiFiConnectionService wifi(ssid.c_str(), password.c_str());
```

### **✅ Use Environment Variables (Development)**:
```ini
# platformio.ini
build_flags =
    -D WIFI_SSID=\"${env.WIFI_SSID}\"
    -D WIFI_PASSWORD=\"${env.WIFI_PASSWORD}\"
```

```cpp
WiFiConnectionService wifi(WIFI_SSID, WIFI_PASSWORD);
```

---

## 📚 Full API Reference

```cpp
class WiFiConnectionService {
public:
    // Constructor
    WiFiConnectionService(const char* ssid, const char* password, 
                         bool autoReconnect = true, 
                         uint32_t reconnectIntervalMs = 1000);
    
    // Lifecycle
    bool initialize();
    bool connect(uint32_t timeoutMs = 10000);
    void disconnect();
    void update();  // ← Call in loop!
    
    // Status
    bool isConnected() const;
    WiFiStatus getStatus() const;
    
    // Information
    int8_t getRSSI() const;
    String getLocalIP() const;
    String getMACAddress() const;
    WiFiStats getStats() const;
    
    // Configuration
    void setAutoReconnect(bool enabled);
    bool getAutoReconnect() const;
    void setRSSIThreshold(int8_t threshold);
    
    // Events
    void onEvent(EventCallback callback);
    
    // Utilities
    void resetStats();
};
```

---

## 💡 Tips & Tricks

### **Tip 1**: Always call `update()`
```cpp
void loop() {
    wifi.update();  // First thing in loop!
    // ... rest of your code
}
```

### **Tip 2**: Use events for state changes
```cpp
wifi.onEvent([](WiFiEvent event, int8_t rssi) {
    // React to connection changes
});
```

### **Tip 3**: Monitor statistics for debugging
```cpp
// Print stats every minute
static uint32_t lastPrint = 0;
if (millis() - lastPrint > 60000) {
    WiFiStats stats = wifi.getStats();
    Serial.printf("Uptime: %lu | Disconnects: %lu\n", 
                  stats.uptimeSeconds, stats.disconnections);
    lastPrint = millis();
}
```

### **Tip 4**: Check RSSI for placement optimization
```cpp
// Find optimal gateway location
Serial.printf("RSSI: %d dBm\n", wifi.getRSSI());
// Move gateway until RSSI > -60 dBm
```

---

## 📖 See Also

- **Full Documentation**: `docs/PHASE2_2_COMPLETION_REPORT.md`
- **Test Program**: `test/wifi_connection_test.cpp`
- **ESP32 WiFi Docs**: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html

---

**Quick Reference Card v1.0** | WiFiConnectionService | October 14, 2025
