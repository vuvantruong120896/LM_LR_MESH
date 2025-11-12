# PHASE 2 Test Guide: Cellular Connection Service

**Date**: November 11, 2025  
**Module**: A7682S 4G LTE Module  
**Status**: Ready for Testing

---

## 📋 **Pre-Test Checklist**

### **Hardware Setup**
- [x] Phase 1 tests passed successfully
- [x] Module powered and responding to AT commands
- [x] SIM card inserted and ready
- [x] Active cellular coverage in your area
- [x] SIM card has data plan activated

### **APN Configuration**
⚠️ **CRITICAL**: You MUST configure the correct APN for your carrier!

**Edit `test/test_cellular_phase2.cpp`**:
```cpp
// Line 29-31: Change to your carrier's APN
const char* APN = "v-internet";  // ← CHANGE THIS!
const char* APN_USER = "";       // Usually empty
const char* APN_PASS = "";       // Usually empty
```

**Common APNs**:

| Carrier (Vietnam) | APN | User | Pass |
|------------------|-----|------|------|
| Viettel | `v-internet` or `e-internet` | - | - |
| Vinaphone | `e-connect` or `m3-world` | - | - |
| Mobifone | `m-wap` | `mms` | `mms` |

**Other countries**: Check with your carrier or search online for APN settings.

---

## 🚀 **Build and Upload**

### **Step 1: Build Test Firmware**
```bash
pio run -e test-cellular-phase2
```

**Expected Output**:
```
Processing test-cellular-phase2...
Compiling...
Linking...
Building .pio/build/test-cellular-phase2/firmware.bin
Success
```

### **Step 2: Upload to ESP32**
```bash
pio run -e test-cellular-phase2 -t upload
```

### **Step 3: Open Serial Monitor**
```bash
pio device monitor -e test-cellular-phase2
```

Or use VS Code PlatformIO: Click "Serial Monitor" icon

---

## 🧪 **Expected Test Results**

### **Test Sequence**

#### **STEP 1: Create Service**
```
>>> STEP 1: Create Cellular Connection Service
APN: v-internet
✅ PASSED: Service created
```

---

#### **STEP 2: Register Event Callback**
```
>>> STEP 2: Register Event Callback
✅ PASSED: Event callback registered
```

**What happens**: Event callback is registered to receive connection events

---

#### **STEP 3: Set Signal Threshold**
```
>>> STEP 3: Set Signal Quality Threshold
✅ PASSED: Threshold set to 10
```

**What happens**: Service will trigger `SIGNAL_LOW` event if RSSI drops below 10

---

#### **STEP 4: Initialize Service**
```
>>> STEP 4: Initialize Service
This will:
  - Power on module
  - Test AT communication
  - Check SIM card
  - Get IMEI

✅ PASSED: Service initialized

IMEI: 868822040123456
Signal Quality: 25
```

**Duration**: ~10-15 seconds

**If Failed**:
- Check Phase 1 test results
- Verify SIM card is inserted
- Check power supply

---

#### **STEP 5: Connect to Network** (Critical Step!)
```
>>> STEP 5: Connect to Cellular Network
This will:
  - Wait for network registration (up to 60s)
  - Configure APN
  - Attach to GPRS/LTE
  - Activate PDP context
  - Get IP address

⏳ Please wait (this may take 30-90 seconds)...

[Connection sequence logs...]

🎉 EVENT: PDP_ACTIVATED (RSSI: 25)
🎉 EVENT: CONNECTED (RSSI: 25)
   IP: 10.123.45.67
   Operator: Viettel

✅ PASSED: Connected to network

┌─────────────────────────────────────
│ CONNECTION INFO
├─────────────────────────────────────
│ IMEI:       868822040123456
│ Operator:   Viettel
│ IP Address: 10.123.45.67
│ RSSI:       25
└─────────────────────────────────────
```

**Duration**: 30-90 seconds (varies by network)

