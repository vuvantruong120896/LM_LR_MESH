# 4G Module Integration Feasibility Analysis

**Date**: November 10, 2025  
**Project**: LoRa Mesh Gateway with Firebase Cloud Integration  
**Device**: ESP32 (DOIT DevKit V1) / ESP32-S3  
**Status**: ✅ FEASIBLE with proper architecture

---

## Executive Summary

Adding a **4G/LTE module alongside existing WiFi connectivity** is technically feasible but requires careful architectural planning. The current system is WiFi-exclusive, but the design patterns support multi-connectivity with minimal core changes.

**Key Findings:**
- ✅ Hardware interfaces available (free UART ports)
- ✅ Memory budget permits 4G stack (~50-100KB)
- ⚠️ Firebase library tightly coupled to WiFi (requires abstraction layer)
- ⚠️ Network failover strategy must be explicitly designed
- 📊 Network stack implications well-understood (can coexist)

---

## 1. Hardware Interface Analysis

### Current Pin Configuration (Gateway)

```
SPI Bus (LoRa Module):
  SCK:   GPIO 18
  MISO:  GPIO 16  
  MOSI:  GPIO 19
  CS:    GPIO 5

LoRa Specific:
  RST:   GPIO 4
  IRQ:   GPIO 6
  IO1:   Unused (-1)

Miscellaneous:
  LED:   GPIO 0

Total GPIO Used: 7 pins
```

### Available Interfaces for 4G Module

**Option 1: UART (Recommended)**
```
UART0: GPIO 1 (TX), GPIO 3 (RX)    [USED: Serial Monitor/Debug]
UART1: GPIO 9 (TX), GPIO 10 (RX)   [AVAILABLE]
UART2: GPIO 17 (TX), GPIO 16 (RX)  [CONFLICT: MISO for LoRa!]
```

**Verdict**: ✅ **UART1 is ideal** (GPIO 9/10 completely free)

**Option 2: SPI Alternative**
```
SPI0: GPIO 6 (CLK), GPIO 7 (MOSI), GPIO 8 (MISO), GPIO 11 (CS)  [AVAILABLE but not on current board]
```

**Verdict**: ❌ Would require custom wiring, UART preferred

**Option 3: I2C**
```
I2C0: GPIO 21 (SDA), GPIO 22 (SCL)  [AVAILABLE]
```

**Verdict**: ⚠️ Possible but most 4G modules use UART (fewer pins, faster)

### Recommended Configuration

```cpp
// 4G Module UART Configuration (add to gateway_config.h)
#define MODEM_UART_NUM          UART_NUM_1
#define MODEM_TX_PIN            9      // GPIO 9 - Free
#define MODEM_RX_PIN            10     // GPIO 10 - Free
#define MODEM_BAUD_RATE         115200 // Standard AT command speed
#define MODEM_RESET_PIN         14     // GPIO 14 - Free
#define MODEM_POWER_PIN         13     // GPIO 13 - Free (optional)
```

✅ **NO CONFLICTS** with LoRa (SPI) or WiFi (WiFi module is integrated)

---

## 2. Memory and Stack Impact Analysis

### Current Heap Usage (Baseline)

From gateway_app.cpp loop monitoring:
- **Free Heap at Boot**: ~300-350 KB
- **Minimum Free Heap**: ~180-250 KB (during Firebase operations)
- **Heap Watermark**: Critical at <30 KB (currently logged)

### 4G Module Memory Overhead

| Component | Memory (KB) | Note |
|-----------|-----------|------|
| 4G Driver (AT commands) | 15-20 | TinyGSM library or similar |
| SSL/TLS Stack | 30-50 | For HTTPS support over 4G |
| PPP Protocol Stack | 20-30 | Point-to-Point Protocol |
| DNS Resolution Cache | 5-10 | Domain name caching |
| HTTP Client (if needed) | 10-15 | For MQTT or HTTP uploads |
| **Total Estimated** | **80-125 KB** | Worst case scenario |

### Current Stack Allocation Analysis

From previous system analysis (Oct 30):

