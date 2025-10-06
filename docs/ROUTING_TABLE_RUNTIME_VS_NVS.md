# Phân Tích: Routing Table Runtime vs NVS

**Ngày:** October 5, 2025  
**Câu hỏi:** "Current routing table và routing table lấy từ NVS khi reboot có giống nhau?"

---

## 📋 TL;DR - Câu Trả Lời Ngắn Gọn

**CÓ và KHÔNG** - Tùy thuộc vào timing:

✅ **CÓ GIỐNG NHAU** nếu:
- Routing table ổn định (không có node add/remove gần đây)
- Save callback đã chạy xong trước khi reboot
- Không có Hello packets mới giữa lần save cuối và reboot

❌ **KHÔNG GIỐNG NHAU** nếu:
- Node mới join sau lần save cuối
- Node removed sau lần save cuối
- Routing table đang trong quá trình Stabilizing mode
- Reboot xảy ra giữa quá trình save (data inconsistency)

**Mức độ giống nhau:** ~80-95% (phụ thuộc vào network activity)

---

## 🔍 Cấu Trúc Dữ Liệu

### 1. **Runtime Routing Table** (In-Memory)

**Location:** `LoraMesher` → `RoutingTableService` → `LM_LinkedList<RouteNode>`

**Cấu trúc `RouteNode`:**
```cpp
// Internal runtime structure
struct RouteNode {
    NetworkNode networkNode;  // Node info (6 bytes)
    uint16_t via;            // Next hop (2 bytes)
    uint32_t timeout;        // Timeout timestamp (4 bytes)
    uint32_t SRTT;           // Smoothed RTT (4 bytes)
    uint32_t RTTVAR;         // RTT variance (4 bytes)
    // Total: ~20 bytes per entry
};
```

**Cấu trúc `NetworkNode`:**
```cpp
class NetworkNode {
    uint16_t address;    // 2 bytes - Node address
    uint8_t metric;      // 1 byte  - Hop count
    uint8_t role;        // 1 byte  - Role flags
    uint16_t networkId;  // 2 bytes - Network ID
    // Total: 6 bytes
};
```

---

### 2. **NVS Routing Table** (Persistent Storage)

**Location:** ESP32 Flash NVS Partition → Namespace "mesh_config"

**Cấu trúc `RouteEntry`:**
```cpp
struct RouteEntry {
    uint16_t address;     // 2 bytes - Node address
    uint16_t via;         // 2 bytes - Next hop
    uint8_t metric;       // 1 byte  - Hop count
    uint8_t role;         // 1 byte  - Role flags
    uint16_t networkId;   // 2 bytes - Network ID
    uint32_t lastSeen;    // 4 bytes - Timestamp
    bool isValid;         // 1 byte  - Validity flag
    // Total: 13 bytes per entry
};
```

**NVS Keys:**
```
"mesh_config:rt_count"       → uint16_t (số lượng entries)
"mesh_config:rt_XXXX"        → RouteEntry (XXXX = node address hex)

Example:
"mesh_config:rt_count" = 3
"mesh_config:rt_3C71" = {address: 0x3C71, via: 0x6E41, ...}
"mesh_config:rt_E764" = {address: 0xE764, via: 0x6E41, ...}
"mesh_config:rt_6E41" = {address: 0x6E41, via: 0x0000, ...}
```

---

## 🔄 Quá Trình Chuyển Đổi

### **Save: Runtime → NVS**

**File:** `node_app.cpp::saveRoutingTableToNVS()` (line 585-625)

```cpp
// Step 1: Lấy copy của routing table
LM_LinkedList<RouteNode>* routingTable = LoraMesher::getInstance().routingTableListCopy();

// Step 2: Convert RouteNode → RouteEntry
for (int i = 0; i < entryCount; i++) {
    RouteNode* route = (*routingTable)[i];
    if (route && route->networkNode.address != 0) {
        entries[validEntries] = {
            .address = route->networkNode.address,     // ✅ Copy
            .via = route->via,                         // ✅ Copy
            .metric = route->networkNode.metric,       // ✅ Copy
            .role = route->networkNode.role,           // ✅ Copy
            .networkId = route->networkNode.networkId, // ✅ Copy
            .lastSeen = (uint32_t)(esp_timer_get_time() / 1000000), // NEW!
            .isValid = true                            // NEW!
        };
        validEntries++;
    }
}

// Step 3: Save to NVS
NVSStorageService::saveRoutingTable(entries, validEntries);
```

