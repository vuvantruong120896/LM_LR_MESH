# CPU Task Architecture - ESP32 Dual-Core Load Distribution

**Date**: October 23, 2025  
**Author**: Gateway Team  
**Status**: ✅ Implemented & Optimized

---

## Overview

ESP32 has **2 CPU cores** (CPU0 and CPU1) that can run tasks independently. This document describes how we distribute workload across cores to optimize performance and prevent blocking operations.

### Design Philosophy

1. **CPU0**: Protocol Stack & Time-Critical Operations
   - WiFi/TCP/IP stack (ESP-IDF default)
   - LoRa mesh protocol processing
   - Main loop coordination (non-blocking)
   - Gateway packet receive task

2. **CPU1**: Application Layer & Heavy I/O
   - Firebase operations (TLS, JSON, network I/O)
   - Command polling with circuit breaker
   - Queue-based data upload
   - Netkey distribution (blocking LoRa broadcasts)

---

## CPU Core Assignment Map

### CPU0 Tasks (Protocol & Coordination)

| Task Name | Stack | Priority | Rationale |
|-----------|-------|----------|-----------|
| **Main Loop** | Default | Default | Coordination, non-blocking checks only |
| **Gateway Receive Task** | 16KB | 2 | Process incoming LoRa packets, enqueue to Firebase |
| **WiFi/TCP Stack** | ESP-IDF | ESP-IDF | System default (always CPU0) |
| **LoRa Mesh Tasks** (6 tasks) | 4-12KB | 2-6 | Receiving, sending, hello, routing, queue manager |

**Total CPU0 Load**: Protocol processing, packet routing, mesh management

---

### CPU1 Tasks (Application & I/O)

| Task Name | Stack | Priority | Rationale |
|-----------|-------|----------|-----------|
| **Firebase Queue Worker** | 16KB | 2 | Non-blocking uploads (TLS/JSON heavy) |
| **Command Poller** | 8KB | 1 | Poll Firebase commands with circuit breaker |
| **Netkey Distribution Worker** | 8KB | 1 | Offload blocking broadcasts (5-25s operation) |

**Total CPU1 Load**: Firebase I/O, TLS handshakes, heavy blocking operations

---

## Task Creation Examples

### CPU0 - Protocol Task (Pinned)
```cpp
xTaskCreatePinnedToCore(
    processGatewayPackets,
    "Gateway Receive Task",
    16384,  // 16KB stack
    (void*) 1,
    2,      // Priority 2 (higher than Firebase)
    &taskHandle,
    0);     // ⚡ Core 0: Mesh/protocol processing
```

### CPU1 - Application Task (Pinned)
```cpp
xTaskCreatePinnedToCore(
    pollingTask,
    "CmdPoller",
    8192,   // 8KB stack
    this,
    1,      // Priority 1
    &handle,
    1);     // ⚡ Core 1: Application/Firebase
```

### No Core Pinning (Scheduler Decides)
```cpp
xTaskCreate(
    someTask,
    "UnpinnedTask",
    4096,
    NULL,
    1,
    &handle);  // ⚠️ Scheduler chooses core (usually CPU0)
```

---

## Command Handler Optimization

### Fast Handlers (Run on CPU0 Main Loop)
These handlers perform **local, non-blocking operations** and can run directly in main loop:

#### ✅ `handleStartProvisioning`
- JSON parsing (~1ms)
- Radio mode switch (local)
- Single LoRa broadcast (HELLO mode change)
- Update local state
- **Execution time**: <10ms

#### ✅ `handleStopProvisioning`
- Radio mode switch
- Single LoRa broadcast
- String operations
- Update local state
- **Execution time**: <10ms

---

### Heavy Handlers (Offloaded to CPU1)

#### ⚡ `handleAssignNetkey` → **Netkey Distribution Worker**
**Problem**: Broadcasting netkey to ALL nodes is **blocking and slow**:
- 50 nodes × 200ms per node = **10 seconds blocking**
- 100 nodes × 300ms per node = **30 seconds blocking**

**Solution**: Offload to dedicated CPU1 worker task

```cpp
// Main loop (CPU0): Quick setup only
void GatewayApp::handleAssignNetkey(const FirebaseCommandPoller::Command& cmd) {
    // Load config from NVS (~10ms)
    NetworkConfig cfg;
    NVSStorageService::loadNetworkConfig(cfg);
    
    // Update local netkey (fast)
    NetkeyDistributionService::updateLocalNetworkKey(...);
    
    // ⚡ Create CPU1 worker for heavy distribution
    NetkeyDistributionTask* taskParams = new NetkeyDistributionTask{cmd, cfg};
    xTaskCreatePinnedToCore(
        netkeyDistributionWorker,
        "NetkeyWorker",
        8192, 1, &handle, 1  // CPU1
    );
    
    // Main loop continues immediately (non-blocking!)
}

// Worker task (CPU1): Heavy blocking work
void GatewayApp::netkeyDistributionWorker(void* parameter) {
    // HEAVY: Broadcast netkey to ALL nodes
    // This takes 5-25 seconds depending on node count
    NetkeyDistributionService::distributeNetkeyToAllNodes(...);
    
    // Update command status
    commandPoller->moveToCompleted(cmd, "success", message);
    
    // Self-destruct when done
    vTaskDelete(NULL);
}
```

