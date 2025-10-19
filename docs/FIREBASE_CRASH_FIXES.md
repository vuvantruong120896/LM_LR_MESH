# Firebase BearSSL Crash Fixes

## 🔴 Critical Issues Found

### Issue 1: Task Watchdog Timeout
**Symptom:** Gateway reboots during fast provisioning with error:
```
E (58617) task_wdt: Task watchdog got triggered
E (58617) task_wdt: CPU 0: Process routine (blocked >5s)
```

**Root Cause:**
- Firebase upload retry logic blocks packet processing task
- No watchdog reset during retry delays
- Multiple packets: 3 packets × 2 retries × 500ms = 3+ seconds → timeout

**Fix Applied:**
```cpp
// In firebase_client.cpp::uploadToPathWithRetry()
bool FirebaseClient::uploadToPathWithRetry(...) {
    for (uint8_t attempt = 0; attempt < m_maxRetries; attempt++) {
        esp_task_wdt_reset();  // Feed before each attempt
        
        if (uploadToPath(path, jsonData)) {
            delay(50);
            return true;
        }
        
        if (attempt < m_maxRetries - 1) {
            esp_task_wdt_reset();  // Feed before delay
            delay(m_retryDelayMs);
            esp_task_wdt_reset();  // Feed after delay
        }
    }
}
```

---

### Issue 2: BearSSL LoadProhibited Crash
**Symptom:** Gateway crashes with LoadProhibited exception:
```
> ERROR.write: BearSSL returned zero length buffer for sending
Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
EXCVADDR: 0x00000000  (null pointer access)

Backtrace:
- WiFiClientImpl.h:506, 96
- BSSL_SSL_Client.cpp:1916, 1692, 319
```

**Root Cause:**
- BearSSL internal buffer corruption during SSL write
- Firebase ESP32 Client library doesn't handle WiFi disconnection gracefully
- Multiple concurrent uploads can corrupt WiFi client state

**Fix Applied:**
```cpp
// In firebase_client.cpp::uploadToPath()
bool FirebaseClient::uploadToPath(const String& path, const String& jsonData) {
    // CRITICAL: Check WiFi connection before upload
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Firebase] WiFi not connected, skipping upload");
        m_lastError = "WiFi disconnected";
        return false;
    }
    
    // Feed watchdog before/after Firebase operation
    esp_task_wdt_reset();
    success = Firebase.updateNode(m_firebaseData, path.c_str(), json);
    esp_task_wdt_reset();
    
    // Clear buffers to prevent memory corruption
    m_firebaseData.clear();
    
    if (!success) {
        errorReason = m_firebaseData.errorReason();
        
        // CRITICAL: Detect BearSSL errors and force reconnect
        if (errorReason.indexOf("BearSSL") >= 0 || errorReason.indexOf("SSL") >= 0) {
            Serial.printf("[Firebase] BearSSL error, forcing reconnect: %s\n", errorReason.c_str());
            Firebase.reconnectWiFi(true);
            delay(100);  // Allow cleanup
        }
    }
}
```

---

## 📊 Timeline of Issues

### User Test Scenario: Fast Provisioning Mode
1. **T+0s**: User starts provisioning via Mobile App
2. **T+1s**: Gateway enters fast discovery (HELLO every 30s)
3. **T+5s**: Node 0xCC64 discovered, routing table uploaded
4. **T+10s**: Upload routing table starts
5. **T+15s**: BearSSL SSL error: "returned zero length buffer"
6. **T+16s**: LoadProhibited crash → reboot

### Previous Scenario: Watchdog Timeout
1. **T+0s**: Gateway processing multiple packets
2. **T+2s**: Packet 1 upload (2 attempts × 500ms each)
3. **T+4s**: Packet 2 upload (2 attempts × 500ms each)
4. **T+6s**: Watchdog timeout (>5s blocked) → reboot

---

## 🔧 Configuration Changes

### Retry Configuration
```cpp
constexpr uint8_t DEFAULT_MAX_RETRIES = 1;        // 1 retry = 2 total attempts
constexpr uint32_t DEFAULT_RETRY_DELAY_MS = 500;  // Reduced from 1000ms
constexpr uint32_t UPLOAD_TIMEOUT_MS = 5000;      // Reduced from 10s
```