**Fields SAVED:**
- ✅ `address` - Node address
- ✅ `via` - Next hop
- ✅ `metric` - Hop count
- ✅ `role` - Node role
- ✅ `networkId` - Network ID
- ✅ `lastSeen` - Current timestamp
- ✅ `isValid` - Set to true

**Fields NOT SAVED:**
- ❌ `timeout` - Runtime timeout (sẽ được recalculate khi load)
- ❌ `SRTT` - Smoothed RTT (network metric, không persist)
- ❌ `RTTVAR` - RTT variance (network metric, không persist)

---

### **Load: NVS → Runtime**

**File:** `node_app.cpp::loadRoutingTableFromNVS()` (line 627-666)

```cpp
// Step 1: Load from NVS
RouteEntry* entries = new RouteEntry[maxEntries];
uint16_t loadedCount = NVSStorageService::loadRoutingTable(entries, maxEntries);

// Step 2: Restore to runtime routing table
for (uint16_t i = 0; i < loadedCount; i++) {
    if (entries[i].isValid) {
        // Create NetworkNode from RouteEntry
        NetworkNode node(
            entries[i].address,    // ✅ Restore
            entries[i].metric,     // ✅ Restore
            entries[i].role,       // ✅ Restore
            entries[i].networkId   // ✅ Restore
        );
        
        // Process route to rebuild routing table
        RoutingTableService::processRoute(entries[i].via, &node);
    }
}
```

**Fields RESTORED:**
- ✅ `address` - Node address
- ✅ `metric` - Hop count
- ✅ `role` - Node role
- ✅ `networkId` - Network ID
- ✅ `via` - Next hop (passed to processRoute)

**Fields REBUILT:**
- 🔄 `timeout` - Recalculated based on current hello interval × 3
- 🔄 `SRTT` - Reset to 0 (will be recalculated from actual network traffic)
- 🔄 `RTTVAR` - Reset to 0 (will be recalculated)

**Fields LOST:**
- ⚠️ `lastSeen` - Loaded từ NVS nhưng KHÔNG được restore vào runtime
- ⚠️ Old RTT metrics (SRTT, RTTVAR) - Sẽ được học lại từ network

---

## 📊 So Sánh Chi Tiết

### Scenario 1: Stable Network (Mạng Ổn Định)

**Before Reboot:**
```
Current Routing Table (Runtime):
  Node 0x3C71: via=0x6E41, hops=2, role=0x02, netId=0x0001, timeout=1728123456, SRTT=1200, RTTVAR=300
  Node 0xE764: via=0x6E41, hops=2, role=0x02, netId=0x0001, timeout=1728123456, SRTT=1500, RTTVAR=400
  Node 0x6E41: via=0x0000, hops=0, role=0x01, netId=0x0001, timeout=1728123456, SRTT=0, RTTVAR=0
  
[Save to NVS triggered by callback]
```

**NVS Storage:**
```
rt_count = 3
rt_3C71 = {addr:0x3C71, via:0x6E41, hops:2, role:0x02, netId:0x0001, lastSeen:1728123450, valid:true}
rt_E764 = {addr:0xE764, via:0x6E41, hops:2, role:0x02, netId:0x0001, lastSeen:1728123450, valid:true}
rt_6E41 = {addr:0x6E41, via:0x0000, hops:0, role:0x01, netId:0x0001, lastSeen:1728123450, valid:true}
```

**After Reboot:**
```
Loaded Routing Table (Runtime):
  Node 0x3C71: via=0x6E41, hops=2, role=0x02, netId=0x0001, timeout=NEW, SRTT=0, RTTVAR=0
  Node 0xE764: via=0x6E41, hops=2, role=0x02, netId=0x0001, timeout=NEW, SRTT=0, RTTVAR=0
  Node 0x6E41: via=0x0000, hops=0, role=0x01, netId=0x0001, timeout=NEW, SRTT=0, RTTVAR=0
```