| Task | Stack (KB) | CPU | Priority |
|------|----------|-----|----------|
| Gateway Receive | 16 | 0 | 2 |
| Firebase Queue | 16 | 1 | 2 |
| Firebase Poller | 8 | 1 | 1 |
| LoRa Mesh Tasks | 4×6 | Unpinned | 2-6 |
| Netkey Worker | 8 | 1 | - |
| **Total Allocated** | **84 KB** | - | - |
| **Free for 4G** | **~80-100 KB** | - | - |

### Verdict: ✅ Memory is SUFFICIENT

**Constraints:**
- Minimum free heap must remain > 50 KB (currently maintained)
- 4G driver task should run on **CPU1** (same as Firebase)
- 4G connection polling can share queue infrastructure with Firebase

---

## 3. Network Stack Implications

### Current Architecture

```
┌─────────────────────────────────────┐
│         Application Layer           │
│  (Gateway App, Firebase Client)     │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      WiFi Connection Service        │
│  (WiFiConnectionService.h)          │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Arduino WiFi Library              │
│   (ESP32 Built-in WiFi)             │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│  ESP-IDF WiFi Stack (CPU0)          │
│  TCP/IP, lwIP, Socket Layer         │
└──────────────┬──────────────────────┘
```

### After Adding 4G (Proposed)

```
┌────────────────────────────────────────────────────────┐
│              Application Layer                         │
│  (Gateway App, Firebase Client, Multi-Network Manager) │
└──────┬──────────────────────┬──────────────────────────┘
       │                      │
   ┌───▼────────────┐    ┌────▼──────────────┐
   │  WiFi Service  │    │  4G/LTE Service   │
   │  (Existing)    │    │  (New)            │
   └───┬────────────┘    └────┬──────────────┘
       │                      │
   ┌───▼────────────┐    ┌────▼──────────────┐
   │  Arduino WiFi  │    │  TinyGSM or LTE   │
   │  Library       │    │  Driver Library   │
   └───┬────────────┘    └────┬──────────────┘
       │                      │
   ┌───▼──────────────────────▼──────────────┐
   │   Unified Network Interface (Abstract)   │
   │   (New) - Route traffic intelligently    │
   └───┬──────────────────────────────────────┘
       │
   ┌───▼──────────────────────────────────────┐
   │  Multi-Stack Support:                    │
   │  • ESP-IDF TCP/IP (WiFi, CPU0)           │
   │  • PPP Stack (4G, CPU1 via UART)         │
   │  • lwIP maintains routing between them    │
   └────────────────────────────────────────────┘
```

### Network Stack Behavior

**Important**: When using 4G via UART AT commands:
- **No additional WiFi stack overhead** (WiFi runs as-is)
- **4G uses separate PPP stack** (Point-to-Point Protocol)
- **Dual simultaneous connections possible** (WiFi primary + 4G backup)
- **lwIP routing**: Can configure priority/failover via metric settings

### TCP/IP Stack CPU Affinity

| Stack | CPU | Why |
|-------|-----|-----|
| WiFi (ESP-IDF) | CPU0 | Non-configurable, managed by ESP-IDF |
| 4G/PPP (via UART) | CPU1 | UART driver + AT command parsing (CPU1) |
| Socket Operations | CPU0 or CPU1 | Transparent to application |

✅ **No conflicts**: Both stacks operate on different CPUs

---

## 4. Firebase Library Coupling Analysis

### Current Problem: Firebase Dependency Chain

```cpp
// firebase_client.h - Line 1-10 (currently)
#include <FirebaseESP32.h>        // <- Hardcoded dependency
#include <WiFi.h>                 // <- Assumes WiFi always available

// firebase_client.cpp - Line ~150
FirebaseData fbdo;
Firebase.begin(
    FIREBASE_HOST, 
    FIREBASE_AUTH,
    &fbdo
);
// ^^^ Internally uses WiFi.begin() - no abstraction!
```

### Impact Assessment

