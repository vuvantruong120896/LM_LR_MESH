# PHASE 1 Test Guide: UART Driver & AT Command Handler

**Date**: November 11, 2025  
**Module**: A7682S 4G LTE Module  
**Status**: Ready for Testing

---

## 📋 **Pre-Test Checklist**

### **Hardware Setup**
- [ ] A7682S module connected to ESP32
- [ ] Power supply capable of 2A peak current
- [ ] SIM card inserted and activated (with data plan)
- [ ] Antenna connected to A7682S

### **Wiring Verification**
| ESP32 Pin | A7682S Pin | Function |
|-----------|------------|----------|
| IO40 (TX) | RXD | UART Transmit |
| IO41 (RX) | TXD | UART Receive |
| IO39 | PWR_KEY | Power Enable (active HIGH) |
| IO38 | NETLIGHT | Network Status (optional) |
| GND | GND | Ground |
| 5V/3.8V | VCC | Power (check A7682S specs!) |

**⚠️ IMPORTANT**: A7682S requires 3.3V-4.3V (typical 3.8V). Do NOT use 5V directly!

---

## 🚀 **Build and Upload**

### **Step 1: Build Test Firmware**
```bash
# In project root directory
pio run -e test-cellular-phase1
```

### **Step 2: Upload to ESP32**
```bash
pio run -e test-cellular-phase1 -t upload
```

### **Step 3: Open Serial Monitor**
```bash
pio device monitor -e test-cellular-phase1
```

Or use VS Code PlatformIO extension: Click "Serial Monitor" icon

---

## 🧪 **Expected Test Results**

### **Test Sequence**

#### **STEP 1: Initialize UART**
```
>>> STEP 1: Initialize UART
✅ PASSED: UART initialized
```
**If Failed**: Check pin definitions in code match your hardware

---

#### **STEP 2: Power On Module**
```
>>> STEP 2: Power On Module
Powering on A7682S (wait 5 seconds)...
✅ PASSED: Module powered on
```
**If Failed**: 
- Check PWR_KEY pin connection
- Ensure power supply provides enough current (2A peak)
- Check module power LED

---

#### **STEP 3: Create AT Handler**
```
>>> STEP 3: Create AT Command Handler
✅ PASSED: AT handler created
```

---

#### **STEP 4: Test AT Communication**
```
>>> STEP 4: Test AT Communication
Sending AT command (3 retries)...
✅ PASSED: Module responds to AT
```
**If Failed**: 
- Check TX/RX pins (they must be crossed!)
- Check baud rate (should be 115200 for A7682S)
- Check UART connections
- Try swapping TX/RX pins
- Wait longer (module takes ~5-10s to boot)

**Common Issues**:
- TX/RX reversed: Swap IO40 ↔ IO41
- Wrong baud rate: A7682S default is 115200
- Module not powered: Check power LED
- Module booting: Wait 10 seconds after power on

---

#### **STEP 5: Disable Echo**
```
>>> STEP 5: Disable Echo (ATE0)
✅ PASSED: Echo disabled
```

---

#### **STEP 6: Check SIM Card**
```
>>> STEP 6: Check SIM Card (AT+CPIN?)
Response: +CPIN: READY
✅ PASSED: SIM card ready
```

**Possible Responses**:
- `+CPIN: READY` ✅ SIM ready
- `+CPIN: SIM PIN` ⚠️ PIN required (need to enter PIN)
- `+CPIN: SIM PUK` ❌ PUK required (SIM locked)
- `+CME ERROR: 10` ❌ SIM not inserted
- `+CME ERROR: 13` ❌ SIM failure

**If "SIM PIN" required**:
```cpp
// Add in setup() after testAT():
atHandler->sendCommand("+CPIN=1234");  // Replace 1234 with your PIN
```

---

#### **STEP 7: Check Signal Quality**
```
>>> STEP 7: Check Signal Quality (AT+CSQ)
Response: +CSQ: 25,0
  RSSI: 25 (0-31, 99=unknown)
  BER: 0 (0-7, 99=unknown)
✅ PASSED: Good signal quality
```

**RSSI Values**:
- `0`: -113 dBm or less (no signal)
- `1`: -111 dBm
- `2-9`: -109 to -95 dBm (poor)
- `10-14`: -93 to -85 dBm (fair)
- `15-19`: -83 to -75 dBm (good)
- `20-30`: -73 to -53 dBm (excellent)
- `31`: -51 dBm or greater
- `99`: Unknown

**If weak/no signal**:
- Check antenna connection
- Move to location with better coverage
- Check SIM card is activated

---

#### **STEP 8: Check Network Registration**
```
>>> STEP 8: Check Network Registration (AT+CREG?)
Response: +CREG: 0,1
  Mode: 0
  Status: 1 (registered, home network)
✅ PASSED: Registered on home network
```

