# Phase 3 - Testing Scenarios & Procedures

## Test Environment Setup

### Hardware Requirements
- 1x Gateway ESP32S3 (with SIM card for Cellular variant)
- 2x Node ESP32S3 (for multi-node testing)
- Mobile Device with Kagri App (iOS/Android)
- BLE-capable laptop for debug (optional)

### Software Requirements
- Kagri Mobile App (with BLE provisioning feature)
- PlatformIO with esp32-gateway and esp32-node environments
- Serial monitor for device logs (baud 115200)

### Pre-test Preparation
```bash
# Build and upload firmware to all devices
pio run -e esp32-gateway -t upload
pio run -e esp32-node -t upload

# Monitor all devices in separate terminals
pio device monitor -e esp32-gateway --port /dev/ttyUSB0 -b 115200
pio device monitor -e esp32-node --port /dev/ttyUSB1 -b 115200
```

---

## Test Case 1: Gateway WiFi Mode Provisioning

### Objective
Verify Gateway can be provisioned via BLE with WiFi credentials and connect to network

### Preconditions
- Gateway flashed with WiFi mode firmware (USE_CELLULAR not defined)
- Gateway not previously provisioned
- WiFi network available with known credentials
- Mobile app installed and ready

### Test Steps

1. **Device Discovery**
   - Launch Kagri Mobile App
   - Navigate to "Add Gateway"
   - Scan for BLE devices
   - **Expected:** Device "KAGRI-GW-XXXX" appears in scan results
   - **Verify:** Device name format correct (XXXX = last 4 MAC digits)

2. **Connect to Gateway**
   - Tap "KAGRI-GW-XXXX" to connect
   - **Expected:** Connection successful within 5 seconds
   - **Verify:** Service UUID 0000ffb0 present with characteristics

3. **WiFi Provisioning**
   - Enter WiFi SSID: `TestNetwork`
   - Enter WiFi Password: `TestPassword123`
   - Tap "Provision"
   - **Expected:** Processing notification appears
   - **Serial Log:**
     ```
     [BLE] Received provisioning data
     [PROV] Validating WiFi...
     [WiFi] Attempting connection to TestNetwork
     [WiFi] Connected! IP: 192.168.1.XXX
     [PROV] Deriving netkey...
     [NVS] Saving provisioning data
     [BLE] Sending response: status=success, gatewayMAC=AA:BB:CC:DD:EE:FF
     ```

4. **Verify Response**
   - **Expected:** Mobile app shows "Gateway provisioned successfully"
   - **Expected:** Gateway MAC displayed: "AA:BB:CC:DD:EE:FF"
   - **Expected:** Completion confirmation

5. **Persistent State**
   - Restart Gateway (power off/on)
   - **Expected:** Device does NOT advertise BLE (provisioned)
   - **Serial Log:** `[PROV] Device already provisioned`

### Expected Results
- ✅ Gateway connects to WiFi
- ✅ Netkey derived and stored
- ✅ Gateway MAC returned to Mobile App
- ✅ Provisioning state persistent

### Failure Handling
| Issue | Symptom | Recovery |
|-------|---------|----------|
| Invalid WiFi | Connection timeout | Check SSID/password in app |
| BLE disconnect | Mid-provisioning failure | Retry from device discovery |
| NVS write error | Provisioning stored but erratic | Format NVS and retry |

---

## Test Case 2: Gateway Cellular Mode Provisioning

### Objective
Verify Gateway can be provisioned via BLE for Cellular mode without WiFi

### Preconditions
- Gateway flashed with Cellular mode firmware (USE_CELLULAR defined)
- SIM card installed and activated
- Gateway not previously provisioned
- Mobile app ready

### Test Steps

1. **Device Discovery**
   - Launch Kagri Mobile App
   - Navigate to "Add Gateway"
   - Scan for BLE devices
   - **Expected:** Device "KAGRI-GW-XXXX" appears

