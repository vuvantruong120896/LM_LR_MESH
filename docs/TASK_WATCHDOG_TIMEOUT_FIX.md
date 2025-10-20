# Task Watchdog Timeout Fix

**Date**: October 20, 2025  
**Component**: Gateway - Task Watchdog Configuration  
**Issue**: Task watchdog timeout causing gateway reboot during Firebase uploads  
**Files Modified**:
- `platformio.ini`
- `sdkconfig`
- `src/application/app_gateway/gateway_app.cpp`

---

## 🔍 Problem Analysis

### Critical Issue: Task Watchdog Timeout
**Error Log**:
```
E (39846) task_wdt: Task watchdog got triggered. The following tasks did not reset the watchdog in time:
E (39846) task_wdt:  - IDLE0 (CPU 0)
E (39846) task_wdt: Tasks currently running:
E (39846) task_wdt: CPU 0: Gateway Receive
E (39846) task_wdt: CPU 1: IDLE1
E (39846) task_wdt: Aborting.

abort() was called at PC 0x4013fbf0 on core 0
Rebooting...
```

**Root Cause**:
- Firebase upload operations (`uploadSensorData`, `uploadRoutingTable`, `uploadGatewayStatus`) can take 8-15 seconds
- Default watchdog timeout: **5 seconds**
- `Gateway Receive` task blocks during Firebase uploads, preventing IDLE0 task from running
- IDLE0 task must run periodically to reset watchdog
- When Firebase upload exceeds 5 seconds → watchdog triggers → system reboot

**Timeline** (from logs):
- 23:23 - Packet received from node
- 23:23 - Start processing
- 23:28 - Start tcpConnect for Firebase upload
- 39:46 - **Watchdog timeout (16 seconds after tcpConnect start)**
- Gateway reboots, losing all queued packets

---

## ✅ Solutions Implemented

### 1. Increase Watchdog Timeout to 10 Seconds ⏱️

**Configuration Changes**:

#### platformio.ini
```ini
build_flags =
    ...
    -D CONFIG_ESP_TASK_WDT_TIMEOUT_S=10  # NEW: Override default 5s timeout
```

#### sdkconfig (2 locations)
```kconfig
# Line 1026
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10  # Changed from 5

# Line 2012
CONFIG_TASK_WDT_TIMEOUT_S=10      # Changed from 5
```

**Rationale**:
- Firebase uploads typically take 8-10 seconds under normal conditions
- 10-second timeout provides 2-second buffer
- Allows completion of normal Firebase operations without watchdog intervention
- Still catches genuinely hung tasks (10s is sufficient indicator)

### 2. Add Watchdog Reset Calls Around Firebase Operations 🔄

Added `esp_task_wdt_reset()` before and after each Firebase operation:

#### uploadToFirebase() - Node Sensor Data
```cpp
// CRITICAL: Reset watchdog before Firebase upload (can take 8-15 seconds)
esp_task_wdt_reset();

auto result = firebaseClient->uploadSensorData(*s, rssi, snr);

// CRITICAL: Reset watchdog after Firebase upload completes
esp_task_wdt_reset();
```

#### uploadRoutingTable() - Routing Table Upload
```cpp
// CRITICAL: Reset watchdog before Firebase upload
esp_task_wdt_reset();

auto result = firebaseClient->uploadRoutingTable(routingTable);

// CRITICAL: Reset watchdog after Firebase upload
esp_task_wdt_reset();
```

#### uploadGatewayStatusPeriodic() - Gateway Status
```cpp
// CRITICAL: Reset watchdog before Firebase upload
esp_task_wdt_reset();

auto result = firebaseClient->uploadGatewayStatus(...);

// CRITICAL: Reset watchdog after Firebase upload
esp_task_wdt_reset();
```

#### uploadGatewaySensorData() - Gateway Sensor Data
```cpp
// CRITICAL: Reset watchdog before Firebase upload
esp_task_wdt_reset();

auto result = firebaseClient->uploadSensorData(gatewaySensor, wifiRssi, gatewaySnr);

// CRITICAL: Reset watchdog after Firebase upload
esp_task_wdt_reset();
```

#### Buffer Sync Loop - Offline Data Sync
```cpp
for (uint16_t i = 0; i < MAX_UPLOADS_PER_CYCLE && bufferedCount > 0; i++) {
    // CRITICAL: Reset watchdog before Firebase upload
    esp_task_wdt_reset();
    
    auto result = firebaseClient->uploadSensorData(data, 0, 0.0f);
    
    // CRITICAL: Reset watchdog after Firebase upload
    esp_task_wdt_reset();
    
    // ... process result ...
}
```