**Connection Steps** (visible in logs):
1. Network registration check (polling every 2s)
2. Registration successful (home or roaming)
3. APN configuration
4. GPRS attachment
5. PDP context activation
6. IP address assignment

**If Failed**, check:
- APN settings (most common issue!)
- Signal quality (RSSI should be ≥ 10)
- SIM card activation status
- Account balance
- Network coverage in your area

---

### **Connection Logs (Detailed)**

Successful connection will show:

```
[CELLULAR_CONN] Connecting to cellular network...
[CELLULAR_CONN] APN: v-internet
[CELLULAR_CONN] Waiting for network registration (timeout: 30s)...
[CELLULAR_CONN] ✅ Registered on network: Viettel (Home)
[CELLULAR_CONN] Configuring APN: v-internet
[CELLULAR_CONN] ✅ APN configured
[CELLULAR_CONN] Attaching to GPRS/LTE...
[CELLULAR_CONN] ✅ Attached to GPRS/LTE
[CELLULAR_CONN] Activating PDP context...
[CELLULAR_CONN] ✅ PDP context activated
[CELLULAR_CONN] IP address: 10.123.45.67
[CELLULAR_CONN] ✅ Connected to cellular network (45.2s)
[CELLULAR_CONN] IP: 10.123.45.67, Operator: Viettel, RSSI: 25
```

---

## 🔄 **Periodic Status Reports**

After connection, the service runs periodic reports every 60 seconds:

```
====== PERIODIC STATUS REPORT ======
Timestamp: 120543 ms (2.0 min uptime)
Status: CONNECTED ✅
IP: 10.123.45.67
Operator: Viettel
RSSI: 25
Auto-reconnect: ON

┌─────────────────────────────────────
│ CONNECTION STATISTICS
├─────────────────────────────────────
│ Connection Attempts:    1
│ Successful:             1
│ Failed:                 0
│ Reconnect Attempts:     0
│ Total Connected Time:   2.0 min
│ Current RSSI:           25 (Excellent)
└─────────────────────────────────────

====================================
```

**RSSI Quality**:
- `≥20`: Excellent
- `15-19`: Good
- `10-14`: Fair
- `5-9`: Poor
- `<5`: Very Poor

---

## 🎯 **Event Callbacks**

During operation, you'll see event notifications:

### **Connected Event**
```
🎉 EVENT: CONNECTED (RSSI: 25)
   IP: 10.123.45.67
   Operator: Viettel
```

### **Disconnected Event**
```
❌ EVENT: DISCONNECTED (RSSI: 20)
```

### **Reconnecting Event**
```
🔄 EVENT: RECONNECTING (RSSI: 18)
```

### **Signal Low Event**
```
⚠️  EVENT: SIGNAL_LOW (RSSI: 8)
```

### **Connection Failed Event**
```
⚠️  EVENT: CONNECTION_FAILED (RSSI: 5)
```

---

## 🧪 **Testing Auto-Reconnect** (Optional)

To test auto-reconnect functionality:

1. **Uncomment test code** in `test_cellular_phase2.cpp` (around line 155):
   ```cpp
   // Test reconnect (optional - uncomment to test)
   static bool testReconnect = false;
   if (!testReconnect && millis() > 120000) {
       testReconnect = true;
       Serial.println("\n>>> TESTING AUTO-RECONNECT");
       Serial.println("Manually disconnecting to test auto-reconnect...\n");
       cellularService->disconnect();
   }
   ```

2. **Expected behavior**:
   - After 2 minutes of connection
   - Service disconnects automatically
   - Auto-reconnect triggers after 5 seconds
   - Reconnect interval increases on failure (5s → 7.5s → 11.25s → ...)
   - Resets to 5s on successful reconnection