2. **Connect to Gateway**
   - Tap "KAGRI-GW-XXXX" to connect
   - **Expected:** Connection successful

3. **Cellular Provisioning**
   - App detects Cellular mode (may show "No WiFi required" message)
   - Enter userUID from Firebase auth
   - Tap "Provision"
   - **Expected:** Processing notification
   - **Serial Log:**
     ```
     [BLE] Received provisioning data
     [PROV] Cellular mode detected (isWiFi=false)
     [PROV] Skipping WiFi validation
     [PROV] Deriving netkey...
     [CELLULAR] Initializing A7682S modem...
     [CELLULAR] Connecting to cellular network...
     [CELLULAR] Connected! Signal: -80dBm
     [NVS] Saving provisioning data
     [BLE] Sending response: status=success, gatewayMAC=AA:BB:CC:DD:EE:FF
     ```

4. **Verify Response**
   - **Expected:** "Gateway provisioned successfully"
   - **Expected:** Gateway MAC displayed
   - **Verify:** No WiFi SSID requested

5. **Cellular Connection Verification**
   - Monitor gateway's HTTP requests (e.g., NTP time sync, Firebase connection)
   - **Expected:** Cellular connection established
   - **Verify:** Data flowing over cellular modem

### Expected Results
- ✅ Gateway provisioned without WiFi credentials
- ✅ Cellular modem initialized and connected
- ✅ Netkey derived correctly
- ✅ Firebase connectivity works over cellular

---

## Test Case 3: Node BLE Provisioning

### Objective
Verify Node can be provisioned via BLE with Gateway MAC and derives correct netkey

### Preconditions
- Node flashed with BLE provisioning firmware
- Node not previously provisioned
- Gateway already provisioned (MAC address known)
- Mobile app ready with Gateway MAC

### Test Steps

1. **Device Discovery**
   - Launch Kagri Mobile App
   - Navigate to "Add Node"
   - Scan for BLE devices
   - **Expected:** Device "KAGRI-NODE-XXXX" appears
   - **Verify:** Different from Gateway UUID (0000ffc0)

2. **Connect to Node**
   - Tap "KAGRI-NODE-XXXX" to connect
   - **Expected:** Connection successful within 5 seconds

3. **Node Provisioning**
   - Enter Gateway MAC: "AA:BB:CC:DD:EE:FF"
   - Enter userUID from Firebase auth (same as Gateway)
   - Tap "Provision"
   - **Expected:** Processing notification
   - **Serial Log:**
     ```
     [BLE] Received provisioning data
     [PROV] Parsing Gateway MAC: AA:BB:CC:DD:EE:FF
     [PROV] Deriving netkey using Gateway MAC...
     [PROV] Netkey derived successfully
     [PROV] Assigning node address: 2
     [NVS] Saving provisioning data
     [BLE] Sending response: status=success, nodeAddress=2
     ```

4. **Verify Response**
   - **Expected:** "Node provisioned successfully"
   - **Expected:** Node address displayed: "2"
   - **Expected:** Confirmation notification

5. **Mesh Network Join**
   - Gateway and Node on same power supply
   - Monitor serial logs
   - **Expected within 30 seconds:**
     ```
     [Node] Node address: 2
     [Node] Netkey: XXXXXXXXXXX...
     [LoRa] Joining mesh network...
     [LoRa] Router found at address: 0 (Gateway)
     [LoRa] Route to gateway established
     ```

### Expected Results
- ✅ Node provisioned with Gateway MAC
- ✅ Netkey derived using Gateway MAC
- ✅ Node address assigned
- ✅ Node joins LoRa mesh within 30 seconds

---

## Test Case 4: Netkey Consistency (Multi-Node)

### Objective
Verify all nodes derive the SAME netkey when provisioned with same userUID and Gateway MAC

### Preconditions
- Gateway provisioned and running
- 2x Nodes ready to provision
- Both nodes will receive same (userUID, Gateway MAC)
- Serial access to both nodes and gateway

### Test Steps