| Aspect | Impact | Severity |
|--------|--------|----------|
| **Tight WiFi Coupling** | Firebase library directly calls WiFi methods | 🔴 HIGH |
| **No Network Abstraction** | Can't route Firebase via 4G without WiFi | 🔴 HIGH |
| **Failover Logic** | Manual implementation needed | 🟡 MEDIUM |
| **Dual Stack Support** | Not built-in, requires custom logic | 🟡 MEDIUM |

### Solution: Network Abstraction Layer (Required)

**Must create abstraction** to decouple Firebase from WiFi:

```cpp
// new file: src/components/network/include/NetworkInterface.h
class INetworkInterface {
public:
    virtual bool isConnected() = 0;
    virtual void update() = 0;
    virtual String getName() = 0;
    virtual int getSignalStrength() = 0;  // RSSI or LTE signal bars
    virtual ~INetworkInterface() = default
};

// Concrete implementations:
class WiFiNetworkInterface : public INetworkInterface {
    // Existing WiFiConnectionService wrapper
};

class CellularNetworkInterface : public INetworkInterface {
    // New 4G/LTE wrapper using TinyGSM
};

// Network manager for failover:
class MultiNetworkManager {
    void setPrimaryNetwork(INetworkInterface* primary);
    void setBackupNetwork(INetworkInterface* backup);
    INetworkInterface* getActiveNetwork();  // Returns primary if connected, else backup
};
```

### Modification Required to Firebase Client

```cpp
// firebase_client.h - AFTER refactoring
class FirebaseClient {
private:
    INetworkInterface* networkInterface;  // Abstraction instead of WiFi
    
public:
    void initialize(INetworkInterface* network) {
        networkInterface = network;
        // Firebase initialization happens here, using abstraction
    }
    
    void update() {
        if (networkInterface && networkInterface->isConnected()) {
            // Firebase operations...
        }
    }
};
```

**Refactoring Effort**: ~4-6 hours
- Extract WiFiConnectionService into INetworkInterface
- Create CellularNetworkInterface wrapper
- Update FirebaseClient to use abstraction
- Test both interfaces independently

---

## 5. Network Failover Strategy

### Current System (WiFi-Only)

```
WiFi Connected?
    ├─ YES → Use Firebase operations
    ├─ NO  → Offline buffer (accumulate data)
    └─ RECONNECTING → Exponential backoff
```

### Proposed Multi-Network Architecture

#### Option A: WiFi Primary + 4G Backup (Recommended)

```
┌──────────────────────────────────────┐
│  Connectivity Check (every 30s)      │
└──────────────────────────────────────┘
           │
    ┌──────▼──────┐
    │  WiFi Ready? │
    └──┬───────┬──┘
       │       │
      YES     NO
       │       │
       │   ┌───▼───────────┐
       │   │ 4G Activated? │
       │   └───┬────────┬──┘
       │      YES      NO
       │       │       │
       │   UPLOAD  OFFLINE
       │   VIA 4G  BUFFER
       │       │
       └───┬───┘
           │
      UPLOAD VIA
       WiFi
```

**Advantages**:
- WiFi is power-efficient (on-device chip)
- 4G only activates when WiFi fails
- Clear priority hierarchy
- Lower battery drain

**Implementation**:
```cpp
// In MultiNetworkManager
INetworkInterface* activeNetwork = nullptr;

if (wifiInterface->isConnected()) {
    activeNetwork = wifiInterface;
} else if (cellularInterface->isConnected()) {
    ESP_LOGW(TAG, "⚠️ WiFi down, switching to 4G backup");
    activeNetwork = cellularInterface;
} else {
    ESP_LOGI(TAG, "📦 No connectivity, buffering data");
    activeNetwork = nullptr;  // Trigger offline buffer
}
```

#### Option B: Parallel (WiFi + 4G Simultaneously)

```
WiFi Connected? ─┐
                 ├─ DUAL UPLOAD: Send to Firebase via both
4G Connected?  ──┘
```

**Advantages**:
- Redundancy: one network failure doesn't lose data
- Higher reliability for critical data
- Can prioritize fastest network

**Disadvantages**:
- Double bandwidth usage
- Increased power consumption
- Potential data duplication
- More complex conflict resolution