3. **Example output**:
   ```
   >>> TESTING AUTO-RECONNECT
   Manually disconnecting to test auto-reconnect...
   
   ❌ EVENT: DISCONNECTED (RSSI: 25)
   
   [After 5 seconds]
   🔄 EVENT: RECONNECTING (RSSI: 25)
   [CELLULAR_CONN] Auto-reconnecting (attempt 1, interval 5s)...
   [Connection sequence...]
   🎉 EVENT: CONNECTED (RSSI: 25)
   ✅ Auto-reconnect successful
   ```

---

## 🐛 **Troubleshooting**

### **Problem: Connection Timeout**

**Symptoms**:
```
❌ FAILED: Network connection
Possible causes:
  - No cellular coverage
  - Weak signal (move to better location)
  ...
```

**Solutions**:
1. **Check signal quality**:
   - RSSI should be ≥ 10
   - Move to location with better coverage
   - Check antenna connection

2. **Check APN settings**:
   - Most common issue!
   - Verify APN name for your carrier
   - Try alternative APN (e.g., `e-internet` instead of `v-internet`)

3. **Check SIM card**:
   - Try SIM in phone to verify it works
   - Check data plan is activated
   - Verify account balance

4. **Network registration**:
   - May take 30-90 seconds on first boot
   - Check carrier coverage map
   - Try manual operator selection

---

### **Problem: "SIM card not ready"**

**Solutions**:
1. Check SIM card is properly inserted
2. Wait longer (SIM can take 10-20 seconds to initialize)
3. Power cycle module
4. Try SIM in phone to verify

---

### **Problem: Network registration fails**

**Error**: Registration state stays at `SEARCHING (2)` or `DENIED (3)`

**Solutions**:
1. **SEARCHING**: Wait longer (up to 2 minutes), check coverage
2. **DENIED**: 
   - Check SIM activation
   - Verify account balance
   - Contact carrier

---

### **Problem: PDP context activation fails**

**Error**: `PDP context activation failed`

**Solutions**:
1. Verify APN settings (most likely cause)
2. Check GPRS attachment succeeded
3. Try disconnecting and reconnecting
4. Check carrier supports data

---

### **Problem: No IP address assigned**

**Symptoms**: IP shows as empty or 0.0.0.0

**Solutions**:
1. PDP context may not be fully activated
2. Wait a few seconds after activation
3. Query IP address manually: `AT+IPADDR`
4. Check carrier DHCP server

---

## 📊 **Success Criteria**

All steps should complete successfully:

- [x] Service created
- [x] Event callback registered
- [x] Service initialized
- [x] **Connected to network** ← Most important!
- [x] IP address assigned
- [x] Periodic status reports working
- [x] Statistics tracking
- [x] Auto-reconnect functional (if tested)

---

## 📝 **Test Log Example**

Save this output for reference:

```
==========================================
PHASE 2 TEST: Cellular Connection Service
==========================================

>>> STEP 1-4: [All PASSED]
IMEI: 868822040123456
Signal Quality: 25

>>> STEP 5: Connect to Cellular Network
⏳ Please wait...

[Network registration...]
[APN configuration...]
[GPRS attachment...]
[PDP activation...]

✅ PASSED: Connected to network

┌─────────────────────────────────────
│ CONNECTION INFO
├─────────────────────────────────────
│ IMEI:       868822040123456
│ Operator:   Viettel
│ IP Address: 10.123.45.67
│ RSSI:       25
└─────────────────────────────────────

====== PERIODIC STATUS REPORT ======
[Statistics showing uptime, RSSI, etc.]
====================================
```

---

## ➡️ **Next Steps**

Once all tests pass:

1. ✅ **Save connection details**:
   - IMEI (will replace MAC address)
   - IP address format
   - Operator name

2. ✅ **Verify stability**:
   - Leave running for 10-15 minutes
   - Monitor periodic reports
   - Check for disconnections

3. ✅ **Ready for Phase 3**: TCP/IP Stack
   - Socket management
   - DNS resolution
   - Data transmission

---

**Test Status**: ⏳ Awaiting hardware test  
**Author**: AI Assistant  
**Last Updated**: November 11, 2025
