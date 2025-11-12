# Cellular Module Component

**Module**: A7682S 4G LTE Modem  
**Version**: 1.0.0  
**Status**: Phase 1 Complete ✅

---

## 📋 **Overview**

This component provides a complete driver stack for the A7682S 4G LTE cellular module, enabling internet connectivity via cellular networks as a replacement for WiFi.

---

## 🏗️ **Architecture**

```
Application Layer
       ↓
Firebase REST Client (Phase 5) [TODO]
       ↓
HTTP/HTTPS Client (Phase 4) [TODO]
       ↓
TCP/IP Stack (Phase 3) [TODO]
       ↓
Cellular Connection Service (Phase 2) [TODO]
       ↓
AT Command Handler (Phase 1) [✅ COMPLETE]
       ↓
UART Driver (Phase 1) [✅ COMPLETE]
       ↓
A7682S Hardware
```

---

## 📁 **Directory Structure**

```
src/components/cellular/
├── include/
│   ├── cellular_uart.h              ✅ UART driver
│   ├── at_command_handler.h         ✅ AT command interface
│   ├── cellular_connection_service.h   [Phase 2]
│   ├── cellular_tcp_client.h           [Phase 3]
│   ├── cellular_http_client.h          [Phase 4]
│   └── README.md                     ✅ This file
└── src/
    ├── cellular_uart.cpp             ✅ UART implementation
    ├── at_command_handler.cpp        ✅ AT command implementation
    ├── cellular_connection_service.cpp [Phase 2]
    ├── cellular_tcp_client.cpp         [Phase 3]
    └── cellular_http_client.cpp        [Phase 4]
```

---

## 🔌 **Hardware Configuration**

### **Pin Connections**

| ESP32 Pin | A7682S Pin | Function | Direction |
|-----------|------------|----------|-----------|
| IO40 | RXD | UART TX | ESP32 → A7682S |
| IO41 | TXD | UART RX | ESP32 ← A7682S |
| IO39 | PWR_KEY | Power Enable | ESP32 → A7682S |
| IO38 | NETLIGHT | Network Status | ESP32 ← A7682S |
| GND | GND | Ground | - |
| 3.8V | VCC | Power | - |

### **Power Requirements**
- **Voltage**: 3.3V - 4.3V (typical 3.8V)
- **Current**: 500mA idle, up to 2A peak during transmission
- **⚠️ WARNING**: Do NOT use 5V directly!

### **UART Settings**
- **Baud Rate**: 115200
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1

---

## 🚀 **Quick Start**

### **Phase 1: Basic AT Commands** (Current)

```cpp
#include "cellular_uart.h"
#include "at_command_handler.h"

// Initialize UART
CellularUART uart;
uart.initialize();
uart.powerOn(5000);  // Wait 5 seconds

// Create AT handler
ATCommandHandler atHandler(&uart);

// Test communication
if (atHandler.testAT(3)) {
    Serial.println("✅ Module ready!");
}

// Check SIM
auto resp = atHandler.sendCommand("+CPIN?");
if (resp.success && resp.data.indexOf("READY") >= 0) {
    Serial.println("✅ SIM ready!");
}

// Get signal quality
resp = atHandler.sendCommand("+CSQ");
Serial.println(resp.data);  // "+CSQ: 25,0"
```

---

## 📚 **API Reference**

### **CellularUART**

**Constructor**
```cpp
CellularUART();  // Use default config
CellularUART(const Config& config);  // Custom config
```

**Configuration**
```cpp
struct Config {
    uint8_t txPin = 40;
    uint8_t rxPin = 41;
    uint8_t pwrPin = 39;
    uint8_t netPin = 38;
    uint32_t baudRate = 115200;
    uint8_t uartNum = 1;
};
```

**Methods**
```cpp
bool initialize();
bool powerOn(uint32_t delayMs = 3000);
bool powerOff(uint32_t delayMs = 1000);
PowerState getPowerState() const;
size_t write(const String& str);
size_t readLine(char* buffer, size_t maxLength, uint32_t timeoutMs = 1000);
String readUntilTimeout(uint32_t timeoutMs = 1000);
int available() const;
void flush();
bool isNetworkConnected() const;
```

