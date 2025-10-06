# Bug Analysis: Bridge Save Routing Table - Missing Fields

**Ngày:** October 5, 2025  
**Severity:** 🔴 **HIGH** - Data loss bug

---

## 🐛 Bug Tìm Ra

### **Bridge Code (bridge_app.cpp line 527-537):**

```cpp
// ❌ BUG: Missing networkId and isValid fields!
entries[index].address = node->networkNode.address;
entries[index].via = node->via;
entries[index].metric = node->networkNode.metric;
entries[index].role = node->networkNode.role;
// ❌ MISSING: entries[index].networkId = node->networkNode.networkId;
// ❌ MISSING: entries[index].lastSeen = (uint32_t)(esp_timer_get_time() / 1000000);
// ❌ MISSING: entries[index].isValid = true;
```

### **Node Code (node_app.cpp line 600-610):**

```cpp
// ✅ CORRECT: All fields populated
entries[validEntries] = {
    .address = route->networkNode.address,
    .via = route->via,
    .metric = (uint8_t)route->networkNode.metric,
    .role = route->networkNode.role,
    .networkId = route->networkNode.networkId,      // ✅ Set correctly
    .lastSeen = (uint32_t)(esp_timer_get_time() / 1000000), // ✅ Set correctly
    .isValid = true                                  // ✅ Set correctly
};
```

---

## 📊 Impact Analysis

### **RouteEntry Structure:**
```cpp
struct RouteEntry {
    uint16_t address;     // ✅ Bridge sets
    uint16_t via;         // ✅ Bridge sets
    uint8_t metric;       // ✅ Bridge sets
    uint8_t role;         // ✅ Bridge sets
    uint16_t networkId;   // ❌ Bridge KHÔNG set (uninitialized!)
    uint32_t lastSeen;    // ❌ Bridge KHÔNG set (uninitialized!)
    bool isValid;         // ❌ Bridge KHÔNG set (uninitialized!)
};
```

### **Consequences:**

1. **networkId = Random Value** 
   - Uninitialized memory có thể chứa garbage value
   - NVS save networkId sai
   - Khi load lại: network filtering có thể reject packet

2. **lastSeen = Random Value**
   - Timestamp ngẫu nhiên
   - Nếu implement aging: có thể xóa entry ngay lập tức

3. **isValid = Random (true/false)**
   - Nếu false: Entry bị skip khi load!
   - Load code check: `if (entries[i].isValid)`
   - **Worst case: Tất cả entries bị bỏ qua!**

---

## 🔍 Load Code Analysis

### **NVS Load Code (NVSStorageService.cpp line 1140):**

```cpp
if (err == ESP_OK && required_size == sizeof(RouteEntry)) {
    if (entries[loadedCount].isValid) {  // ❌ Check isValid!
        ESP_LOGD(TAG, "Loaded route: addr=0x%04X...");
        loadedCount++;
    } else {
        ESP_LOGD(TAG, "Skipping invalid route entry");  // ← Bridge entries bị skip!
    }
}
```

### **Bridge Load Code (bridge_app.cpp line 578):**

```cpp
for (uint16_t i = 0; i < count; i++) {
    NetworkNode netNode;
    netNode.address = entries[i].address;
    netNode.metric = entries[i].metric;
    netNode.role = entries[i].role;
    // ❌ MISSING: netNode.networkId = entries[i].networkId;
    
    RoutingTableService::processRoute(entries[i].via, &netNode);
}
```

**Bug #2:** Bridge load cũng KHÔNG restore `networkId`!

---

## 🎯 Comparison Table