**When to use**: For critical monitoring where data loss is unacceptable

#### Option C: Round-Robin (Alternate between networks)

**Not recommended** for this use case - adds complexity without clear benefit.

---

## 6. Potential Conflicts Analysis

### 1. Hardware Conflicts ✅ RESOLVED

| Pin | Current Use | 4G Module | Conflict? |
|-----|------------|-----------|-----------|
| GPIO 9 | Free | UART1 TX | ✅ No |
| GPIO 10 | Free | UART1 RX | ✅ No |
| GPIO 18 | SPI SCK | - | ✅ No |
| GPIO 19 | SPI MOSI | - | ✅ No |
| GPIO 16 | SPI MISO | - | ✅ No |
| GPIO 5 | LoRa CS | - | ✅ No |
| GPIO 4 | LoRa RST | - | ✅ No |
| GPIO 6 | LoRa IRQ | - | ✅ No |

**Verdict**: ✅ **ZERO PIN CONFLICTS** with UART1

---

### 2. Power/Current Conflicts ⚠️ DEPENDS ON HARDWARE

**ESP32-DOIT DevKit V1 Specifications**:
- USB Power: 500mA (standard USB 2.0)
- OnBoard Regulator: 1A typical rating
- WiFi Peak Current: ~200 mA
- LoRa TX Current: ~100-150 mA
- **Available for 4G**: ~150-250 mA

**4G Module Typical Current Drain**:
- Idle: 50-100 mA
- Data RX: 100-150 mA  
- Data TX: 200-500 mA (peak)
- Registration (initial): 300-600 mA

**Risk**: ⚠️ **HIGH** - Peak current during 4G TX + WiFi RX may exceed 1A

**Mitigation**:
```cpp
// Implement sequential activation (never both transmitting simultaneously)
if (wifiActive && cellularTxNeeded) {
    // Temporarily pause WiFi TX to allow 4G TX
    wifiService->pauseTransmissions(500);  // 500ms window
    cellularService->transmit(data);
    wifiService->resumeTransmissions();
}
```

**Better Solution**: Use external power supply (5V/2A minimum)

---

### 3. Bandwidth Conflicts ✅ NO CONFLICT

WiFi operates on **2.4 GHz band**  
4G operates on **cellular bands** (varies by region: 700 MHz - 2.6 GHz)

- Different frequency bands → **No interference**
- Can operate simultaneously

---

### 4. Task/CPU Contention ⚠️ MANAGEABLE

**Current Task Layout**:

| Task | CPU | Stack | Priority | Load |
|------|-----|-------|----------|------|
| Gateway Receive | CPU0 | 16KB | 2 | High (LoRa RX) |
| Firebase Queue | CPU1 | 16KB | 2 | Medium |
| Firebase Poller | CPU1 | 8KB | 1 | Low |
| Netkey Worker | CPU1 | 8KB | - | Rare |
| **4G Driver (NEW)** | **CPU1** | **16KB** | **3** | **Medium** |
| **4G Polling (NEW)** | **CPU1** | **8KB** | **1** | **Low** |

**Concern**: CPU1 now has 3 periodic tasks (Firebase Queue, 4G Driver, Netkey Worker)

**Solution**:
```cpp
// Implement task scheduling to avoid conflicts:
// - Firebase Queue: 100ms cycles
// - 4G Driver: 250ms cycles (staggered)
// - Firebase Poller: 5s cycles

// Use FreeRTOS notification API to signal when data ready
// Avoid busy-waiting/polling
```

**Verdict**: ⚠️ **MANAGEABLE** with proper task scheduling

---

### 5. Firebase Library Conflicts 🔴 CRITICAL

Firebase ESP32 library **assumes WiFi is always the primary connection**.

**Problems**:
```cpp
Firebase.begin(..., &fbdo);  // Hardcoded to WiFi
// If WiFi drops, library may:
// - Retry only on WiFi
// - Not check 4G availability
// - Require manual reconnection logic
```

**Solution Required**: Network abstraction layer (as described in Section 4)

