# TL;DR: Routing Table Runtime vs NVS

**Câu hỏi:** Current routing table và routing table từ NVS khi reboot có giống nhau?

---

## 🎯 Câu Trả Lời Ngắn Gọn

### **CÓ và KHÔNG - Tùy timing:**

✅ **Core routing data:** 100% giống nhau
- Node addresses ✅
- Via (next hop) ✅
- Metric (hops) ✅
- Role ✅
- NetworkId ✅

❌ **Network metrics:** 0% giống nhau (expected)
- Timeout ❌ (recalculated)
- SRTT/RTTVAR ❌ (reset về 0)

⚠️ **Timing-dependent:**
- Nodes joined AFTER last save → ❌ Missing
- Nodes removed BEFORE reboot → ✅ Correct

**Overall Similarity: 80-95%**

---

## 📊 Quá Trình Save & Load

### Save (Runtime → NVS):
```cpp
RouteNode (in-memory)          →  RouteEntry (NVS)
  address: 0x3C71              →    address: 0x3C71      ✅
  via: 0x6E41                  →    via: 0x6E41          ✅
  metric: 2                    →    metric: 2            ✅
  role: 0x02                   →    role: 0x02           ✅
  networkId: 0x0001            →    networkId: 0x0001    ✅
  timeout: 1728123456          →    ❌ NOT SAVED
  SRTT: 1200                   →    ❌ NOT SAVED
  RTTVAR: 300                  →    ❌ NOT SAVED
                               →    lastSeen: NOW        ⚠️ (unused)
```

### Load (NVS → Runtime):
```cpp
RouteEntry (NVS)               →  RouteNode (rebuilt)
  address: 0x3C71              →    address: 0x3C71      ✅
  via: 0x6E41                  →    via: 0x6E41          ✅
  metric: 2                    →    metric: 2            ✅
  role: 0x02                   →    role: 0x02           ✅
  networkId: 0x0001            →    networkId: 0x0001    ✅
  lastSeen: 1728123450         →    ❌ NOT RESTORED
                               →    timeout: NEW         🔄
                               →    SRTT: 0              🔄
                               →    RTTVAR: 0            🔄
```

---

## 🔍 Scenarios

### Scenario 1: Stable Network (Ổn Định)
```
Before reboot: [Node1, Node2, Node3]
After reboot:  [Node1, Node2, Node3]
Match: ✅ 95% (core data 100%, metrics reset)
```

### Scenario 2: New Node After Save
```
T1: Save [Node1, Node2]
T2: Node3 joins
T3: Reboot
After: [Node1, Node2] ← Missing Node3!
Match: ⚠️ 70% (Node3 will be rediscovered)
```

### Scenario 3: Node Timeout Before Reboot
```
T1: [Node1, Node2, Node3]
T2: Node3 timeout → removed → save [Node1, Node2]
T3: Reboot
After: [Node1, Node2]
Match: ✅ 100% (correct state)
```

---

## ⚠️ Vấn Đề Quan Trọng

### 1. **lastSeen Field Không Được Dùng**
```cpp
// Save: Set lastSeen = current time
// Load: ❌ Ignore lastSeen completely!
```
→ Waste 4 bytes per entry trong NVS

### 2. **Gap Giữa Node Join và Save**
```
Node joins → Trigger save callback → Save completes
         ↑← 100-500ms gap →↑
If reboot here → Node lost!
```

### 3. **RTT Metrics Reset**
```
Before: SRTT=1200ms, RTTVAR=300ms (learned from network)
After:  SRTT=0, RTTVAR=0 (need to learn again)
```
→ First few packets after reboot có thể timeout

---

## 📝 Recommendations

1. **Remove unused lastSeen field** (save 4 bytes × 50 nodes = 200 bytes)
2. **Add boot logging:** "Restored X nodes, Y new nodes discovered"
3. **Validate entries on load:** Check address, metric, role ranges
4. **Consider RTT persistence** nếu quan trọng cho QoS

---

**Full Analysis:** `docs/ROUTING_TABLE_RUNTIME_VS_NVS.md`
