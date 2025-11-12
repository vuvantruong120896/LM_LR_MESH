# Phase 3 Test Guide: TCP/IP Stack

This guide helps you test the TCP/IP functionality of the cellular module.

## 📋 Prerequisites

### Hardware
- ✅ ESP32-S3 board
- ✅ A7682S cellular module
- ✅ Proper connections:
  - TX → GPIO40
  - RX → GPIO41
  - PWR_EN → GPIO39
  - NET_STATUS → GPIO38
- ✅ SIM card with active data plan
- ✅ Good cellular signal (RSSI > 10)
- ✅ Stable power supply (2A minimum)

### Network
- ✅ APN configured correctly for your carrier
- ✅ Internet access enabled on SIM
- ✅ No firewall blocking TCP port 80
- ✅ DNS servers accessible

## 🔧 Configuration

### 1. Edit Test File

Open `test/test_cellular_phase3.cpp` and configure your APN:

```cpp
// Line 29-31: Change to your carrier
const char* APN = "v-internet";  // Your APN here
const char* APN_USER = "";       // Usually empty
const char* APN_PASS = "";       // Usually empty
```

**Common APNs:**

| Carrier (Vietnam) | APN | User | Pass |
|-------------------|-----|------|------|
| Viettel | v-internet | | |
| Mobifone | m-wap | mms | mms |
| Vinaphone | e-connect | | |

| Carrier (International) | APN |
|------------------------|-----|
| AT&T (US) | broadband |
| T-Mobile (US) | fast.t-mobile.com |
| Vodafone (EU) | internet |
| Orange (EU) | internet |

### 2. Optional: Change Test Server

Default test uses `httpbin.org:80` (HTTP testing service).

To change:
```cpp
// Line 35-36
const char* TEST_HOST = "httpbin.org";  // Change hostname
const uint16_t TEST_PORT = 80;          // Change port
```

**Alternative Test Servers:**
- `example.com:80` - Simple text response
- `jsonplaceholder.typicode.com:80` - JSON API
- Your own server (must support HTTP/1.1)

## 🚀 Build & Upload

### Option 1: PlatformIO CLI

```bash
# Build
pio run -e test-cellular-phase3

# Upload
pio run -e test-cellular-phase3 -t upload

# Monitor
pio device monitor

# Or combine all:
pio run -e test-cellular-phase3 -t upload && pio device monitor
```

### Option 2: VS Code

1. Open PlatformIO sidebar
2. Select `test-cellular-phase3` environment
3. Click "Upload and Monitor"

## 📊 Expected Output

### Full Test Sequence