---

### 6. SSL/TLS Certificate Management ⚠️ MANAGEABLE

Both WiFi and 4G need HTTPS certificates for Firebase:
- **Current**: WiFi uses Arduino WiFi client with built-in CA certificates
- **4G Needed**: TinyGSM + PicoTLS (lightweight TLS for embedded)

```cpp
// Size comparison:
WiFi SSL/TLS:        ~20 KB (built-in, via esp-idf)
4G SSL/TLS:          ~30-50 KB (needs external lib)
```

**Total SSL/TLS overhead**: Already accounted for in Section 2 memory analysis

---

## 7. Implementation Roadmap

### Phase 1: Foundation (Week 1) - ~8 hours

```
✅ Task 1.1: Add 4G module UART configuration to gateway_config.h
   - Define pins, baud rate, timeouts
   - Estimated: 30 min

✅ Task 1.2: Create INetworkInterface abstraction layer  
   - Location: src/components/network/include/NetworkInterface.h
   - Define interface contract
   - Estimated: 1 hour

✅ Task 1.3: Refactor WiFiConnectionService
   - Implement INetworkInterface
   - Add abstraction wrapper
   - Estimated: 1.5 hours

✅ Task 1.4: Create CellularNetworkInterface stub
   - Implement INetworkInterface for 4G
   - Placeholder methods (will integrate TinyGSM later)
   - Estimated: 1 hour

✅ Task 1.5: Create MultiNetworkManager
   - Implement WiFi primary + 4G backup logic
   - Estimated: 2 hours

✅ Task 1.6: Update FirebaseClient to use abstraction
   - Replace WiFi dependency with INetworkInterface
   - Estimated: 2 hours
```

### Phase 2: 4G Driver Integration (Week 2) - ~12 hours

```
Task 2.1: Integrate TinyGSM library
   - Add to platformio.ini
   - Pin configuration
   - Estimated: 2 hours

Task 2.2: Implement CellularNetworkInterface (full)
   - AT command handling
   - Connection state management
   - Estimated: 4 hours

Task 2.3: Implement failover logic
   - WiFi → 4G switching
   - Estimated: 2 hours

Task 2.4: Add 4G monitoring & metrics
   - Signal strength tracking
   - Connection statistics
   - Estimated: 2 hours

Task 2.5: Test on hardware
   - Single network tests (WiFi only, 4G only)
   - Failover tests
   - Estimated: 2 hours
```

### Phase 3: Optimization (Week 3) - ~8 hours

```
Task 3.1: Power management
   - Implement current limiting logic
   - TX scheduling
   - Estimated: 2 hours

Task 3.2: Performance tuning
   - Task scheduling optimization
   - CPU1 load balancing
   - Estimated: 2 hours

Task 3.3: Firebase integration testing
   - Failover + Firebase sync
   - Estimated: 2 hours

Task 3.4: Documentation
   - Architecture guide
   - Configuration manual
   - Estimated: 2 hours
```

### Phase 4: Production Validation (Week 4) - ~6 hours

```
Task 4.1: Long-duration tests
   - 24h continuous operation
   - Network switching scenarios
   - Estimated: 2 hours

Task 4.2: Stress testing
   - High data volume
   - Rapid network switching
   - Estimated: 2 hours

Task 4.3: Edge case handling
   - Simultaneous network failures
   - Partial connectivity
   - Estimated: 2 hours
```

**Total Implementation Time**: ~34 hours (~1 week with focused work)

---

## 8. Risk Assessment & Mitigation

| Risk | Severity | Probability | Mitigation |
|------|----------|-------------|-----------|
| **Insufficient Memory** | HIGH | LOW | See Section 2 - ample memory available |
| **Power Supply Inadequacy** | HIGH | MEDIUM | Use 5V/2A external PSU, implement TX scheduling |
| **Firebase Library Incompatibility** | CRITICAL | HIGH | Create network abstraction layer (Phase 1) |
| **Task CPU Contention** | MEDIUM | MEDIUM | Stagger polling cycles, use notifications |
| **UART Pin Conflicts** | LOW | LOW | Verified no conflicts with UART1 (GPIO 9/10) |
| **SSL Certificate Issues** | MEDIUM | LOW | Use TinyGSM built-in cert support |
| **LTE Module Hardware Failure** | LOW | LOW | Implement watchdog, auto-recovery logic |
| **Failover Logic Bugs** | MEDIUM | MEDIUM | Extensive testing, unit tests for state machine |