1. **Provision Node 1**
   - Follow Test Case 3 steps
   - Record netkey from Node 1 serial log
   - **Capture:** `[PROV] Netkey = AABBCCDD...EEFF (32 bytes, hex)`

2. **Provision Node 2**
   - Use same Gateway MAC and userUID
   - Follow Test Case 3 steps
   - Record netkey from Node 2 serial log
   - **Capture:** `[PROV] Netkey = AABBCCDD...EEFF (32 bytes, hex)`

3. **Compare Netkeys**
   - Extract hex values from both serial logs
   - **Expected:** Node 1 netkey == Node 2 netkey
   - **Expected:** Node 1 netkey == Gateway netkey
   - **Verify:** Character-by-character match

4. **Mesh Network Formation**
   - Power on all devices (Gateway + Node1 + Node2)
   - Monitor logs for 60 seconds
   - **Expected:**
     ```
     [Gateway] Router initialized, address: 0
     [Node1] Joined mesh, address: 2
     [Node2] Joined mesh, address: 3
     [Node1] Route to Node2 discovered via Gateway
     ```

5. **Test Multi-Hop Communication**
   - Send test message from Gateway to Node 2 (via Node 1)
   - **Expected:** Message received without errors
   - **Verify:** No "Netkey mismatch" errors in logs

### Expected Results
- ✅ All devices derive identical netkeys
- ✅ Mesh network forms automatically
- ✅ Multi-hop routing works without errors
- ✅ No security rejections in logs

### Netkey Verification Command (Debug)
```
# Extract netkey from logs (Node 1)
uart_read | grep "Netkey" | cut -d'=' -f2

# Expected format:
# 27:AB:F3:4C:2E:9D:1A:FF:...
```

---

## Test Case 5: WiFi to Cellular Mode Switch

### Objective
Verify Gateway firmware can be switched between WiFi and Cellular modes

### Preconditions
- Gateway firmware buildable in both modes
- SIM card available for Cellular testing
- WiFi network available for WiFi testing

### Test Steps

1. **Build and Deploy WiFi Mode**
   ```bash
   # Edit platformio.ini: remove USE_CELLULAR flag
   pio run -e esp32-gateway -t upload
   pio device monitor -e esp32-gateway
   ```

2. **Provision in WiFi Mode**
   - Follow Test Case 1 steps
   - Gateway successfully provisioned with WiFi

3. **Factory Reset**
   ```bash
   # Press factory reset button (or via serial command)
   # Monitor for: [PROV] NVS cleared, device ready for provisioning
   ```

4. **Build and Deploy Cellular Mode**
   ```bash
   # Edit platformio.ini: add -D USE_CELLULAR
   pio run -e esp32-gateway -t upload
   pio device monitor -e esp32-gateway
   ```

5. **Provision in Cellular Mode**
   - Follow Test Case 2 steps
   - Gateway successfully provisioned for Cellular

6. **Verify Mode-Specific Behavior**
   - **WiFi Mode:** Try to download file via HTTP - should work over WiFi
   - **Cellular Mode:** Same test - should work over cellular modem

### Expected Results
- ✅ Firmware builds without errors in both modes
- ✅ Provisioning works in both modes
- ✅ Correct communication channel used
- ✅ Mode can be switched by re-flashing

---

## Test Case 6: Error Handling - Invalid Payloads

### Objective
Verify proper error responses for malformed provisioning data

### Preconditions
- Devices provisioned in Test Cases 1-3
- Manual BLE payload injection capability
- Test framework (e.g., BLE debugging app)

### Test Steps

1. **Invalid userUID (too short)**
   - Send JSON with userUID < 10 chars
   - **Expected:** 
     ```json
     {"status": "error", "code": "ERR_INVALID_PAYLOAD", "message": "Invalid userUID length"}
     ```

2. **Missing Gateway MAC (Node)**
   - Send Node provisioning without gatewayMAC
   - **Expected:** 
     ```json
     {"status": "error", "code": "ERR_INVALID_GATEWAY_MAC", "message": "Gateway MAC required"}
     ```

