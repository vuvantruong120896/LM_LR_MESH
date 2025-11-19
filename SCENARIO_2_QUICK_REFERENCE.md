# Scenario 2 - Quick Reference Guide

## What is Scenario 2?

When a Node's Gateway becomes unavailable during operation, Scenario 2 automatically buffers sensor data. Once the Gateway is detected in the routing table (via Hello packet), the Node immediately starts an **urgent sync** to transmit all buffered data.

## Key Difference from Scenario 1

| Feature | Scenario 1 | Scenario 2 |
|---------|-----------|-----------|
| Gateway Detection | Periodic check (10 min) | Immediate (routing table callback) |
| Sync Trigger | Timer (30 sec intervals) | Timer OR gateway detection event |
| Transmission Rate | 3 samples/cycle (slow) | 10 samples/cycle in urgent mode (fast) |
| Latency | Up to 30 seconds | <1 second detection |

## Code Locations

### 1. Gateway Availability State Tracking
**File:** `node_app.cpp` line ~32-35
```cpp
static bool wasGatewayAvailable = false;  // Current state
static uint16_t lastKnownGateway = 0;     // Last gateway address
```

### 2. Callback Function (NEW)
**File:** `node_app.cpp` line ~921-988
```cpp
static void onRoutingTableChanged()
{
    // Detect gateway transitions
    // Trigger urgent sync on availability change
}
```

### 3. Enhanced Buffer Sync
**File:** `node_app.cpp` line ~406-476
```cpp
// Adaptive rate based on urgency
uint16_t maxSyncPerCycle = isUrgentSync ? 10 : 3;
uint32_t delayMs = isUrgentSync ? 200 : 500;
```

### 4. Callback Registration
**File:** `node_app.cpp` line ~235-237
```cpp
RoutingTableService::setRoutingTableChangedCallback(onRoutingTableChanged);
```

## Data Flow

```
Scenario 2 Event Sequence:

[Node generates sensor data]
        ↓
[Try to find gateway]
        ↓
    Found? NO → Buffer data to NVS
    Found? YES → Send directly
        ↓
[30 seconds pass]
        ↓
[Routing table updates: Gateway appears in Hello packet]
        ↓
[onRoutingTableChanged() callback triggered]
        ↓
[Detect: wasGatewayAvailable: false → true]
        ↓
[Set isUrgentSync = true]
        ↓
[Next sync cycle: Use urgent rate (10 samples, 200ms delay)]
        ↓
[Send all buffered data ASAP]
        ↓
[Buffer empty → Success message + LED pattern]
```

## Visual Indicators

### LED Patterns

| Condition | LED Pattern | Color |
|-----------|------------|-------|
| Gateway available | Connected pattern | Blue (pulsing) |
| Gateway unavailable | Error pattern | Red (alert) |
| Data buffered | Message pattern | Cyan (flash) |
| Urgent sync complete | Connected pattern | Blue (steady) |

## Log Messages to Watch For

### Gateway Becomes Available (URGENT SYNC)
```
╔════════════════════════════════════════════════════════════╗
║  🎯 SCENARIO 2: GATEWAY DETECTED AFTER OFFLINE!           ║
╚════════════════════════════════════════════════════════════╝

📤 URGENT SYNC: Gateway 0x04XX is NOW AVAILABLE
📦 Triggering immediate sync of 5 buffered samples
⚡ Urgent sync mode activated
```

### Sync In Progress
```
⚡ URGENT SYNC MODE: Transmitting buffered data at maximum rate!
📤 Sending buffered sample #1/5 (counter: 1, age: 125s)
📤 Sending buffered sample #2/5 (counter: 2, age: 115s)
```

### Sync Complete
```
╔════════════════════════════════════════════════════════════╗
║  ✅ ALL BUFFERED DATA SYNCED SUCCESSFULLY!                ║
╚════════════════════════════════════════════════════════════╝

Synced 5 samples to gateway 0x04XX
```

### Gateway Lost
```
⚠️ Gateway 0x04XX is now UNAVAILABLE - will buffer sensor data
```

## Configuration Parameters

### Sync Rate Configuration
**Location:** `node_app.cpp` loop() function