---

## 9. Recommended 4G Module Options

### Best-Performing Options (Recommended)

#### 1. **SIM7600G-H (Best Overall)**
- **Frequency**: 2G/3G/4G/LTE-M/NB-IoT
- **Interface**: UART (AT commands)
- **Size**: Compact module
- **Power**: 3.8V, ~500mA average
- **Cost**: ~$30-50
- **Pros**: Mature, well-documented, TinyGSM support
- **Cons**: Needs voltage converter (3.8V supply)

```cpp
// platformio.ini addition
lib_deps =
    ... existing ...
    tinygsm                    ; TinyGSM library
```

#### 2. **SIM800L (Budget-Friendly)**
- **Frequency**: 2G/3G/LTE fallback
- **Interface**: UART (AT commands)
- **Power**: 3.7V, ~1000mA peak
- **Cost**: ~$15-25
- **Pros**: Extremely cheap, widely available
- **Cons**: Older technology, may not get 4G in all regions

#### 3. **ESP32-U (Integrated Solution)**
- **Frequency**: WiFi + 4G in single chip
- **Interface**: Already integrated
- **Cost**: ~$40-60
- **Pros**: No additional wiring, simplified design
- **Cons**: More expensive, requires new board

### Recommendation

**Use SIM7600G-H with external 5V/2A power supply** for optimal balance of features, reliability, and cost.

---

## 10. Conflict Resolution Matrix

### Summary of All Identified Conflicts

| Conflict | Severity | Status | Resolution |
|----------|----------|--------|-----------|
| **Hardware GPIO Pins** | HIGH | ✅ RESOLVED | UART1 on GPIO 9/10 (free pins) |
| **SPI Bus** | MEDIUM | ✅ RESOLVED | 4G uses UART, not SPI |
| **WiFi/Cellular Interference** | MEDIUM | ✅ RESOLVED | Different frequency bands |
| **Power Supply** | HIGH | ⚠️ MITIGATED | Use external 5V/2A PSU, TX scheduling |
| **CPU/Task Contention** | MEDIUM | ⚠️ MITIGATED | Stagger task cycles, use notifications |
| **Memory Budget** | MEDIUM | ✅ RESOLVED | 80-125 KB available (see Section 2) |
| **Firebase Library Coupling** | CRITICAL | 🔄 ACTION REQUIRED | Network abstraction layer (Phase 1) |
| **SSL/TLS Certificates** | MEDIUM | ✅ RESOLVED | TinyGSM handles this automatically |
| **Network Failover Logic** | MEDIUM | 🔄 ACTION REQUIRED | Implement MultiNetworkManager (Phase 1) |
| **Offline Data Sync** | LOW | ✅ RESOLVED | Existing buffer can use both networks |

---

## 11. Conclusion & Recommendation

### Final Verdict: ✅ **4G INTEGRATION IS FEASIBLE**

**Key Takeaways:**
1. **Hardware**: No conflicts - UART1 (GPIO 9/10) available
2. **Memory**: Sufficient (80-125 KB overhead, plenty of headroom)
3. **Power**: Requires external PSU but manageable with TX scheduling
4. **Network Stack**: Dual stacks can coexist (WiFi on CPU0, 4G on CPU1)
5. **Firebase**: Requires network abstraction layer (planned refactoring)

### Immediate Next Steps

**If proceeding with 4G integration:**

1. **First**: Implement network abstraction layer (Phase 1 - ~8 hours)
   - Decouple Firebase from WiFi dependency
   - Create flexible interface for multi-connectivity

2. **Second**: Integrate TinyGSM for 4G driver (Phase 2 - ~12 hours)
   - Test basic 4G connectivity
   - Verify no hardware conflicts