3. **Invalid WiFi Credentials (Gateway)**
   - Send WiFi SSID but no password (WiFi mode)
   - **Expected:** 
     ```json
     {"status": "error", "code": "ERR_WIFI_MISSING", "message": "WiFi password required"}
     ```

4. **Already Provisioned**
   - Send provisioning data to already-provisioned device
   - **Expected:** 
     ```json
     {"status": "error", "code": "ERR_ALREADY_PROVISIONED", "message": "Device already provisioned. Reset to provision again."}
     ```

### Expected Results
- ✅ All error codes match specification
- ✅ Error messages are descriptive
- ✅ Device remains stable after error
- ✅ Device can retry provisioning without restart

---

## Test Case 7: Persistence & Reboot

### Objective
Verify provisioning data persists across device reboots

### Preconditions
- Devices provisioned (from Test Cases 1-3)
- Serial monitor connected

### Test Steps

1. **Record Initial State**
   - Gateway/Node running and provisioned
   - Capture initial netkey from logs
   - Note node address if applicable

2. **Perform Hard Reboot**
   - Power off device for 10 seconds
   - Power on device
   - **Expected Serial Log:**
     ```
     [BOOT] System starting...
     [NVS] Loading provisioning data...
     [PROV] Device provisioned (user_uid: xxxx...)
     [PROV] Netkey loaded: AABBCCDD...
     [LoRa] Initializing mesh with netkey...
     ```

3. **Verify Persistence**
   - Compare netkey from logs with Step 1
   - **Expected:** Identical match
   - **Verify:** No re-provisioning required

4. **Verify Network Rejoin**
   - **For Node:** Should rejoin mesh within 30 seconds
   - **For Gateway:** Should re-establish as router
   - **Expected:** No address conflicts or key mismatches

5. **Soft Reset (Watchdog/Software Reset)**
   - Trigger software reset via serial command
   - **Expected:** Same behavior as hard reboot
   - **Verify:** Netkey still persisted

### Expected Results
- ✅ Provisioning data survives power loss
- ✅ Device doesn't re-advertise BLE after reboot
- ✅ Mesh network rejoin is automatic
- ✅ No data corruption in NVS

---

## Test Case 8: Factory Reset & Reprovisioning

### Objective
Verify device can be reset and reprovisioned

### Preconditions
- Device provisioned and running
- Factory reset capability available

### Test Steps

1. **Initial Provisioning**
   - Device provisioned with known data
   - Record MAC address

2. **Trigger Factory Reset**
   - Hold factory reset button for 5 seconds
   - **Expected Serial Log:**
     ```
     [FACTORY_RESET] NVS being cleared...
     [NVS] Provisioning data cleared
     [BOOT] System ready for provisioning
     ```

3. **Verify Reset State**
   - Device should start advertising BLE
   - **Expected:** "KAGRI-GW-XXXX" or "KAGRI-NODE-XXXX" appears in BLE scan

4. **Reprovision Device**
   - Follow Test Case 1 (Gateway) or 3 (Node)
   - Can use different WiFi/credentials (for Gateway)
   - Can use different userUID or Gateway MAC (for Node)

5. **Verify New Provisioning**
   - New netkey derived if parameters changed
   - **Expected:** Device operates normally with new config
   - **Verify:** Old data completely overwritten

### Expected Results
- ✅ Factory reset clears all provisioning data
- ✅ Device ready for reprovisioning
- ✅ New provisioning process succeeds
- ✅ No residual data from previous config

---

## Test Case 9: BLE Connection Timeout & Retry

### Objective
Verify proper handling of BLE connection issues

### Preconditions
- Gateway/Node ready to provision
- Mobile app with retry capability

### Test Steps

1. **Initiate BLE Connection**
   - Mobile app attempts to connect to device
   - During connection (before write), move device 50+ meters away
   - **Expected:** Connection timeout after 10 seconds
   - **Expected:** Error message: "Connection timeout - please retry"