**Benefits**:
- Main loop on CPU0 remains responsive
- Protocol processing unaffected
- Progress visible in Firebase (command status updates)
- Worker auto-cleans up after completion

---

## Performance Benefits

### Before Optimization (Single-Core Behavior)
```
Main Loop (CPU0):
  ├─ WiFi/TCP stack
  ├─ LoRa mesh processing
  ├─ Gateway receive task
  ├─ Firebase uploads (BLOCKING 30s TLS handshake) ❌
  └─ Command polling (BLOCKING 30s timeout) ❌
  └─ Netkey distribution (BLOCKING 5-25s) ❌

Result: Main loop hangs → WDT resets → data loss
```

### After Optimization (Dual-Core Distribution)
```
CPU0 (Protocol):                    CPU1 (Application):
├─ WiFi/TCP stack                   ├─ Firebase Queue Worker
├─ LoRa mesh processing             │   └─ Non-blocking uploads
├─ Gateway receive task             ├─ Command Poller Task
│   └─ Enqueue to Firebase          │   └─ Circuit breaker + pre-checks
└─ Main loop (coordination)         └─ Netkey Worker (one-shot)
                                         └─ Heavy blocking ops isolated

Result: Responsive, no WDT resets, optimal performance ✅
```

---

## Memory Management

### Stack Allocation Strategy

| Task Type | Stack Size | Reason |
|-----------|-----------|--------|
| Protocol tasks | 4-16KB | Packet buffers, crypto operations |
| Firebase tasks | 8-16KB | TLS state, JSON buffers |
| Worker tasks | 8KB | Temporary operations, auto-cleanup |

### Heap Fragmentation Prevention
- Pre-allocate task stacks at boot
- Avoid dynamic allocation in main loop
- Worker tasks self-destruct after completion
- Firebase queue uses fixed-size ring buffer

---

## Monitoring & Debugging

### Check Task Core Assignment at Runtime
```cpp
ESP_LOGI(TAG, "Task running on CPU%d", xPortGetCoreID());
```

### Expected Output
```
[GATEWAY-TASK] Gateway packet processing task started on CPU0
[Firebase Worker] Started on CPU1
[CmdPoller] Polling task started on CPU1
[NETKEY-WORKER] Started on CPU1
```

### Stack High Water Mark Monitoring
```cpp
UBaseType_t stackFree = uxTaskGetStackHighWaterMark(NULL);
ESP_LOGI(TAG, "Stack high water mark: %d bytes free", stackFree);
```

**Warning threshold**: < 1024 bytes free

---

## Best Practices

### ✅ DO
1. Pin protocol tasks to CPU0 (WiFi, LoRa mesh)
2. Pin Firebase/TLS tasks to CPU1 (avoid blocking protocols)
3. Offload heavy blocking operations to CPU1 workers
4. Use circuit breakers for network operations
5. Monitor stack usage and heap fragmentation

### ❌ DON'T
1. Block main loop with network I/O
2. Mix protocol and application tasks on same core
3. Create tasks without explicit core pinning (scheduler may choose wrong core)
4. Ignore WDT warnings (indicates blocking on CPU0)
5. Forget to delete one-shot worker tasks

---

## Future Improvements

### Potential Optimizations
1. **Dynamic Core Balancing**: Monitor CPU load and migrate low-priority tasks
2. **Task Affinity Hinting**: Use FreeRTOS affinity masks for multi-core scheduling
3. **Interrupt Core Assignment**: Pin interrupt handlers to specific cores
4. **Memory Pool per Core**: Reduce cache contention between cores

### Known Limitations
- ESP-IDF WiFi/TCP stack always runs on CPU0 (cannot change)
- Core migration has overhead (~10-100µs context switch)
- Shared resources (routing table, NVS) need mutex protection

---

## References

- [ESP-IDF FreeRTOS SMP Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)
- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- Firebase Queue Architecture: `docs/FIREBASE_QUEUE_ARCHITECTURE.md`
- Command Poller Task Upgrade: `docs/COMMAND_POLLER_TASK_UPGRADE.md`

---

## Change Log

| Date | Change | Rationale |
|------|--------|-----------|
| Oct 21, 2025 | Firebase Queue on CPU1 | Isolate TLS/network from CPU0 |
| Oct 21, 2025 | Command Poller on CPU1 | Non-blocking polling with circuit breaker |
| Oct 23, 2025 | Gateway Receive Task pinned to CPU0 | Explicit protocol processing separation |
| Oct 23, 2025 | Netkey Worker offload to CPU1 | Prevent 5-25s blocking on CPU0 |

---

**Status**: ✅ All critical tasks properly assigned  
**Performance**: ✅ No WDT resets, responsive main loop  
**Tested**: ✅ 50+ node networks, 24+ hour uptime
