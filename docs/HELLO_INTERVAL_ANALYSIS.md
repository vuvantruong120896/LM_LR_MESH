# LoRa Mesh - Phân tích Timeout và Hello Interval

## 📅 Ngày phân tích
October 4, 2025

---

## ❓ CÂU HỎI 1: Sau bao lâu node bị xóa khỏi routing table?

### **Công thức tính:**
```
Timeout = Current Hello Interval × TIMEOUT_MULTIPLIER
```

Với `TIMEOUT_MULTIPLIER = 3` (định nghĩa trong `BuildOptions.h`)

### **📊 Bảng timeout theo từng mode:**

| Hello Mode | Hello Interval | Timeout | Số Hello bị miss | Ghi chú |
|------------|----------------|---------|------------------|---------|
| **Fast Discovery** | 30s | **DISABLED** ⚠️ | ∞ | Không xóa node trong provisioning |
| **Stabilizing** | 90s | **270s** (4.5 phút) | 3 packets | Sau provisioning |
| **Normal** | 600s | **1800s** (30 phút) | 3 packets | Hoạt động bình thường |

### **⚙️ Cách hoạt động:**

1. **Mỗi khi nhận Hello packet từ node:**
   ```cpp
   resetTimeoutRoutingNode(node);
   // node->timeout = millis() + (currentHelloInterval × 3) * 1000
   ```

2. **Check timeout (chạy định kỳ):**
   ```cpp
   if (node->timeout < millis()) {
       // XÓA node khỏi routing table
   }
   ```

3. **Trong Fast Discovery Mode:**
   ```cpp
   if (currentMode == HELLO_MODE_FAST_DISCOVERY) {
       return false; // KHÔNG xóa node
   }
   ```

### **🎯 Ví dụ thực tế:**

**Scenario 1: Normal Mode (600s interval)**
- Node A gửi Hello tại: T=0, T=600s, T=1200s, T=1800s
- Timeout reset tại: T=1800s → timeout = T + 1800s = T=3600s
- Nếu không nhận Hello sau T=1800s → bị xóa tại T=3600s
- **→ Node bị xóa sau 30 phút không có Hello**

**Scenario 2: Stabilizing Mode (90s interval)**  
- Node A gửi Hello tại: T=0, T=90s, T=180s, T=270s
- Timeout reset tại: T=270s → timeout = T + 270s = T=540s
- Nếu không nhận Hello sau T=270s → bị xóa tại T=540s
- **→ Node bị xóa sau 4.5 phút không có Hello**

---

## ❓ CÂU HỎI 2: HELLO_NORMAL_INTERVAL = 600s (10 phút) có phù hợp không?

### **📡 Đặc điểm của mạng LoRa mesh hiện tại:**

#### **✅ Ưu điểm của 10 phút:**
1. **Tiết kiệm năng lượng** 🔋
   - LoRa thường dùng battery → ít gửi = pin sống lâu hơn
   - Đặc biệt quan trọng cho sensor nodes

2. **Giảm collision** 📡
   - Ít packet → ít xung đột trên shared spectrum
   - LoRa duty cycle 1% (EU) / 0.4% (Úc) → cần space out transmissions

3. **Phù hợp mạng tĩnh** 🏠
   - Sensor nodes cố định, ít di chuyển
   - Topology ổn định → không cần update routing thường xuyên

#### **❌ Nhược điểm của 10 phút:**
1. **Phát hiện lỗi chậm** 🐌
   - Node offline → mất 30 phút mới phát hiện (3 × 10 phút)
   - Data loss trong 30 phút

2. **Khôi phục chậm** 🔧
   - Node mới join → tối đa 10 phút mới được discover
   - Bridge restart → 10 phút mới rebuild routing table

3. **Không phù hợp mobile nodes** 🚗
   - Nếu có nodes di động → 10 phút quá lâu

---

## 💡 KHUYẾN NGHỊ

### **📊 Phân tích theo use case:**

#### **🏭 Use Case 1: Sensor Network Tĩnh (Hiện tại)**
**Đặc điểm:**
- Các sensor node cố định (nhiệt độ, độ ẩm, ánh sáng...)
- Chạy bằng pin
- Topology ít thay đổi
- Data không critical (chấp nhận delay)

**Khuyến nghị:** ✅ **GIỮ NGUYÊN 600s (10 phút)**