2. **Retry Connection**
   - Move device back into range
   - Tap "Retry" in mobile app
   - **Expected:** Connection succeeds on second attempt
   - **Verify:** Device state unchanged (not partially provisioned)

3. **Mid-Provisioning Disconnect**
   - Connect to device
   - Start provisioning write
   - During write (before completion), disconnect
   - **Expected:** Device cancels provisioning
   - **Expected:** Device still ready for new attempt

4. **Recovery**
   - Reconnect to device
   - Provision normally
   - **Expected:** Provisioning succeeds
   - **Verify:** No duplicate or corrupted data

### Expected Results
- ✅ BLE timeouts handled gracefully
- ✅ No partial provisioning states
- ✅ Device recovers automatically
- ✅ Retry succeeds without device reset

---

## Test Case 10: Performance & Scalability

### Objective
Verify system performance with multiple devices and rapid provisioning

### Preconditions
- 1 Gateway + 3 Nodes total
- Gateway already provisioned
- All nodes ready to provision

### Test Steps

1. **Rapid Multi-Node Provisioning**
   - Provision Node 1: Record time to completion
   - Provision Node 2: Record time
   - Provision Node 3: Record time
   - **Expected:** Each provisioning < 10 seconds

2. **Mesh Network Formation**
   - All devices powered on together
   - Monitor mesh formation time
   - **Expected:** All nodes joined mesh within 60 seconds

3. **Network Stability**
   - All 4 devices running for 10 minutes
   - Monitor for errors/warnings in logs
   - **Expected:** No netkey mismatches, connection drops, or timeouts

4. **Message Throughput**
   - Gateway sends 1 message/second to each node
   - Collect delivery statistics
   - **Expected:** >95% message delivery rate

5. **Memory Usage**
   - Monitor heap usage over 10 minutes
   - **Expected:** No continuous growth (no memory leak)
   - **Verify:** Heap usage stable within ±10KB range

### Expected Results
- ✅ Provisioning completes quickly (<10s per device)
- ✅ Multi-node mesh forms reliably
- ✅ Network operates stably for extended periods
- ✅ No memory leaks or performance degradation

---

## Test Execution Log Template

```
TEST CASE: ______________________________
DATE: ______________  TESTER: ______________
FIRMWARE VERSION: ____________  BUILD: ____________

PRECONDITIONS CHECK:
[ ] Hardware configured
[ ] Firmware uploaded
[ ] Serial monitor connected
[ ] Network available (if WiFi test)

TEST STEPS EXECUTION:
Step 1: _______________
  Result: [ ] PASS [ ] FAIL
  Notes: _______________

Step 2: _______________
  Result: [ ] PASS [ ] FAIL
  Notes: _______________

[Continue for all steps...]

OVERALL RESULT: [ ] PASS [ ] FAIL

ISSUES FOUND:
1. _______________
   Severity: [ ] Critical [ ] Major [ ] Minor
   
2. _______________
   Severity: [ ] Critical [ ] Major [ ] Minor

SIGN-OFF: ________________  DATE: ______________
```

---

## Continuous Integration Testing

For automated testing:

```bash
# Run full test suite
./scripts/run_all_tests.sh

# Individual test commands
pio test -e esp32-gateway   # Unit tests
pio test -e esp32-node      # Unit tests

# Manual test coverage
pytest tests/provisioning_tests.py -v
```

---

## Known Limitations & Future Testing

1. **BLE Range Testing**
   - Current: Tested up to 30 meters
   - Future: Test 100+ meter range in open space

2. **Interference Testing**
   - Current: Single WiFi network
   - Future: Test with multiple networks (2.4GHz crowding)

3. **Battery Life Testing**
   - Current: Powered device
   - Future: Battery operation, sleep modes, wake timing

4. **Stress Testing**
   - Current: 3 nodes max
   - Future: 10+ nodes, edge cases with max load

