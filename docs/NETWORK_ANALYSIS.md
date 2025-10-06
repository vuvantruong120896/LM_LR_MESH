# LoRa Mesh Network Analysis & Improvement Roadmap

Ngày cập nhật: 2025-10-05

Ngôn ngữ: VI (tiếng Việt chính) + EN (tóm tắt)

---
## 1. Tổng Quan Hiện Trạng (Current State)

| Thành phần | Mô tả | Nhận xét |
|------------|------|----------|
| Routing Protocol | Distance Vector (Hello broadcast định kỳ, metric = hop count) | Đơn giản, dễ triển khai nhưng hạn chế về chất lượng và scalability |
| Hello Interval | 300s (5 phút) | Chậm trong phát hiện node hỏng |
| Timeout | 900s (15 phút) | Quá dài cho ứng dụng cần phản ứng nhanh |
| Packet Size Limit | 150 bytes (tăng từ 100) | Hỗ trợ ~21 routes/Hello, sensor payload ~120B. ToA tăng ~40% |
| Persistence | NVS lưu routing table | Lưu cả indirect routes → dẫn tới reject khi load lại |
| Security | MAC authentication (Network Key) | Chưa có encryption, replay protection, key rotation |
| Link Quality | Không sử dụng (không xét RSSI/SNR) | Route "ngắn nhất" có thể không phải tốt nhất |
| Collisions | Không có CSMA/LBT | Dễ xảy ra va chạm khi nhiều node gửi |
| Energy | Không duty-cycling | Không tối ưu cho battery node |
| Diagnostics | Có log cơ bản | Chưa có health report hay tracing |

---
## 2. Các Vấn Đề/Nút Thắt (Pain Points & Bugs)

### 2.1 Bugs đã phát hiện
1. Self-Route Loop: Chấp nhận route tới chính nó qua node khác → tạo vòng lặp.
2. Zombie Route Propagation: Route hết hạn ở 1 node vẫn được phát tán từ node khác (thiếu expiry sync).
3. NVS Indirect Route Bug: Lưu indirect routes, khi load lại bị loại bỏ (metric > phép nhận) → mất thông tin.

### 2.2 Giới hạn kiến trúc
| Vấn đề | Ảnh hưởng |
|--------|-----------|
| Hop-count only | Không phản ánh chất lượng đường truyền |
| Hello full-table broadcast | Không scale khi > 30 nodes (cải thiện: 150B → ~23 routes/packet) |
| 150B packet cap | Cải thiện từ 100B, vẫn giới hạn nếu mạng > 30 nodes |
| Timeout dài (15m) | Phát hiện lỗi chậm → gửi vào blackhole |
| Không congestion control | Có thể overflow queue khi tải cao |
| Không ưu tiên gói | ACK/Control có thể bị delay bởi data |
| Không encryption | Dữ liệu bị sniff được |
| Không chống replay | Có thể phát lại gói hợp lệ |

---
## 3. Phân Tích Routing Flow (Ví dụ điển hình)

Scenario: D gửi A qua B (D→B→A). Điều kiện cần:
1. D có route A via B.
2. B có route A (direct hoặc via hop khác).
3. Nếu B mất route tới A → gói bị drop (NextHop Not Found).

Điều này nhấn mạnh yêu cầu: Tính nhất quán routing table giữa các hop trung gian là tối quan trọng.

---
## 4. Định Lượng Sơ Bộ (Approx Metrics)

| Tham số | Trước (100B) | Hiện tại (150B) |
|---------|--------------|-----------------|
| Hello Overhead | ~ (8 + 6×N) bytes mỗi 300s | Không đổi |
| Max routes/Hello | ≈ floor((100 - 8) / 6) = 15 | **≈ floor((150 - 8) / 6) = 23** ✅ |
| ToA @SF7/BW250 | ~150ms | **~210ms** (+40%) ⚠️ |
| Effective max broadcast throughput | ~6.6 pkt/s | **~4.7 pkt/s** (-29%) |
| Convergence time (node chết) | 900s (timeout) | 900s (chưa đổi) |
| Max sensor payload (với security) | ~60 bytes | **~110 bytes** ✅ |

---
## 5. Chiến Lược Cải Tiến (Improvement Strategy)