| Field | Node Save | Bridge Save | Node Load | Bridge Load |
|-------|-----------|-------------|-----------|-------------|
| address | ✅ Set | ✅ Set | ✅ Restore | ✅ Restore |
| via | ✅ Set | ✅ Set | ✅ Restore | ✅ Restore |
| metric | ✅ Set | ✅ Set | ✅ Restore | ✅ Restore |
| role | ✅ Set | ✅ Set | ✅ Restore | ✅ Restore |
| networkId | ✅ Set | ❌ **Missing** | ✅ Restore | ❌ **Missing** |
| lastSeen | ✅ Set | ❌ **Missing** | ❌ Not used | ❌ Not used |
| isValid | ✅ Set | ❌ **Missing** | ✅ Check | ✅ Check |

**Result:** Bridge có **3 critical bugs** trong save/load!

---

## 🔥 Real-World Scenario

### **Scenario: Bridge Save & Reboot**

```
T0: Bridge has routing table:
  Node 0x3C71: via=0x6E41, metric=2, role=0x02, networkId=0x0001
  Node 0xE764: via=0x6E41, metric=2, role=0x02, networkId=0x0001

T1: Save triggered
  entries[0] = {
    address: 0x3C71,
    via: 0x6E41,
    metric: 2,
    role: 0x02,
    networkId: 0xABCD,  ← Random garbage! (uninitialized)
    lastSeen: 123456,   ← Random garbage!
    isValid: false      ← Random! (50% chance false)
  }

T2: Save to NVS → Garbage data saved!

T3: Reboot

T4: Load from NVS
  Read entry: address=0x3C71, isValid=false
  → Skip entry! ❌
  
T5: Result: ZERO nodes restored!
```

**Impact:** Bridge loses entire routing table on reboot! 🔥

---

## 💡 Fix Required

### Fix 1: Bridge Save (bridge_app.cpp line 527-537)

**Before:**
```cpp
entries[index].address = node->networkNode.address;
entries[index].via = node->via;
entries[index].metric = node->networkNode.metric;
entries[index].role = node->networkNode.role;
```

**After:**
```cpp
entries[index] = {
    .address = node->networkNode.address,
    .via = node->via,
    .metric = node->networkNode.metric,
    .role = node->networkNode.role,
    .networkId = node->networkNode.networkId,
    .lastSeen = (uint32_t)(esp_timer_get_time() / 1000000),
    .isValid = true
};
```

### Fix 2: Bridge Load (bridge_app.cpp line 575-581)

**Before:**
```cpp
NetworkNode netNode;
netNode.address = entries[i].address;
netNode.metric = entries[i].metric;
netNode.role = entries[i].role;
```

**After:**
```cpp
NetworkNode netNode;
netNode.address = entries[i].address;
netNode.metric = entries[i].metric;
netNode.role = entries[i].role;
netNode.networkId = entries[i].networkId;  // ✅ Add this!
```

---

## 🚨 Severity Assessment

| Factor | Rating |
|--------|--------|
| **Data Loss** | 🔴 CRITICAL (100% routing table loss) |
| **Frequency** | 🔴 HIGH (every reboot) |
| **Impact** | 🔴 CRITICAL (network down after reboot) |
| **Detection** | 🟡 MEDIUM (logs show "No entries loaded") |
| **Fix Complexity** | 🟢 LOW (simple copy-paste from Node) |

**Overall Priority:** 🔴 **P0 - MUST FIX IMMEDIATELY**

---

## 📝 Testing Plan

### Test 1: Verify Current Bug
```
1. Deploy Bridge with current code
2. Add 3 nodes to routing table
3. Trigger save (wait for callback)
4. Check NVS content: platformio device monitor
   Look for: "Saved X routing entries"
5. Reboot Bridge
6. Check loaded entries
Expected: ZERO entries or garbage data ❌
```

### Test 2: Verify Fix
```
1. Apply fixes to bridge_app.cpp
2. Build and deploy
3. Add 3 nodes to routing table
4. Trigger save
5. Reboot Bridge
6. Check loaded entries
Expected: 3 entries restored correctly ✅
```

---

**Document Version:** 1.0  
**Status:** 🔴 CRITICAL BUG IDENTIFIED  
**Action Required:** IMMEDIATE FIX