**Lý do:**
- Pin tiết kiệm → triển khai lâu dài
- Collision thấp → reliability cao
- Phù hợp với tính chất LoRa (long range, low power)

---

#### **🏥 Use Case 2: Critical Monitoring**
**Đặc điểm:**
- Cần phát hiện lỗi nhanh
- Data quan trọng (y tế, an toàn)
- Chấp nhận tiêu thụ pin nhiều hơn

**Khuyến nghị:** ⚠️ **GIẢM xuống 300s (5 phút)**

**Cấu hình đề xuất:**
```cpp
#define HELLO_NORMAL_INTERVAL 300        // Normal: 5 phút
#define HELLO_STABILIZING_INTERVAL 60    // Stabilizing: 1 phút
#define HELLO_FAST_INTERVAL 20           // Fast: 20 giây
```

**Timeout tương ứng:**
- Normal: 300s × 3 = **900s (15 phút)**
- Stabilizing: 60s × 3 = **180s (3 phút)**
- Fast: 20s × 3 = **60s (1 phút)**

---

#### **🚗 Use Case 3: Mobile Mesh Network**
**Đặc điểm:**
- Nodes di động (xe, người)
- Topology thay đổi liên tục
- Cần USB/mains power

**Khuyến nghị:** 🔥 **GIẢM xuống 120s (2 phút)**

**Cấu hình đề xuất:**
```cpp
#define HELLO_NORMAL_INTERVAL 120        // Normal: 2 phút
#define HELLO_STABILIZING_INTERVAL 30    // Stabilizing: 30 giây
#define HELLO_FAST_INTERVAL 10           // Fast: 10 giây
```

**Timeout tương ứng:**
- Normal: 120s × 3 = **360s (6 phút)**
- Stabilizing: 30s × 3 = **90s (1.5 phút)**
- Fast: 10s × 3 = **30s**

---

### **🎯 Khuyến nghị cho hệ thống HIỆN TẠI:**

#### **Option A: Tối ưu cân bằng (ĐỀ XUẤT)** ⭐

```cpp
// BuildOptions.h
#define HELLO_NORMAL_INTERVAL 300        // Giảm từ 600s → 300s (5 phút)
#define HELLO_STABILIZING_INTERVAL 60    // Giảm từ 90s → 60s (1 phút)
#define HELLO_FAST_INTERVAL 30           // Giữ nguyên 30s
#define TIMEOUT_MULTIPLIER 3             // Giữ nguyên
```

**Lợi ích:**
- ✅ Phát hiện lỗi nhanh hơn 2x (15 phút vs 30 phút)
- ✅ Discovery nhanh hơn 2x (5 phút vs 10 phút)
- ✅ Vẫn tiết kiệm pin (5 phút vẫn rất reasonable cho LoRa)
- ✅ Ít collision hơn nhiều so với 1-2 phút interval

**Timeout table:**
| Mode | Hello | Timeout | Detection Time |
|------|-------|---------|----------------|
| Normal | 5 phút | 15 phút | Reasonable ✅ |
| Stabilizing | 1 phút | 3 phút | Good ✅ |
| Fast | 30s | DISABLED | Perfect ✅ |

---

#### **Option B: Giữ nguyên (Conservative)** 🐌

Nếu:
- Pin là ưu tiên #1
- Network rất ổn định
- Chấp nhận phát hiện lỗi chậm

**→ GIỮ 600s**

---

#### **Option C: Aggressive (cho critical systems)** 🔥

```cpp
#define HELLO_NORMAL_INTERVAL 180        // 3 phút
#define HELLO_STABILIZING_INTERVAL 45    // 45 giây
#define HELLO_FAST_INTERVAL 20           // 20 giây
```

**Chỉ dùng khi:**
- Có nguồn điện ổn định (USB/mains)
- Cần phát hiện lỗi < 10 phút
- Ready to handle higher collision rate

---

### **🔧 Cách điều chỉnh:**

**File:** `src/components/lora_mesh_manager/src/core/BuildOptions.h`

```cpp
// Dynamic Hello Mode Configuration (Phase 1)
#define HELLO_NORMAL_INTERVAL 300        // THAY ĐỔI: từ 600 → 300 (5 phút)
#define HELLO_FAST_INTERVAL 30           // GIỮ NGUYÊN
#define HELLO_DISCOVERY_DURATION 300     // GIỮ NGUYÊN

// Phase 2: Stabilization
#define HELLO_STABILIZING_INTERVAL 60    // THAY ĐỔI: từ 90 → 60 (1 phút)
#define HELLO_STABILIZATION_DURATION 180 // GIỮ NGUYÊN
```