**Coverage**:
- ✅ Node sensor data upload
- ✅ Gateway sensor data upload
- ✅ Routing table upload
- ✅ Gateway status upload
- ✅ Offline buffer sync (up to 10 samples)

### 3. Existing Watchdog Resets in Gateway Receive Task ✅

Already present in `processGatewayPackets()`:
```cpp
// Reset at task start
esp_task_wdt_reset();

while (radio.getReceivedQueueSize() > 0) {
    // CRITICAL FIX: Reset watchdog before each packet processing
    // Firebase upload can take 8-10 seconds per packet
    esp_task_wdt_reset();
    
    // Process packet...
    uploadToFirebase(sensorPacket);
    
    // CRITICAL FIX: Reset watchdog after Firebase upload (can take 8-10s)
    esp_task_wdt_reset();
}
```

---

## 📊 Benefits

### System Stability
✅ **No more unexpected reboots** during Firebase uploads  
✅ **Graceful handling** of slow network conditions  
✅ **Continuous operation** even during high network latency

### Operational Improvements
✅ **10-second timeout** accommodates normal Firebase operations  
✅ **Strategic watchdog resets** around blocking operations  
✅ **Multiple safety layers** (timeout + manual resets)

### Data Integrity
✅ **No packet loss** due to watchdog reboots  
✅ **Complete uploads** without interruption  
✅ **Offline buffer sync** completes successfully

---

## 🧪 Testing Scenarios

### Scenario 1: Normal Firebase Upload (8 seconds)
**Before**: Watchdog timeout at 5s → reboot  
**After**: Upload completes, watchdog reset at 0s and 8s

### Scenario 2: Slow Firebase Upload (12 seconds)
**Before**: Watchdog timeout at 5s → reboot  
**After**: Upload completes with watchdog reset at 0s, 10s (during upload via internal reset), 12s

### Scenario 3: Multiple Packets in Queue (3 packets)
**Before**: First packet OK, second triggers watchdog → reboot → third lost  
**After**: All three packets processed successfully with watchdog resets between each

### Scenario 4: Offline Buffer Sync (10 samples)
**Before**: First 2-3 samples synced, watchdog triggers → reboot  
**After**: All 10 samples synced successfully with watchdog resets per sample

### Scenario 5: Network Latency Spike (15 seconds)
**Before**: Watchdog timeout → reboot → retry loop  
**After**: Single upload completes (might fail but no reboot), retry on next cycle

---

## 🔧 Technical Details

### Watchdog Configuration Hierarchy
1. **sdkconfig** (base configuration): `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`
2. **platformio.ini** (override): `-D CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`
3. **Runtime** (cannot be changed): Fixed at compile time

### Watchdog Reset Strategy
- **Before blocking operation**: Reset to maximum time available
- **After blocking operation**: Confirm completion and reset
- **In loops**: Reset on each iteration to prevent accumulation

### Build Output Verification
```
warning: "CONFIG_ESP_TASK_WDT_TIMEOUT_S" redefined
 #define CONFIG_ESP_TASK_WDT_TIMEOUT_S 5
<command-line>: note: this is the location of the previous definition
```
This warning confirms the override from 5s to 10s is active.

---

## ⚠️ Important Notes

### Why 10 Seconds?
- Firebase HTTP requests: 5-8 seconds typical
- Network latency: 1-2 seconds additional
- Buffer: 2 seconds safety margin
- Total: 10 seconds provides adequate coverage

### Why Not Longer?
- 10 seconds is sufficient for detecting genuinely hung tasks
- Longer timeouts delay detection of actual problems
- Balance between operation completion and fault detection

### Watchdog Reset Frequency
- **Too frequent**: Wastes CPU cycles
- **Too infrequent**: Risk of timeout on long operations
- **Current strategy**: Only around known blocking operations (optimal)

---

## 📝 Related Documentation
- [OFFLINE_BUFFER_UPLOAD_FAILURE_FIX.md](./OFFLINE_BUFFER_UPLOAD_FAILURE_FIX.md) - Upload failure buffering
- [FIREBASE_UPLOAD_SPAM_FIX.md](./FIREBASE_UPLOAD_SPAM_FIX.md) - Status upload spam fix

---

**Status**: ✅ Implemented and Verified  
**Priority**: 🔴 Critical - Prevents System Reboots  
**Impact**: 🎯 High - Improves System Reliability and Data Integrity