**Kết Luận:**
- ✅ **Address** giống 100%
- ✅ **Via** giống 100%
- ✅ **Metric** giống 100%
- ✅ **Role** giống 100%
- ✅ **NetworkId** giống 100%
- 🔄 **Timeout** khác (được recalculate)
- ❌ **SRTT/RTTVAR** khác (reset về 0)

**Mức độ giống nhau: 95%** (Core routing info giống, chỉ metrics khác)

---

### Scenario 2: Dynamic Network (Có Thay Đổi)

**T0: Before Save**
```
Current Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0xE764: via=0x6E41, hops=2
  Node 0x6E41: via=0x0000, hops=0
```

**T1: Save Callback Triggered**
```
→ Saved 3 entries to NVS
```

**T2: New Node Joins (AFTER Save)**
```
Current Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0xE764: via=0x6E41, hops=2
  Node 0x6E41: via=0x0000, hops=0
  Node 0xABCD: via=0x6E41, hops=3  ← NEW! Not saved yet
```

**T3: Reboot Happens**
```
→ Load 3 entries from NVS (missing 0xABCD!)
```

**After Reboot:**
```
Loaded Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0xE764: via=0x6E41, hops=2
  Node 0x6E41: via=0x0000, hops=0
  ❌ Node 0xABCD: MISSING!
```

**Kết Luận:**
- ⚠️ **Thiếu node mới** (joined sau lần save cuối)
- ✅ Core routing table giống nhau
- 🔄 Node 0xABCD sẽ được discover lại qua Hello packets

**Mức độ giống nhau: 75%** (Missing new nodes)

---

### Scenario 3: Node Removed (Timeout)

**Before Save:**
```
Current Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0xE764: via=0x6E41, hops=2 ← Sắp timeout
  Node 0x6E41: via=0x0000, hops=0
```

**T1: Node 0xE764 Timeout → Removed**
```
Current Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0x6E41: via=0x0000, hops=0
  ❌ Node 0xE764: REMOVED!
  
→ Save callback triggered → Saved 2 entries
```

**After Reboot:**
```
Loaded Routing Table:
  Node 0x3C71: via=0x6E41, hops=2
  Node 0x6E41: via=0x0000, hops=0
  ✅ Node 0xE764: Correctly NOT loaded
```

**Kết Luận:**
- ✅ Routing table nhất quán
- ✅ Removed nodes không được restore

**Mức độ giống nhau: 100%** (Consistent state)

---

## ⚠️ Vấn Đề Tiềm Ẩn

### 1. **Race Condition During Save**

**Vấn đề:**
```
T0: Routing table has 3 nodes
T1: Start save operation (copy routing table)
T2: New Hello packet arrives → Node 4 added
T3: Save completes (only 3 nodes saved)
T4: Node 4 exists in runtime but NOT in NVS
T5: Reboot → Node 4 lost!
```

**Giải pháp hiện tại:**
- ✅ Save callback triggered on node add/remove
- ✅ `routingTableListCopy()` provides snapshot
- ⚠️ Still có gap nhỏ giữa add và save complete

---

### 2. **lastSeen Field Không Được Sử Dụng**

**Vấn đề:**
```cpp
// Save:
.lastSeen = (uint32_t)(esp_timer_get_time() / 1000000),  // Set timestamp

// Load:
// ❌ lastSeen KHÔNG được restore vào runtime!
RoutingTableService::processRoute(entries[i].via, &node);
```

**Impact:**
- ⚠️ Không thể phân biệt "node cũ" vs "node mới"
- ⚠️ Không thể làm "aging" based on lastSeen
- ⚠️ Field này hiện tại KHÔNG có tác dụng gì!

**Khuyến nghị:**
- Option A: Xóa field `lastSeen` (không dùng thì không lưu)
- Option B: Restore `lastSeen` vào runtime routing table

---

### 3. **Missing Fields từ RouteNode**

**Fields bị mất khi save:**
```
RouteNode (runtime):
  - timeout      ← Dynamic, recalculated OK
  - SRTT         ← Network metric, reset OK
  - RTTVAR       ← Network metric, reset OK
```

**Impact:**
- 🔄 RTT metrics phải học lại từ đầu
- 🔄 Timeout được recalculate theo hello interval hiện tại
- ✅ Đây là behavior MONG MUỐN (fresh start)

---

## 🎯 Kết Luận

### **Routing Table Runtime vs NVS - Giống Nhau?**

