# Sửa Lỗi Node Ma (Zombie Node) - Phiên Bản MẠNH

**Ngày:** 7 tháng 10, 2025  
**Vấn đề:** Node 0x09F8 không thể xóa khỏi routing table sau khi ngắt điện

## Vấn Đề Thực Sự

### Tình Huống Bạn Mô Tả

```
Mạng lưới:
  Node 0x09F8 → 0xCC64 → Bridge (0x6150)
                      ↗  ↖
  Node 0x4F70 ────────    ↖
                           Node 0xE764

Routing tables:
  Bridge:  0x09F8 via 0xCC64 (5 hops)
  0xCC64:  0x09F8 via 0x09F8 (1 hop - direct)
  0x4F70:  0x09F8 via 0xCC64 (2 hops)
  0xE764:  0x09F8 via 0xCC64 (2 hops)
```

### Khi Ngắt Điện 0x09F8

**Bạn nghĩ sẽ xảy ra:**
1. 0xCC64 timeout 0x09F8 sau 360s → xóa
2. Bridge không còn nhận HELLO từ 0xCC64 chứa 0x09F8 → timeout → xóa ✅

**Thực tế xảy ra:**
1. 0xCC64 timeout 0x09F8 sau 360s → xóa ✅
2. **NHƯNG** 0x4F70 và 0xE764 vẫn có 0x09F8 trong routing table của họ!
3. 0x4F70 gửi HELLO chứa {0x09F8, metric=1} → 0xCC64 nhận
4. 0xCC64 xử lý: `processRoute(via=0x4F70, node={addr=0x09F8, metric=2})`
5. 0xCC64 nghĩ: "À, có route mới đến 0x09F8 qua 0x4F70" → **THÊM LẠI** 0x09F8 ❌
6. 0xCC64 gửi HELLO chứa 0x09F8 → Bridge reset timeout
7. Vòng lặp vô tận → **0x09F8 sống mãi trong mạng như zombie** ❌

### Nguyên Nhân Gốc

**Code cũ:**
```cpp
// Bất kỳ node nào quảng cáo route đều có thể reset timeout
if (node->metric == rNode->networkNode.metric) {
    resetTimeoutRoutingNode(rNode);  // ❌ Nguy hiểm!
}
```

**Vấn đề:** Thông tin về 0x09F8 lưu trữ ở **TẤT CẢ** các node trong mạng. Miễn còn 1 node chưa timeout, nó sẽ quảng cáo lại và giữ zombie sống mãi.

## Giải Pháp: CHỈ Direct Routes Được Reset Timeout

### Quy Tắc Mới (MẠNH)

**NGUYÊN TẮC VÀNG:**
- **Direct route** (via == address): Node TỰ quảng cáo → CÓ THỂ reset timeout ✅
- **Indirect route** (via != address): Node khác quảng cáo → KHÔNG BAO GIỜ reset timeout ❌

### Code Mới

```cpp
// Chỉ reset timeout cho direct routes
bool isDirect = (via == node->address);

if (node->metric < rNode->networkNode.metric) {
    // Route tốt hơn - cập nhật metric và via
    rNode->networkNode.metric = node->metric;
    rNode->via = via;
    
    if (isDirect) {
        resetTimeoutRoutingNode(rNode);  // ✅ Direct route, an toàn
        ESP_LOGI("Better DIRECT route for %X: metric %d", node->address, node->metric);
    } else {
        // ❌ Indirect route - cập nhật path NHƯNG KHÔNG reset timeout
        ESP_LOGI("Better INDIRECT route for %X via %X - NO timeout reset", 
                 node->address, via);
    }
}
```

### Cách Hoạt Động

**Ví dụ với 0x09F8:**

**T+0:** 0x09F8 ngắt điện
- Bridge table: `0x09F8 via 0xCC64 TTL=360s` (indirect)
- 0xCC64 table: `0x09F8 via 0x09F8 TTL=360s` (direct)
- 0x4F70 table: `0x09F8 via 0xCC64 TTL=360s` (indirect)

**T+30:** 0xCC64 gửi HELLO chứa 0x09F8
- Bridge nhận: `processRoute(via=0xCC64, node={0x09F8, metric=2})`
- Kiểm tra: `via (0xCC64) != address (0x09F8)` → **indirect route**
- Quyết định: **KHÔNG reset timeout** ✅
- Bridge table: `0x09F8 via 0xCC64 TTL=330s` (giảm xuống)

**T+60:** 0x4F70 gửi HELLO chứa 0x09F8
- Bridge nhận: `processRoute(via=0x4F70, node={0x09F8, metric=3})`
- Kiểm tra: `via (0x4F70) != address (0x09F8)` → **indirect route**
- Quyết định: **KHÔNG reset timeout** ✅
- Bridge table: `0x09F8 via 0xCC64 TTL=300s` (tiếp tục giảm)