### 5.1 Ưu tiên Ngắn Hạn (Stability First)
| Mục tiêu | Hành động | Ưu tiên |
|----------|-----------|---------|
| Loại bỏ bug nghiêm trọng | Fix 3 routing bugs | P0 |
| Giảm mất gói do collision | Thêm CSMA / Listen-Before-Talk | P1 |
| Phát hiện node chết nhanh hơn | Probe + ACK khi nghi ngờ | P1 |
| Ngăn route xấu | Thêm SNR-based metric phụ | P1 |

### 5.2 Trung Hạn (Performance & QoS)
| Mục tiêu | Hành động |
|----------|-----------|
| Tối ưu routing | Link Quality Metric (Hop + SNR + PacketLoss) |
| Giảm overhead | Gửi partial / incremental routing updates |
| Tăng hiệu quả ACK | Selective Repeat ARQ |
| Ưu tiên dữ liệu | Multi-queue + weighted scheduler |

### 5.3 Dài Hạn (Scalability & Security)
| Mục tiêu | Hành động |
|----------|-----------|
| Scale > 100 nodes | Hierarchical / Cluster routing |
| Payload lớn | Fragmentation & Reassembly |
| Bảo mật | AES-CTR + HMAC + replay window |
| OTA an toàn | Signed firmware distribution |
| Power saving | Duty cycle + adaptive TX power |

---
## 6. Lộ Trình (Proposed Roadmap)

| Giai đoạn | Thời gian | Nội dung |
|-----------|----------|----------|
| Phase 1 | Tuần 1–2 | Fix bugs, CSMA, Fast failure detection |
| Phase 2 | Tuần 3–5 | Link quality metric, selective ARQ, priority queue |
| Phase 3 | Tháng 2 | Encryption + replay protection + key derivation |
| Phase 4 | Tháng 3–4 | Fragmentation, hierarchical routing, health reporting |
| Phase 5 | Tháng 5+ | Duty cycling, adaptive ADR, secure OTA |

---
## 7. Đề Xuất Kỹ Thuật Cốt Lõi (Key Technical Proposals)

### 7.1 Metric Kết Hợp (Composite Metric)
```
Cost = α * Hop + β * (1 / NormalizedSNR) + γ * PacketLossRate
Khuyến nghị: α=1.0, β=0.5, γ=2.0
```

### 7.2 Fast Failure Detection
```
Idle > 120s → send PROBE (unicast)
No ACK 1 → SUSPECT
No ACK 2 (≥10s) → REMOVE ROUTE
```

### 7.3 CSMA/LBT đơn giản
```
while (retries < MAX) {
  if (channelBusy()) backoff(random 100–500 ms)
  else transmit();
}
```

### 7.4 Selective ARQ
Bitmap ACK window (32 gói) → chỉ retransmit gói thiếu.

### 7.5 Routing Compression (Ý tưởng)
| Kỹ thuật | Lợi ích |
|----------|---------|
| Base address + bitmap | Giảm 6N → (2 + N/8) |
| Metric nibble packing | 1 byte chứa 2 metric |
| TTL relative encoding | 1 byte delta |

### 7.6 Security Layer (Tối thiểu)
```
Header (routing) | IV(12) | Ciphertext | MAC(16)
Cipher: AES-CTR
MAC: HMAC-SHA256 (truncate 16B)
Replay: 64-bit rolling window per src
```

---
## 8. Risk & Trade-offs

| Thay đổi | Rủi ro | Giảm thiểu |
|----------|--------|------------|
| Thêm metric phức tạp | CPU load tăng | Cache + cập nhật lazy |
| Probe nhanh | Tăng traffic | Chỉ khi SUSPECT |
| Encryption | Overhead ~28B | Chỉ áp dụng cho data payload |
| Fragmentation | Mất 1 fragment → drop toàn bộ | Sequence + selective retry |
| Hierarchical routing | Phức tạp logic | POC nhỏ trước |

---
## 9. KPI Theo Dõi Sau Cải Tiến

| KPI | Trước | Mục tiêu |
|-----|-------|----------|
| Route convergence (node fail) | 900s | < 150s |
| Packet delivery ratio | ~70–85% (ước tính) | > 95% |
| Avg latency 2-hop | > 500ms (queue + collision) | < 250ms |
| Control overhead (%) | > 25% | < 12% |
| Energy duty cycle (sensor) | 100% | < 10% |
| Mean route quality (SNR-adjusted) | Baseline | +20% |

