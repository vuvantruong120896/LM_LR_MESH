# 🔄 Modbus RTU Async Design (Non-blocking)

## 1. Phân Tích Luồng Hiện Tại (Blocking)

Hiện tại, `ModbusRTUDriver` hoạt động theo cơ chế **Blocking (Đồng bộ)**:

```cpp
// Code hiện tại (Blocking)
bool readInputRegisters(...) {
    sendRequest(); // Gửi lệnh
    
    // 🛑 BLOCKING HERE!
    // CPU bị chặn ở đây 500ms để chờ phản hồi
    while (waiting_for_bytes) {
        uart_read_bytes(..., timeout=500ms);
    }
    
    return success;
}
```

**Vấn đề:**
1.  **Lãng phí CPU:** Khi chờ cảm biến phản hồi (thường 50-200ms), CPU không làm gì cả.
2.  **Ảnh hưởng Mesh:** Nếu `SensorTask` chạy cùng Core hoặc độ ưu tiên cao, nó sẽ chặn Mesh xử lý packet.
3.  **Timeout tích lũy:** Nếu cảm biến lỗi, ta retry 3 lần x 500ms = 1.5 giây hệ thống bị "đơ".

---

## 2. Thiết Kế Mới (Async / Non-blocking)

Sử dụng kiến trúc tương tự `ATCommandAsync` của Cellular, nhưng đơn giản hơn vì Modbus là Master-Slave (chỉ 1 request tại 1 thời điểm).

### 🎯 Kiến Trúc

```
┌──────────────────────┐          ┌──────────────────────┐
│   Application        │          │   ModbusAsync        │
│ (SensorTask/Service) │          │ (Driver Layer)       │
└──────────┬───────────┘          └──────────┬───────────┘
           │                                 │
           │ 1. sendRequestAsync()           │
           │ (Non-blocking)                  │
           ├────────────────────────────────►│
           │ Returns TransactionID           │ 2. UART Write
           │                                 │ (Direct/Queue)
           │                                 │
           │ 3. Do other work...             │
           │ (Mesh, WiFi, etc.)              │
           │                                 │
           │                                 │ ◄─── UART RX ISR/Task ───
           │                                 │      (Background)
           │                                 │
           │ 4. isTransactionComplete?       │
           │ or Callback                     │
           ├────────────────────────────────►│
           │ Returns Data / Status           │
           │                                 │
           └─────────────────────────────────┘
```

### 🛠️ Thành Phần Chính

#### 1. `ModbusAsync` Class
Quản lý việc gửi và nhận không đồng bộ.

-   **`sendRequestAsync(slave, func, addr, count)`**:
    -   Tạo `Transaction` (lưu thông tin request để biết cần nhận bao nhiêu byte).
    -   Gửi frame qua UART ngay lập tức.
    -   Trả về `transactionId`.
    -   Chuyển trạng thái sang `WAITING_RESPONSE`.

-   **`receiverTask()` (Core 1)**:
    -   Chạy vòng lặp đọc UART (non-blocking hoặc timeout ngắn).
    -   Khi nhận byte: Tích lũy vào buffer.
    -   Kiểm tra điều kiện hoàn thành:
        -   Đủ số byte dự kiến (tính toán từ request).
        -   Hoặc phát hiện Exception frame (3-5 bytes).
        -   Kiểm tra CRC.
    -   Nếu OK: Đánh dấu `COMPLETED`, lưu dữ liệu.
    -   Nếu Timeout: Đánh dấu `TIMEOUT`.

#### 2. `ModbusTransaction` Struct
Lưu trạng thái của request đang xử lý.

```cpp
struct ModbusTransaction {
    uint32_t id;
    uint32_t timestamp;
    uint8_t slaveAddr;
    uint8_t funcCode;
    uint16_t startAddr;
    uint16_t count;
    
    // Trạng thái
    enum State { IDLE, PENDING, COMPLETED, TIMEOUT, ERROR } state;
    
    // Dữ liệu nhận được
    uint8_t responseBuffer[256];
    uint16_t responseLen;
};
```

### 🔄 Luồng Hoạt Động Chi Tiết

1.  **Gửi (Send):**
    -   App gọi `sendRequestAsync(slave=1, func=3, addr=0, count=7)`.
    -   Driver tính toán: "À, đọc 7 thanh ghi thì phản hồi sẽ dài: 1+1+1+(7*2)+2 = 19 bytes".
    -   Driver gửi request frame.
    -   Lưu `expectedResponseLen = 19`.

2.  **Nhận (Receive - Background):**
    -   Receiver Task đọc byte từ UART.
    -   Khi đủ 19 bytes -> Tính CRC.
    -   Nếu CRC đúng -> `state = COMPLETED`.

3.  **Xử lý (App):**
    -   App kiểm tra `isCompleted(id)`.
    -   Nếu xong, lấy dữ liệu và parse ra giá trị cảm biến.

---

## 3. Kế Hoạch Thực Hiện

1.  Tạo `ModbusAsync` class (kế thừa hoặc thay thế `ModbusRTUDriver`).
2.  Implement `receiverTask` chạy trên Core riêng (hoặc sử dụng Event Queue).
3.  Sửa `SoilSensorService` để sử dụng API async thay vì blocking.

Bạn có đồng ý với thiết kế này không? Nếu có, tôi sẽ bắt đầu implement.
