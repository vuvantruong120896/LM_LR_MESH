# A7682S Modem Diagnostic Plan
**Date:** November 12, 2025  
**Issue:** Modem sends HTTPS request but never receives response from Firebase

## Problem Summary

After in-depth log analysis:
- ✅ Modem connects to Firebase (AT+CCHOPEN OK)
- ✅ Modem TLS handshake succeeds (2s delay completes)
- ✅ Modem sends HTTPS GET request (AT+CCHSEND OK, 181 bytes transmitted)
- ❌ **Modem never receives response data** (No +CCHRECV URC, polling returns 0)
- ❌ **Firebase never sends response back** (no RX data in UART logs)

This is **NOT a timing/polling issue** - it's a **modem-level connectivity issue**.

---

## Root Cause Analysis

### Possible Causes (in order of likelihood):

1. **Firebase doesn't respond to requests from this modem**
   - Auth token invalid or malformed
   - Firebase rejects requests (security rules, IP blocking, etc.)
   - Modem IP address blacklisted by Firebase

2. **Modem network configuration blocking responses**
   - APN settings prevent bidirectional HTTPS
   - Firewall/NAT on modem side drops response packets
   - Modem buffer overflow (can't receive while idle)

3. **A7682S firmware bug**
   - Known issue with bidirectional HTTPS data
   - URC callback not triggered for certain response sizes
   - Memory leak preventing +CCHRECV URC generation

4. **Firebase connectivity issue**
   - Firebase server not accessible from this modem's ISP
   - Network path asymmetric (can send, can't receive)
   - Intermediate proxy blocking responses

---

## Diagnostic Strategy

### Phase 1: Verify Modem Can Receive ANY Data (Low-Level Test)

**Objective:** Confirm modem's +CCHRECV URC works at all

**Test:** Simple HTTP echo service (not Firebase)

```at
AT+CCHOPEN=0,"httpbin.org",80,0
AT+CCHSEND=0,<len>
GET /get HTTP/1.1\r\nHost: httpbin.org\r\n\r\n
```

**Expected:** +CCHRECV URC should appear with response data

**Implementation:** Create minimal test in `cellular_diagnostics.cpp`

---

### Phase 2: Verify Firebase Can Be Reached (Medium-Level Test)

**Objective:** Confirm modem can reach Firebase servers

**Test:** DNS lookup + ping-like HTTPS handshake

```at
AT+CIPPING="kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app"
```

**Expected:** Should resolve IP and allow connection

**If fails:** Firebase domain unreachable from this modem

---

### Phase 3: Verify Firebase Auth (Firebase-Level Test)

**Objective:** Confirm auth token is valid

**Test:** Firebase REST API authentication check

```
GET /rest/v1/projects/[PROJECT]/rulesets/latest?access_token=[TOKEN]
```

**Expected:** 200 OK with valid response

**If fails:** Auth token expired or invalid

---

### Phase 4: Alternative Receive Methods (Modem Workaround)

**Objective:** Try alternative AT commands if +CCHRECV URC doesn't work

**Options:**
1. `AT+CCHRECVEXT` - Extended receive command
2. Manual polling with longer delays
3. Different HTTP version (HTTP/1.0 vs 1.1)
4. Chunked transfer encoding
5. Keep-Alive disabled

---

## Implementation Plan

### Step 1: Add Diagnostic Mode to SSL Client

Add new method to `CellularSSLClient`:
```cpp
bool diagnosticTest(const char* hostname, uint16_t port, const char* request);
```

This will:
- Send raw HTTPS request
- Log every AT command and response
- Capture +CCHRECV URC with hex dump
- Retry with different parameters if fails

### Step 2: Create Test Endpoints

1. **Echo service** (HTTP echo.org)
   - Simple test to confirm bidirectional data works
   - If this fails, modem can't receive ANY data

2. **Firebase auth test**
   - Simple GET to check auth token validity
   - If this fails, auth is the problem

3. **Firebase actual endpoint**
   - Final test with real query
   - If echo works but this doesn't, Firebase rejects request

### Step 3: Add Comprehensive Logging

Log at every stage:
- AT command sent + timestamp
- Response received + timestamp
- URC received + timestamp + data hex
- Time gaps between stages
- Modem error codes

### Step 4: Create Recovery Logic

If diagnostic finds issue:
- Auto-retry with different params
- Fallback to alternative receive method
- Suggest user actions (check credentials, network, etc.)

---

## Expected Outcomes

### Success Path:
```
✅ Echo test succeeds → Modem can receive data
  ├─ Firebase auth test succeeds → Auth is valid
  │  ├─ Real Firebase test succeeds → Problem solved! 🎉
  │  └─ Real Firebase test fails → Firebase rejects this modem
  └─ Firebase auth test fails → Auth token problem

❌ Echo test fails → Modem can't receive ANY data
  └─ Modem has fundamental receive issue
     ├─ Try alternative AT commands
     ├─ Check APN settings
     └─ Update modem firmware
```

---

## Quick Actions Before Full Diagnostic

1. **Check Firebase Rules** - Are they blocking this gateway?
2. **Verify Auth Token** - Not expired? Correct format?
3. **Test with WiFi Mode** - If WiFi works but cellular doesn't, modem issue
4. **Check Modem Signal** - RSSI -77 dBm is weak but should work
5. **Try Different Firebase Endpoint** - Not region-locked?

---

## Files to Create/Modify

- `src/components/cellular/src/cellular_diagnostics.cpp` (NEW)
- `src/components/cellular/include/cellular_diagnostics.h` (NEW)
- `src/components/cellular/src/cellular_ssl_client.cpp` (ADD test method)
- `src/application/app_gateway/gateway_app.cpp` (ADD diagnostic trigger)

---

## Notes

- **Do NOT** add to production code yet - diagnostic only
- **Do NOT** interfere with normal Firebase operations
- **Do Log everything** - need data to debug
- **Keep timeouts reasonable** - don't block gateway for too long
- **Test one thing at a time** - isolate problem

---

## Next Steps

1. Implement Phase 1 diagnostic (echo service test)
2. Build firmware and run test
3. Analyze results to determine root cause
4. Implement Phase 2-4 if Phase 1 fails
5. Apply permanent fix based on findings