---
## 10. Kết Luận

Hệ thống hiện tại ổn định ở mức cơ bản nhưng hạn chế bởi:
- Routing đơn giản (hop-count only)
- Timeout dài gây phản ứng chậm
- Không phân biệt chất lượng đường truyền
- Thiếu các cơ chế chống va chạm & bảo mật nâng cao

Việc triển khai tuần tự các nhóm cải tiến (Stability ⇒ Performance ⇒ Security ⇒ Scalability ⇒ Energy) sẽ giúp hệ thống chuyển từ thử nghiệm sang sản phẩm tin cậy/đủ lớn.

---
## 11. Changelog & Implementation Status

### 2025-10-05a: MAX_PACKET_SIZE Increase (100 → 150 bytes) ✅
**Rationale:**
- Fix secure packet overflow crashes (sensor data + security overhead)
- Support more routes in Hello packets (13 → 21 nodes)
- Larger sensor payload capacity (60 → 110 bytes)
- Memory OK: 5.5% RAM usage, plenty of headroom
- PHY layer safe: 150B << 222B max @ SF7/BW125

**Trade-offs:**
- ⚠️ Time on Air +40% (150ms → 210ms) → collision risk up
- ✅ Mitigated by CSMA/LBT implementation (roadmap)
- ⚠️ **CRITICAL:** All nodes MUST upgrade to same max size!

**Test Results:**
- Build: ✅ Success (both bridge & node)
- Memory: Expected ~6-7% RAM (from 5.5%)
- Flash: No change (~34%)

### 2025-10-05b: Buffer Overflow in printDataPacket() ✅
**Bug:** Array iteration used byte count instead of struct count
**Impact:** Reading beyond buffer boundary → memory corruption → IllegalInstruction crash
**Root Cause:**
```cpp
// WRONG:
for (i = 0; i < payloadBytes; i++) { dPacket[i]; }  // 24 iterations!

// CORRECT:
numStructs = payloadBytes / sizeof(dataPacket);
for (i = 0; i < numStructs; i++) { dPacket[i]; }  // 2 iterations
```
**Fix:** Calculate number of structs before iteration + validation check
**Test:** Build ✅ (node: 448KB flash, bridge: 444KB flash)

### 2025-10-05c: NULL Packet Crash in processReceivedPackets() ✅
**Bug:** getNextAppPacket() can return NULL but no validation before use
**Impact:** Dereferencing NULL pointer → IllegalInstruction crash
**Symptoms:** Crash after printing routing table (background task race condition)
**Root Cause:**
```cpp
// WRONG:
AppPacket* packet = getNextAppPacket();
printDataPacket(packet);  // ← Crash if packet == NULL

// CORRECT:
AppPacket* packet = getNextAppPacket();
if (packet == nullptr) { continue; }  // ← Skip safely
printDataPacket(packet);
```
**Fix:** Add NULL checks in both processReceivedPackets() and printDataPacket()
**Test:** Build ✅ (node: 448.5KB flash, bridge: 444.4KB flash)

---
## 12. Next Actions (Đề nghị triển khai ngay)
1. ✅ **COMPLETED:** Tăng MAX_PACKET_SIZE 100→150 + fix secure packet overflow
2. Re-apply và harden 3 routing bug fixes.
3. Thêm fast failure detection (PROBE + ACK logic).
4. **HIGH PRIORITY:** Thêm CSMA/LBT (giảm collision do ToA tăng).
5. Thiết kế cấu trúc metric mở rộng (ghi đè dần).
6. Thêm health report packet (mỗi 30 phút).

---
## 13. English Executive Summary

The current mesh uses a pure distance-vector hop-count protocol with 300s hello intervals and 900s timeouts. Major weaknesses: slow failure detection, no link-quality awareness, limited scalability (100-byte cap), no collision avoidance, and only authenticity (no encryption). Short-term priority: fix routing bugs, add fast failure detection, implement CSMA, adopt composite quality metric. Mid-term: selective ARQ, fragmentation, hierarchical routing for scale. Long-term: security hardening (AES-CTR + HMAC, replay protection), duty cycling, secure OTA.

---
Nếu cần bổ sung biểu đồ luồng hoặc POC cho từng hạng mục, có thể mở rộng tài liệu này.

EOF