```
[PHASE3_TEST] ==========================================
[PHASE3_TEST] PHASE 3 TEST: TCP/IP Stack
[PHASE3_TEST] ==========================================

[PHASE3_TEST] >>> STEP 1: Initialize Cellular Connection
[CELLULAR_CONN] Initializing cellular connection service...
[CELLULAR_UART] Powering ON module...
[AT_CMD] Testing AT communication...
[AT_CMD] ✅ AT test OK
[CELLULAR_CONN] ✅ SIM card ready
[CELLULAR_CONN] IMEI: 865194061047788
[PHASE3_TEST] ✅ PASSED: Cellular initialized

[PHASE3_TEST] >>> STEP 2: Connect to Cellular Network
[CELLULAR_CONN] Connecting to cellular network...
[CELLULAR_CONN] Waiting for network registration...
[CELLULAR_CONN] ✅ Registered on network: 45204 (Home)
[CELLULAR_CONN] Configuring APN: v-internet
[CELLULAR_CONN] ✅ APN configured
[CELLULAR_CONN] Attaching to GPRS/LTE...
[CELLULAR_CONN] ✅ Attached to GPRS/LTE
[CELLULAR_CONN] Activating PDP context...
[CELLULAR_CONN] Waiting for +NETOPEN URC...
[CELLULAR_CONN] ✅ Network opened successfully
[CELLULAR_CONN] Getting IP address...
[CELLULAR_CONN] ✅ IP address: 10.x.x.x
[CELLULAR_CONN] ✅ Connected to cellular network (12.3s)
[PHASE3_TEST] ✅ PASSED: Cellular connected

[PHASE3_TEST] >>> STEP 3: Create TCP Client
[CELLULAR_TCP] TCP client initialized (max 6 sockets)
[PHASE3_TEST] ✅ PASSED: TCP client created

[PHASE3_TEST] >>> STEP 4: Test DNS Resolution
[CELLULAR_TCP] Resolving httpbin.org...
[CELLULAR_TCP] URC: +CDNSGIP: 1,"httpbin.org","54.x.x.x"
[CELLULAR_TCP] ✅ DNS: httpbin.org -> 54.x.x.x
[PHASE3_TEST] ✅ PASSED: DNS resolved

[PHASE3_TEST] >>> STEP 5: Connect to TCP Server
[CELLULAR_TCP] Connecting to httpbin.org:80...
[CELLULAR_TCP] Waiting for +CIPOPEN URC...
[CELLULAR_TCP] URC: +CIPOPEN: link=0, err=0
[CELLULAR_TCP] ✅ Connected to httpbin.org:80 on socket 0 (3.2s)
[PHASE3_TEST] ✅ PASSED: TCP connected on socket 0

[PHASE3_TEST] >>> STEP 6: Send HTTP GET Request
[CELLULAR_TCP] Sending 74 bytes on socket 0...
[CELLULAR_TCP] ✅ Sent 74 bytes on socket 0
[PHASE3_TEST] ✅ PASSED: Sent 74 bytes

[PHASE3_TEST] 📦 Receiving data (423 bytes available)...
[CELLULAR_TCP] ✅ Received 423 bytes from socket 0
[PHASE3_TEST] ✅ Received 423 bytes (total: 423)

[PHASE3_TEST] Server closed connection
[PHASE3_TEST] ┌─────────────────────────────────────
[PHASE3_TEST] │ HTTP RESPONSE (423 bytes)
[PHASE3_TEST] ├─────────────────────────────────────
[PHASE3_TEST] HTTP/1.1 200 OK
[PHASE3_TEST] Date: Mon, 12 Jan 2025 10:30:00 GMT
[PHASE3_TEST] Content-Type: application/json
[PHASE3_TEST] Content-Length: 245
[PHASE3_TEST] 
[PHASE3_TEST] {
[PHASE3_TEST]   "args": {},
[PHASE3_TEST]   "headers": {
[PHASE3_TEST]     "Host": "httpbin.org",
[PHASE3_TEST]     "User-Agent": "ESP32-A7682S/1.0"
[PHASE3_TEST]   },
[PHASE3_TEST]   "url": "http://httpbin.org/get"
[PHASE3_TEST] }
[PHASE3_TEST] └─────────────────────────────────────
[PHASE3_TEST] ✅ Response time: 2.31s

[PHASE3_TEST] ==========================================
[PHASE3_TEST] ✅ PHASE 3 TEST COMPLETE!
[PHASE3_TEST] ==========================================

[PHASE3_TEST] ┌─────────────────────────────────────
[PHASE3_TEST] │ SOCKET 0 INFO
[PHASE3_TEST] ├─────────────────────────────────────
[PHASE3_TEST] │ State:          CONNECTED
[PHASE3_TEST] │ Remote Host:    httpbin.org
[PHASE3_TEST] │ Remote IP:      54.x.x.x
[PHASE3_TEST] │ Remote Port:    80
[PHASE3_TEST] │ Bytes Sent:     74
[PHASE3_TEST] │ Bytes Received: 423
[PHASE3_TEST] │ Available:      0
[PHASE3_TEST] │ Connected Time: 5s
[PHASE3_TEST] └─────────────────────────────────────

[PHASE3_TEST] ┌─────────────────────────────────────
[PHASE3_TEST] │ TCP/IP STATISTICS
[PHASE3_TEST] ├─────────────────────────────────────
[PHASE3_TEST] │ Total Connections:      1
[PHASE3_TEST] │ Failed Connections:     0
[PHASE3_TEST] │ Total Bytes Sent:       74
[PHASE3_TEST] │ Total Bytes Received:   423
[PHASE3_TEST] │ DNS Queries:            1
[PHASE3_TEST] │ DNS Failed:             0
[PHASE3_TEST] │ Active Connections:     1
[PHASE3_TEST] └─────────────────────────────────────

[PHASE3_TEST] Test completed successfully.
```

## ✅ Success Criteria

The test **PASSES** if you see:

1. ✅ Cellular connection established
2. ✅ IP address assigned (10.x.x.x)
3. ✅ DNS resolution successful (httpbin.org → IP)
4. ✅ TCP connection on socket 0
5. ✅ HTTP request sent (74 bytes)
6. ✅ HTTP response received (typically 300-500 bytes)
7. ✅ Response contains "HTTP/1.1 200 OK"
8. ✅ Statistics show 1 connection, 0 failures
9. ✅ Test completes with "✅ PHASE 3 TEST COMPLETE!"

## ❌ Troubleshooting

### Problem: DNS Resolution Fails

```
[CELLULAR_TCP] DNS resolution failed for httpbin.org
[PHASE3_TEST] ❌ FAILED: DNS resolution
```

**Causes:**
- DNS servers not reachable
- APN doesn't provide DNS
- Network firewall blocking port 53

**Solutions:**
1. Check internet access: ping from phone with same SIM
2. Try alternative test server with IP instead of hostname:
   ```cpp
   const char* TEST_HOST = "93.184.216.34";  // example.com IP
   ```
3. Contact carrier about DNS issues