```cpp
// Normal conditions
uint16_t maxSyncPerCycle = 3;      // 3 samples per 30-sec cycle
uint32_t delayMs = 500;             // 500ms between samples

// Moderate load
if (bufferedCount >= 30)
    maxSyncPerCycle = 5;            // 5 samples per cycle

// Urgent sync (gateway just became available)
if (isUrgentSync && bufferedCount >= 5)
    maxSyncPerCycle = 10;           // 10 samples per cycle
    delayMs = 200;                  // 200ms between samples
```

### Buffer Capacity
**Location:** `node_offline_buffer.h`

```cpp
static const uint16_t MAX_BUFFER_SIZE = 50;  // Max 50 samples
// ~2.2 KB data + ~1 KB metadata = ~3.2 KB total
// = ~8-12 minutes of offline operation (typical 10-15s sensor interval)
```

## Testing Checklist

- [ ] **Test 1:** Normal operation (gateway always available)
  - Verify: No buffering, direct send works
  
- [ ] **Test 2:** Gateway becomes unavailable
  - Verify: Buffering starts, LED turns red
  
- [ ] **Test 3:** Gateway becomes available (MAIN SCENARIO 2)
  - Verify: Urgent sync activates, 10 samples/cycle, 200ms delay
  - Verify: Buffer empties successfully
  - Verify: LED turns blue
  
- [ ] **Test 4:** Multiple availability transitions
  - Verify: Each transition handled correctly
  
- [ ] **Test 5:** Buffer overflow (>50 samples)
  - Verify: Circular buffer works (oldest overwritten)
  
- [ ] **Test 6:** Node reboot with buffered data
  - Verify: Buffer persisted to NVS, survives reboot
  - Verify: Sync resumes after reboot

## Troubleshooting

### Problem: "Buffer never syncs"
- **Cause:** Gateway still not in routing table
- **Solution:** Wait for Gateway's Hello packet (~1 min)
- **Check:** Look for routing table print showing Gateway address

### Problem: "URGENT SYNC message not appearing"
- **Cause:** isUrgentSync flag not set correctly
- **Solution:** Verify `wasGatewayAvailable` transitions properly
- **Check:** Look for "SCENARIO 2" message in logs

### Problem: "Buffer partially synced then stops"
- **Cause:** Gateway disappears during sync
- **Solution:** Sync will resume next 30-sec interval
- **Check:** Watch for "UNAVAILABLE" message

### Problem: "LED pattern wrong"
- **Cause:** LED function not called
- **Solution:** Verify `led_pattern_connected()` etc. in code
- **Check:** Look for LED init message at startup

## Performance Expectations

### Urgent Sync Time (50 samples)
```
Time to detect: <1 second (callback triggered immediately)
Time to start sync: ~2-5 seconds (next loop cycle)
Transmission time: ~25 seconds (50 samples × 500ms)
Total: ~30-35 seconds to transmit all buffered data
```

### Memory Usage
```
Stack: ~6 bytes (2 static variables)
Heap: 0 bytes (uses NVS Flash only)
NVS: ~3.2 KB (50 samples × ~44 bytes each)
```

### Network Impact
```
Normal: 1 packet every 10 minutes
During urgent sync: 1 packet every 200ms (10 samples in 2 seconds)
Total bandwidth: Same (all buffered data sent anyway)
Duration of impact: ~30 seconds max (until buffer empty)
```

## Related Files

| File | Purpose |
|------|---------|
| `node_app.cpp` | Main Node app, callback & sync logic |
| `node_app.h` | Node app header |
| `node_offline_buffer.cpp` | Buffer implementation (NVS-based) |
| `node_offline_buffer.h` | Buffer API |
| `RoutingTableService.cpp` | Routing table management |
| `RoutingTableService.h` | Routing table API |

## Summary

**Scenario 2** automatically detects when your Gateway becomes available after being offline and intelligently syncs all buffered data using a fast transmission rate. This ensures no data is lost during temporary network disconnections, while minimizing energy consumption during normal operation.

The system is transparent to the user - it works automatically without requiring any manual intervention or configuration.