---

### **ATCommandHandler**

**Constructor**
```cpp
explicit ATCommandHandler(CellularUART* uart);
```

**Methods**
```cpp
// Send AT command
Response sendCommand(const String& command, 
                    uint32_t timeoutMs = 1000,
                    bool expectOK = true);

// Test communication
bool testAT(uint8_t retries = 3);

// URC handling
void registerURCCallback(URCCallback callback);
void processURCs();

// Utilities
static String extractValue(const String& response, const String& prefix);
static std::vector<String> splitValues(const String& value);
static int parseCMEError(const String& response);
```

**Response Structure**
```cpp
struct Response {
    bool success;           // Command succeeded
    String data;            // Response data
    String errorMessage;    // Error message (if failed)
    int errorCode;          // CME/CMS error code
    uint32_t responseTimeMs; // Response time
};
```

---

## 🧪 **Testing**

### **Build Test**
```bash
pio run -e test-cellular-phase1
```

### **Upload & Monitor**
```bash
pio run -e test-cellular-phase1 -t upload
pio device monitor -e test-cellular-phase1
```

### **Expected Output**
```
=== PHASE 1 TEST: UART & AT Commands ===
✅ PASSED: UART initialized
✅ PASSED: Module powered on
✅ PASSED: Module responds to AT
✅ PASSED: SIM card ready
✅ PASSED: Good signal quality
✅ PASSED: Registered on home network
✅ PASSED: IMEI retrieved
```

See `docs/PHASE1_TEST_GUIDE.md` for detailed testing guide.

---

## 📖 **Common AT Commands**

### **Basic**
```
AT              Test communication
ATE0            Disable echo
AT+CPIN?        Check SIM status
AT+GSN          Get IMEI
```

### **Network**
```
AT+CREG?        Network registration
AT+COPS?        Operator info
AT+CSQ          Signal quality
AT+CGDCONT      Set APN (Phase 2)
AT+CGATT        Attach GPRS (Phase 2)
```

### **TCP/IP** (Phase 3)
```
AT+NETOPEN      Open network
AT+CIPOPEN      Open TCP socket
AT+CIPSEND      Send data
AT+CIPCLOSE     Close socket
```

---

## 🔧 **Troubleshooting**

### **No response from module**
1. Check TX/RX pins (they must be crossed!)
2. Verify baud rate (115200)
3. Wait 10 seconds after power on
4. Check power supply (2A minimum)

### **SIM not ready**
1. Check SIM card is inserted
2. Check SIM PIN if required
3. Try SIM in phone to verify

### **No signal**
1. Check antenna connection
2. Move to area with better coverage
3. Check SIM is activated

---

## 🗺️ **Development Roadmap**

- [x] **Phase 1**: UART Driver & AT Commands (✅ Complete)
- [ ] **Phase 2**: Cellular Connection Service (Next)
- [ ] **Phase 3**: TCP/IP Stack
- [ ] **Phase 4**: HTTP/HTTPS Client
- [ ] **Phase 5**: Firebase REST Client
- [ ] **Phase 6**: Gateway Integration
- [ ] **Phase 7**: Testing & Optimization

---

## 📝 **Notes**

- A7682S supports 4G LTE Cat-1
- Maximum downlink: 10 Mbps
- Maximum uplink: 5 Mbps
- Supports TCP, UDP, HTTP, HTTPS, MQTT, FTP
- Built-in SSL/TLS support
- GPS/GNSS support (not implemented yet)

---

## 📚 **References**

- [A7682S Datasheet](https://www.simcom.com/product/A7682S.html)
- [A7682S AT Command Manual](https://www.simcom.com/product/A7682S.html)
- [ESP32 UART Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html)

---

**Status**: Phase 1 Complete ✅  
**Next**: Phase 2 - Cellular Connection Service  
**Last Updated**: November 11, 2025