### Problem: TCP Connection Timeout

```
[CELLULAR_TCP] Connection timeout
[PHASE3_TEST] ❌ FAILED: TCP connection
```

**Causes:**
- Server unreachable
- Port 80 blocked by carrier
- Weak signal (RSSI < 10)
- PDP context inactive

**Solutions:**
1. Check signal: RSSI should be > 10
2. Verify cellular IP assigned: `AT+IPADDR`
3. Try different port:
   ```cpp
   const uint16_t TEST_PORT = 8080;  // Alternative port
   ```
4. Test with carrier support

### Problem: Send Fails

```
[CELLULAR_TCP] Send failed
```

**Causes:**
- Socket disconnected
- Buffer full
- Module error

**Solutions:**
1. Check connection state before send
2. Reduce request size
3. Add delay before send:
   ```cpp
   delay(1000);
   tcp.send(socket, request);
   ```

### Problem: No Data Received

```
[PHASE3_TEST] ⏱️  Response timeout
```

**Causes:**
- Server didn't respond
- Response too large (>2KB)
- URC not processed

**Solutions:**
1. Verify server responds (test with curl):
   ```bash
   curl -v http://httpbin.org/get
   ```
2. Check `update()` called in loop
3. Increase timeout:
   ```cpp
   if (millis() - requestStartTime > 60000) {  // 60s timeout
   ```

### Problem: Module Crashes/Resets

**Causes:**
- Insufficient power
- Memory corruption
- Stack overflow

**Solutions:**
1. Use external 2A power supply (not USB)
2. Check wiring: TX/RX not swapped
3. Reduce log level:
   ```cpp
   esp_log_level_set("*", ESP_LOG_WARN);
   ```

## 🔍 Debug Mode

To enable verbose logging:

```cpp
// In setup()
esp_log_level_set("CELLULAR_TCP", ESP_LOG_DEBUG);
esp_log_level_set("AT_CMD", ESP_LOG_DEBUG);
```

This shows all AT commands and URCs:

```
[AT_CMD] TX: AT+CIPOPEN=0,"TCP","54.x.x.x",80
[AT_CMD] RX: OK
[AT_CMD] URC: +CIPOPEN: 0,0
[CELLULAR_TCP] URC: +CIPOPEN: link=0, err=0
```

## 📊 Performance Expectations

| Metric | Typical Range | Acceptable |
|--------|---------------|------------|
| DNS Resolution | 2-5 seconds | < 10s |
| TCP Connect | 3-8 seconds | < 15s |
| Send 100 bytes | 100-500ms | < 1s |
| Receive response | 1-3 seconds | < 10s |
| Total test time | 15-25 seconds | < 60s |

If times exceed "Acceptable", check signal quality and network congestion.

## 🧪 Advanced Testing

### Test Multiple Sockets

Modify test to connect to multiple servers:

```cpp
int socket1 = tcp.connect("httpbin.org", 80);
int socket2 = tcp.connect("example.com", 80);
int socket3 = tcp.connect("jsonplaceholder.typicode.com", 80);

// Use different sockets...
```

### Test Large Data Transfer

```cpp
// Send larger request
String largeRequest = "POST /post HTTP/1.1\r\n";
largeRequest += "Host: httpbin.org\r\n";
largeRequest += "Content-Type: application/json\r\n";
largeRequest += "Content-Length: 1000\r\n\r\n";
largeRequest += "{\"data\": \"" + String('A', 980) + "\"}";
```

### Test Error Handling

```cpp
// Invalid hostname
tcp.connect("invalid-host-xyz.com", 80);  // Should fail DNS

// Unreachable server
tcp.connect("192.0.2.1", 80);  // Should timeout

// Invalid port
tcp.connect("httpbin.org", 99999);  // Should fail
```

## 📝 Test Checklist

Before declaring Phase 3 complete:

- [ ] Cellular connection stable
- [ ] DNS resolves public hostnames
- [ ] TCP connects to remote server
- [ ] HTTP request sent successfully
- [ ] HTTP response received and parsed
- [ ] Connection closes gracefully
- [ ] Statistics accurate
- [ ] No memory leaks (run for 10+ minutes)
- [ ] URCs processed correctly
- [ ] Error handling works (tested with invalid host)

## 🚀 Next: Phase 4

Once Phase 3 passes, continue to Phase 4 (HTTP/HTTPS Client):

```bash
pio run -e test-cellular-phase4 -t upload
```

Phase 4 will build on TCP client to provide:
- HTTP request builder
- HTTP response parser
- Chunked transfer encoding
- Content-Type handling
- Connection pooling

---

**Need Help?**
- Check `docs/PHASE3_COMPLETION_SUMMARY.md` for API reference
- Review `src/components/cellular/include/cellular_tcp_client.h` for details
- Enable debug logging for detailed traces