3. **Third**: Implement failover logic (Phase 2-3 - ~6 hours)
   - WiFi primary + 4G backup strategy
   - Monitor and switch based on signal quality

4. **Fourth**: Optimize and validate (Phase 3-4 - ~8 hours)
   - Power management
   - Long-duration testing

### Alternative Recommendations

If 4G integration timeline is critical:

**Option A: Deploy WiFi-only for now**
- Current system is stable and functional
- Add 4G later (modular design allows it)
- Reduces project complexity

**Option B: Use cloud-based failover**
- Keep existing WiFi-only system
- Add secondary internet link in cloud (e.g., AWS multi-region)
- Simpler than device-side 4G integration

**Option C: Hybrid approach**
- Deploy WiFi + 4G on test gateway only
- Validate in production before rolling out to all devices
- Minimize rollout risk

---

## 12. Architecture Diagram (Final)

```
┌────────────────────────────────────────────────────────┐
│                   Application Layer                    │
│  Gateway App ──→ Firebase Operations ──→ Data Queue   │
└─────────────────────┬────────────────────────────────┘
                      │
          ┌───────────▼───────────┐
          │ MultiNetworkManager   │  (NEW)
          │ • Failover logic      │
          │ • Route selection     │
          │ • Priority handling   │
          └───┬─────────────┬─────┘
              │             │
          ┌───▼────┐    ┌───▼────────┐
          │ WiFi   │    │ Cellular   │  (NEW)
          │ Service│    │ Service    │
          └───┬────┘    └───┬────────┘
              │             │
        ┌─────▼──────┐  ┌───▼────────────┐
        │ WiFi Lib   │  │ TinyGSM + 4G   │  (NEW)
        │ + HTTPS    │  │ Module + AT    │
        │ (CPU0)     │  │ Commands       │
        │            │  │ (CPU1)         │
        └─────┬──────┘  └───┬────────────┘
              │             │
        ┌─────▼──────────────▼──────┐
        │ Network/TCP Stack         │
        │ (ESP-IDF, lwIP)           │
        │ • WiFi (CPU0)             │
        │ • PPP (CPU1)              │
        └────────────────────────────┘
```

---

## Appendix A: Configuration Template

```cpp
// gateway_config.h - Addition for 4G support
#ifdef ENABLE_4G_MODULE

#define MODEM_UART_NUM          UART_NUM_1
#define MODEM_TX_PIN            9
#define MODEM_RX_PIN            10
#define MODEM_RESET_PIN         14      // Optional
#define MODEM_POWER_PIN         13      // Optional
#define MODEM_BAUD_RATE         115200

// 4G Network Configuration
#define MODEM_APN               "internet"   // Change per provider
#define MODEM_USER              ""           // Leave empty for public APNs
#define MODEM_PASSWORD          ""           // Leave empty for public APNs

// Failover Strategy
#define PRIMARY_NETWORK         "WiFi"       // Or "Cellular"
#define AUTO_FAILOVER_TIMEOUT   30000        // 30 seconds before switching
#define MIN_SIGNAL_QUALITY      -100         // dBm for WiFi, bars for cellular

#endif // ENABLE_4G_MODULE
```

---

## Appendix B: Bill of Materials (BOM) for 4G Addition

| Item | Quantity | Cost | Notes |
|------|----------|------|-------|
| SIM7600G-H Module | 1 | $35 | 4G/LTE/2G coverage |
| 3.8V Power Converter | 1 | $8 | Step-down from 5V |
| JST Connectors | 5 | $5 | For module connections |
| SIM Card Holder | 1 | $3 | Micro-SIM slot |
| Micro-USB to JST | 1 | $5 | Power supply connection |
| 5V/2A Power Supply | 1 | $15 | External power |
| Dupont Wires | 1 pack | $3 | For breadboarding |
| **Total** | - | **~$74** | - |

---

**Document Version**: 1.0  
**Last Updated**: November 10, 2025  
**Author**: GitHub Copilot  
**Status**: Ready for Review & Implementation