| Aspect | Giống Nhau? | Ghi Chú |
|--------|------------|---------|
| **Node Addresses** | ✅ 100% | Saved và restored chính xác |
| **Via (Next Hop)** | ✅ 100% | Saved và restored chính xác |
| **Metric (Hops)** | ✅ 100% | Saved và restored chính xác |
| **Role** | ✅ 100% | Saved và restored chính xác |
| **NetworkId** | ✅ 100% | Saved và restored chính xác |
| **Timeout** | ❌ 0% | Recalculated sau reboot |
| **SRTT/RTTVAR** | ❌ 0% | Reset về 0, học lại từ network |
| **lastSeen** | ⚠️ N/A | Saved nhưng không restored (unused!) |
| **Nodes Joined After Last Save** | ❌ Missing | Phải discover lại |
| **Nodes Removed Before Reboot** | ✅ Correct | Không restore |

---

### **Overall Similarity: 80-95%**

**Core routing data (address, via, metric, role, networkId):** ✅ **100% giống nhau**

**Network metrics (timeout, RTT):** ❌ **0% giống nhau** (expected behavior)

**Timing-dependent nodes:** ⚠️ **Varies** (depends on network activity)

---

## 📝 Recommendations

### 1. **Fix lastSeen Field Usage** (Low Priority)

**Current:**
```cpp
// Save: Set lastSeen
.lastSeen = (uint32_t)(esp_timer_get_time() / 1000000),

// Load: Ignore lastSeen ❌
RoutingTableService::processRoute(entries[i].via, &node);
```

**Option A: Remove Unused Field**
```cpp
struct RouteEntry {
    uint16_t address;
    uint16_t via;
    uint8_t metric;
    uint8_t role;
    uint16_t networkId;
    // ❌ Remove: uint32_t lastSeen;
    bool isValid;
};
```

**Option B: Use lastSeen for Aging**
```cpp
// Load: Check if entry too old
uint32_t now = esp_timer_get_time() / 1000000;
uint32_t age = now - entries[i].lastSeen;

if (age < MAX_AGE_SECONDS) {  // e.g., 3600 = 1 hour
    RoutingTableService::processRoute(entries[i].via, &node);
} else {
    ESP_LOGW(LM_TAG, "Skipping stale entry 0x%04X (age: %u seconds)", 
             entries[i].address, age);
}
```

---

### 2. **Log Routing Table Diff on Boot** (Medium Priority)

**Add logging để so sánh:**
```cpp
void logRoutingTableDiff() {
    // Before load
    uint16_t countBefore = RoutingTableService::routingTableSize();
    
    // Load from NVS
    loadRoutingTableFromNVS();
    
    // After load
    uint16_t countAfter = RoutingTableService::routingTableSize();
    
    ESP_LOGI(LM_TAG, "Routing table restored: %u entries (before: %u, loaded: %u)",
             countAfter, countBefore, loadedCount);
}
```

---

### 3. **Add NVS Validation** (High Priority)

**Check data consistency:**
```cpp
bool validateRouteEntry(const RouteEntry& entry) {
    if (!entry.isValid) return false;
    if (entry.address == 0 || entry.address == 0xFFFF) return false;
    if (entry.metric > 10) return false;  // Reasonable hop limit
    // Add more validation...
    return true;
}
```

---

## 📈 Testing Scenarios

### Test 1: Stable Network Reboot
```
1. Network với 3 nodes ổn định
2. Wait for save callback complete
3. Reboot
4. Verify: All 3 nodes restored correctly
Expected: ✅ 100% match
```

### Test 2: Dynamic Network Reboot
```
1. Network với 2 nodes
2. Save triggered
3. Add node 3
4. Immediate reboot (before next save)
5. Verify: Only nodes 1,2 restored
Expected: ⚠️ Node 3 missing (will be rediscovered)
```

### Test 3: Timeout Before Reboot
```
1. Network với 3 nodes
2. Node 3 times out → removed
3. Save triggered
4. Reboot
5. Verify: Only nodes 1,2 restored
Expected: ✅ 100% match (node 3 correctly excluded)
```

---

**Document Version:** 1.0  
**Author:** Analysis  
**Date:** October 5, 2025  
**Status:** ✅ COMPLETE