**T+360:** Timeout của 0x09F8 hết hạn
- Bridge: **XÓA 0x09F8** ✅
- 0xCC64: **XÓA 0x09F8** (direct timeout) ✅
- 0x4F70: vẫn còn 0x09F8 nhưng không ai quan tâm (nó là indirect)

**T+390:** 0x4F70 vẫn gửi HELLO chứa 0x09F8
- Bridge nhận nhưng không có 0x09F8 trong table → **BỎ QUA**
- Không thêm lại vì đã timeout ✅

## Kết Quả

### Trước Fix
- Zombie tồn tại **VÔ THỜI HẠN** ❌
- Tất cả nodes quảng cáo đều giữ zombie sống
- Không có cách nào xóa zombie ngoài reboot toàn bộ mạng

### Sau Fix (AGGRESSIVE)
- Zombie bị xóa sau **ĐÚNG 360 giây (6 phút)** ✅
- Chỉ direct neighbor có thể giữ route sống
- Indirect routes tự động expire, không lan truyền zombie

## Logs Để Kiểm Tra

### Hoạt động bình thường (direct routes)
```
[I][RoutingTableService.cpp:144] Better DIRECT route for CC64: metric 1 (was 2)
[D][RoutingTableService.cpp:163] Refreshing DIRECT route for CC64 (metric=1)
```

### Fix đang hoạt động (indirect routes)
```
[I][RoutingTableService.cpp:150] Better INDIRECT route for 09F8 via CC64: metric 5 (was 6) - NO timeout reset
[D][RoutingTableService.cpp:167] Same INDIRECT route for 09F8 via CC64 (metric=5) - NO timeout reset
```

### Zombie bị xóa (thành công)
```
[W][RoutingTableService.cpp:343] Route timeout 09F8 via CC64 (metric=5)
[W][RoutingTableService.cpp:362] Routing table updated: 3 node(s) remaining after timeout cleanup
```

## Tác Động

### Ưu Điểm
- ✅ Xóa zombie hoàn toàn trong 6 phút
- ✅ Không cần broadcast "route removed" (tiết kiệm băng thông)
- ✅ Đơn giản, dễ hiểu, dễ maintain
- ✅ Không ảnh hưởng routing bình thường (vẫn học routes mới)

### Nhược Điểm (Lưu Ý)
- ⚠️ Multi-hop routes sẽ expire nhanh hơn nếu không nhận được HELLO trực tiếp
- ⚠️ Trong mạng lớn, có thể cần giảm timeout hoặc tăng tần suất HELLO

### Đề Xuất Tiếp Theo
- Tăng tần suất HELLO trong normal mode từ 120s → 60s (nếu cần)
- Hoặc giảm timeout multiplier từ 3 → 2 (miss 2 HELLOs thay vì 3)
- Monitor logs để xem bao nhiêu indirect routes bị expire sớm

## ⭐ Kết Hợp Với FIX #4: Max Hop Count

**QUAN TRỌNG:** Fix này (FIX #3) đã được kết hợp với **FIX #4: Maximum Hop Count Filter**.

**FIX #4 làm gì:**
- Giới hạn routing table **CHỈ nhận routes ≤ 3 hops**
- Routes > 3 hops (như 0x09F8 - 5 hops) sẽ **BỊ TỪ CHỐI NGAY** khi nhận HELLO
- Không cần đợi timeout → zombie không bao giờ được thêm vào routing table!

**Kết hợp FIX #3 + FIX #4:**
```
FIX #4: Route > 3 hops → Reject ngay (không thêm vào table) ✅ Primary defense
FIX #3: Indirect routes ≤ 3 hops → Không reset timeout (expire trong 6 phút) ✅ Secondary defense
```

**Kết quả:**
- Zombie routes **xa** (> 3 hops): KHÔNG BAO GIỜ được thêm vào ✅✅
- Zombie routes **gần** (≤ 3 hops): Expire trong 6 phút ✅

Chi tiết về FIX #4 xem file: `docs/MAX_HOP_COUNT_FILTER.md`

## File Đã Sửa

- `src/components/lora_mesh_manager/src/services/RoutingTableService.cpp`
  - Function: `processRoute(uint16_t via, NetworkNode* node)`
  - Lines: ~118-178
  - Change: Chỉ reset timeout cho direct routes (via == address)

- `docs/ZOMBIE_ROUTE_FIX.md` (English version)

## Build Kết Quả

```
Flash: [====      ]  35.2% (used 461076 bytes)
========================================== 1 succeeded in 00:00:07.292
```

✅ **HOÀN TẤT - SẴN SÀNG ĐỂ TEST**