**Rationale:**
- Minimize blocking time in packet processing task
- Reduce chance of watchdog timeout
- Faster failure detection → quicker recovery

---

## 🧪 Testing Recommendations

### Test 1: Fast Provisioning with Multiple Nodes
**Setup:**
- 3+ nodes sending packets simultaneously
- Gateway in fast discovery mode
- Monitor for watchdog timeout or crash

**Expected Result:**
- ✅ No watchdog timeout (watchdog fed during retries)
- ✅ No BearSSL crash (WiFi check + error handling)
- ✅ Sensor data appears on Mobile App within 10 seconds

### Test 2: WiFi Disconnection During Upload
**Setup:**
- Start provisioning
- Disconnect WiFi router mid-upload
- Reconnect WiFi after 30 seconds

**Expected Result:**
- ✅ Gateway detects WiFi disconnection
- ✅ Skips Firebase upload with error log
- ✅ Resumes upload when WiFi reconnects
- ✅ No crash or reboot

### Test 3: SSL Error Handling
**Setup:**
- Force SSL error by using invalid Firebase URL
- Monitor BearSSL error detection

**Expected Result:**
- ✅ BearSSL error detected in error message
- ✅ Firebase.reconnectWiFi(true) called
- ✅ No crash or memory corruption

---

## 📈 Performance Impact

### Before Fixes:
- **Watchdog Timeout Risk:** HIGH (blocks 3-6+ seconds)
- **Crash Risk:** HIGH (BearSSL buffer corruption)
- **Uptime:** LOW (~5 minutes before reboot)

### After Fixes:
- **Watchdog Timeout Risk:** LOW (watchdog fed every 500ms)
- **Crash Risk:** MEDIUM (WiFi check + error handling added)
- **Expected Uptime:** 30+ minutes (limited by Firebase library stability)

---

## ⚠️ Known Limitations

### Firebase ESP32 Client Library v4.4.17
1. **Memory Leaks:** Observed ~64KB heap loss over time
2. **Stack Usage:** TCP connections decrease stack from 4544 → 2000 bytes
3. **BearSSL Stability:** Occasional buffer corruption on SSL errors
4. **Reconnection Issues:** Manual reconnect required on SSL errors

### Recommended Future Improvements
1. **Upgrade Firebase Library:** Test v5.x or newer for stability
2. **Implement Upload Queue:** Separate FreeRTOS task for Firebase uploads
3. **Connection Pooling:** Reuse TCP connections to reduce overhead
4. **Circuit Breaker:** Disable Firebase temporarily after consecutive failures

---

## 📝 Build Information

**Environment:** esp32-gateway  
**Platform:** Espressif 32 (6.9.0)  
**Framework:** Arduino ESP32 v2.0.17  
**Board:** ESP32 DOIT DevKit V1  

**Memory Usage:**
- RAM: 17.7% (57904 / 327680 bytes)
- Flash: 60.8% (1594225 / 2621440 bytes)

**Dependencies:**
- Firebase ESP32 Client: v4.4.17
- ArduinoJson: v7.4.2
- RadioLib: v6.6.0
- WiFi: v2.0.0

---

## 🔍 Debug Commands

### Decode Crash Backtrace
```powershell
cd 'd:\Projects\Lora\LM_LR_MESH'
xtensa-esp32-elf-addr2line -e .pio\build\esp32-gateway\firmware.elf [addresses]
```

### Monitor Serial with Exception Decoder
```powershell
pio device monitor --environment esp32-gateway --filter esp32_exception_decoder
```

### Check Memory Usage
```cpp
ESP_LOGI(TAG, "Free heap: %d bytes", ESP.getFreeHeap());
ESP_LOGI(TAG, "Stack high water mark: %d", uxTaskGetStackHighWaterMark(NULL));
```

---

## ✅ Verification Checklist

Before deploying firmware:
- [ ] Build successful (no compilation errors)
- [ ] Watchdog reset added to retry loops
- [ ] WiFi connection check before uploads
- [ ] BearSSL error detection and recovery
- [ ] Memory usage within limits (RAM < 20%, Flash < 70%)
- [ ] Test with multiple nodes (3+ simultaneous packets)
- [ ] Test WiFi disconnection/reconnection
- [ ] Monitor serial logs for crashes (30+ minutes uptime)

---

**Last Updated:** 2025-10-19  
**Status:** ✅ Fixes applied, pending user testing