**Status Values**:
- `0`: Not registered, not searching ❌
- `1`: Registered, home network ✅
- `2`: Not registered, searching ⏳
- `3`: Registration denied ❌
- `5`: Registered, roaming ✅

**If status = 2 (searching)**:
- Wait 30-60 seconds
- Check SIM card network compatibility
- Check signal quality

**If status = 3 (denied)**:
- Check SIM card is activated
- Check account balance
- Contact carrier

---

#### **STEP 9: Get IMEI**
```
>>> STEP 9: Get IMEI (AT+GSN)
IMEI: 868822040123456
✅ PASSED: IMEI retrieved
```

**IMEI**: 15-digit unique identifier (will replace MAC address in gateway)

---

#### **STEP 10: Get Operator**
```
>>> STEP 10: Get Operator (AT+COPS?)
Operator: +COPS: 0,0,"Viettel",7
✅ PASSED: Operator info retrieved
```

**Format**: `+COPS: <mode>,<format>,"<oper>",<act>`
- `<oper>`: Operator name (e.g., "Viettel", "Vinaphone", "Mobifone")
- `<act>`: Access technology (7 = LTE)

---

## ✅ **Success Criteria**

All steps should show **✅ PASSED**:
- [x] UART initialized
- [x] Module powered on
- [x] Module responds to AT
- [x] Echo disabled
- [x] SIM card ready
- [x] Good signal quality (RSSI ≥ 10)
- [x] Network registered (status 1 or 5)
- [x] IMEI retrieved
- [x] Operator info retrieved

---

## 🔄 **Periodic Status Check**

After initial tests, the program runs a periodic check every 30 seconds:

```
--- Periodic Status Check ---
Signal Quality: 25,0
Network Status: 0,1
-----------------------------
```

This verifies the module maintains connection.

---

## 🐛 **Troubleshooting**

### **No response from module**
1. Check power supply (2A minimum)
2. Swap TX/RX pins
3. Wait 10 seconds after power on
4. Check baud rate (115200)
5. Manually test with AT commands in serial monitor

### **SIM not ready**
1. Check SIM card is properly inserted
2. Check SIM card is activated
3. Try SIM card in phone to verify
4. Check PIN if required

### **No signal**
1. Check antenna connection
2. Check SIM card network type (LTE)
3. Move to area with better coverage
4. Check APN settings (Phase 2)

### **Network registration fails**
1. Wait 60 seconds (network search takes time)
2. Check SIM card balance
3. Check network compatibility
4. Try manual operator selection:
   ```cpp
   atHandler->sendCommand("+COPS=1,2,\"45201\"");  // Viettel
   ```

---

## 📊 **Test Log Example**

Save this output for reference:

```
=================================
PHASE 1 TEST: UART & AT Commands
=================================

>>> STEP 1: Initialize UART
✅ PASSED: UART initialized

>>> STEP 2: Power On Module
Powering on A7682S (wait 5 seconds)...
✅ PASSED: Module powered on

>>> STEP 4: Test AT Communication
Sending AT command (3 retries)...
✅ PASSED: Module responds to AT

>>> STEP 6: Check SIM Card (AT+CPIN?)
Response: +CPIN: READY
✅ PASSED: SIM card ready

>>> STEP 7: Check Signal Quality (AT+CSQ)
Response: +CSQ: 25,0
  RSSI: 25 (0-31, 99=unknown)
  BER: 0 (0-7, 99=unknown)
✅ PASSED: Good signal quality

>>> STEP 8: Check Network Registration (AT+CREG?)
Response: +CREG: 0,1
  Mode: 0
  Status: 1 (registered, home network)
✅ PASSED: Registered on home network

>>> STEP 9: Get IMEI (AT+GSN)
IMEI: 868822040123456
✅ PASSED: IMEI retrieved

=================================
PHASE 1 TEST COMPLETE!
=================================
```

---

## ➡️ **Next Steps**

Once all tests pass:
1. ✅ Save IMEI (will use as gateway ID)
2. ✅ Verify network registration is stable
3. ✅ Ready for **Phase 2: Cellular Connection Service**

**Phase 2 will implement**:
- Network attach (GPRS/LTE)
- PDP context activation
- APN configuration
- Auto-reconnect logic
- Event callbacks

---

## 📝 **Notes**

- **Power consumption**: A7682S draws ~500mA idle, up to 2A during transmission
- **Boot time**: ~5-10 seconds after power on
- **Network search**: Can take 30-60 seconds on first boot
- **URC messages**: Module sends unsolicited messages (e.g., `+CREG: 1`)

---

**Test Status**: ⏳ Awaiting hardware test  
**Author**: AI Assistant  
**Last Updated**: November 11, 2025