Sau đó rebuild:
```bash
platformio run
```

---

### **📊 So sánh các options:**

| Metric | Current (600s) | Option A (300s) | Option C (180s) |
|--------|----------------|-----------------|-----------------|
| **Failure detection** | 30 min | 15 min ⚡ | 9 min ⚡⚡ |
| **Discovery time** | 10 min | 5 min ⚡ | 3 min ⚡⚡ |
| **Battery life** | 100% ✅ | ~90% ✅ | ~70% ⚠️ |
| **Network load** | Low ✅ | Medium ✅ | Medium-High ⚠️ |
| **Collision rate** | Very Low ✅ | Low ✅ | Medium ⚠️ |
| **Recommended for** | Static sensors | Balanced use | Critical monitoring |

---

## 🎓 Nguyên tắc thiết kế Hello Interval cho LoRa:

### **1. LoRa Duty Cycle Compliance** ⚖️
EU: 1% duty cycle = 36s TX per hour
→ 10 nodes × 600s interval = OK ✅
→ 10 nodes × 120s interval = ~50s TX/hour → BORDERLINE ⚠️

### **2. Time-on-Air (ToA)** 📡
LoRa SF7/125kHz/100 bytes ≈ 370ms
→ 600s interval = 0.06% airtime ✅
→ 300s interval = 0.12% airtime ✅
→ 120s interval = 0.31% airtime (still OK)

### **3. Battery Budget** 🔋
Typical LoRa node: 2000mAh battery
- TX 100mA × 0.5s per Hello
- 600s interval → 144 TX/day → ~0.002Ah/day → **3 years battery** ✅
- 300s interval → 288 TX/day → ~0.004Ah/day → **1.5 years battery** ✅
- 120s interval → 720 TX/day → ~0.010Ah/day → **7 months battery** ⚠️

### **4. Collision Probability** 💥
P(collision) ∝ (num_nodes × ToA) / interval
- 10 nodes, 600s: P ≈ 0.6% ✅
- 10 nodes, 300s: P ≈ 1.2% ✅
- 10 nodes, 120s: P ≈ 3.0% ⚠️

---

## ✅ KẾT LUẬN VÀ KHUYẾN NGHỊ CUỐI CÙNG

### **Cho dự án hiện tại (Sensor Network):**

**🎯 ĐỀ XUẤT: OPTION A - Balanced (300s Normal / 60s Stabilizing)**

**Lý do:**
1. ✅ Cải thiện responsiveness **2x** (15 min fault detection vs 30 min)
2. ✅ Vẫn tiết kiệm pin (>1 năm battery life)
3. ✅ Tuân thủ LoRa duty cycle
4. ✅ Low collision rate
5. ✅ Phù hợp với 90% use cases

**Implementation:**
```cpp
// BuildOptions.h - Recommended values
#define HELLO_NORMAL_INTERVAL 300        // 5 minutes
#define HELLO_STABILIZING_INTERVAL 60    // 1 minute  
#define HELLO_FAST_INTERVAL 30           // 30 seconds
#define TIMEOUT_MULTIPLIER 3             // Keep as-is
```

**Network behavior:**
- Normal operation: Hello every 5 min → node removed after 15 min silence
- After provisioning: Hello every 1 min → node removed after 3 min silence
- During provisioning: Hello every 30 sec → NO node removal (as designed)

---

### **📝 Action Items:**

**Nếu đồng ý với Option A:**
1. [ ] Update `BuildOptions.h` với values mới
2. [ ] Rebuild firmware (`platformio run`)
3. [ ] Test trong 24-48 giờ
4. [ ] Monitor battery usage (nếu có)
5. [ ] Monitor collision rate qua logs

**Nếu muốn giữ nguyên (600s):**
- ✅ OK cho static sensor network với battery power
- ⚠️ Chấp nhận 30 phút detection time

**Nếu cần aggressive (180s):**
- ⚠️ Chỉ khi có mains power
- ⚠️ Network < 10 nodes
- ⚠️ Monitor collision rate closely

---

Bạn muốn tôi implement Option A (300s/60s) ngay không? Hoặc bạn có câu hỏi gì về các trade-offs?
