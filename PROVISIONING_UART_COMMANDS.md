# UART Provisioning Control Commands

## Tổng quan
Bridge hiện đã hỗ trợ điều khiển quá trình provisioning qua UART commands, bao gồm:
- Bật provisioning mode (UART_CMD_START_PROVISIONING = 0x15)
- Tắt provisioning mode (UART_CMD_STOP_PROVISIONING = 0x16) 
- Lấy trạng thái provisioning (UART_CMD_GET_PROVISIONING_STATUS = 0x17)

## Cấu trúc UART Packet

```c
struct UartPacket {
   uint8_t startBytes[2];           // 0x4C, 0x4D
    uint8_t packetType;             // UART_PACKET_COMMAND (0x04)
    uint8_t payloadLength;          // Length of payload
    uint8_t sequenceNumber;         // Sequence number
    uint8_t payload[200];           // Command + data
    uint8_t checksum;               // XOR checksum
    uint8_t endByte;                // 0x55
};
```

## Cấu trúc Provisioning Control

```c
struct UartProvisioningControl {
    uint8_t action;                // 0=stop, 1=start, 2=get_status
    uint32_t durationMs;           // Duration in milliseconds (0=indefinite)
    uint8_t maxSessions;           // Max concurrent sessions
    uint8_t authMethod;            // Authentication method
};

struct UartProvisioningStatus {
    bool active;                   // Provisioning mode active
    uint32_t remainingTimeMs;      // Remaining time (0=indefinite)
    uint8_t activeSessions;        // Current active sessions
    uint8_t maxSessions;           // Maximum sessions allowed
    uint16_t totalRequests;        // Total provisioning requests
    uint16_t successfulProvisions; // Successful provisions
    uint16_t rejectedRequests;     // Rejected requests
};
```

## Commands

### 1. Bật Provisioning Mode
**Command:** 0x15 (UART_CMD_START_PROVISIONING)
**Payload:** [0x15] + UartProvisioningControl struct

Ví dụ:
```
startBytes: 0x4C, 0x4D
packetType: 0x04 (COMMAND)
payloadLength: 10
sequenceNumber: 0x01
payload: [0x15, 0x01, 0x00, 0x00, 0x4C, 0x1D, 0x04, 0x00, 0x00, 0x01]
         [cmd,  act,  ----duration(300s)----,  max, auth]
checksum: calculated XOR
endByte: 0x55
```

### 2. Tắt Provisioning Mode
**Command:** 0x16 (UART_CMD_STOP_PROVISIONING)
**Payload:** [0x16] only

Ví dụ:
```
startBytes: 0x4C, 0x4D
packetType: 0x04 (COMMAND)
payloadLength: 1
sequenceNumber: 0x02
payload: [0x16]
checksum: calculated XOR
endByte: 0x55
```

### 3. Lấy Trạng Thái Provisioning
**Command:** 0x17 (UART_CMD_GET_PROVISIONING_STATUS)
**Payload:** [0x17] only

Ví dụ:
```
startBytes: 0x4C, 0x4D
packetType: 0x04 (COMMAND)
payloadLength: 1
sequenceNumber: 0x03
payload: [0x17]
checksum: calculated XOR
endByte: 0x55
```

## Responses

Bridge sẽ phản hồi bằng:
1. **ACK packet** cho START/STOP commands
2. **Status packet** chứa UartProvisioningStatus cho GET_STATUS

### Response Format
```
startBytes: 0x4C, 0x4D
packetType: 0x02 (STATUS) hoặc 0x05 (ACK)
payloadLength: sizeof(UartProvisioningStatus) hoặc 1
sequenceNumber: sequence++
payload: UartProvisioningStatus struct hoặc ACK code
checksum: calculated XOR
endByte: 0x55
```

## Cách sử dụng

1. **Chuẩn bị Bridge:**
   - Đảm bảo Bridge đã flash firmware mới
   - Kết nối UART (Serial1) với ESP32 external
   - Đảm bảo Bridge đã có network key (qua UART_CMD_SET_NETKEY trước đó)

2. **Kích hoạt provisioning mode:**
   ```
   Gửi UART_CMD_START_PROVISIONING với:
   - durationMs: 600000 (10 phút)
   - maxSessions: 4 
   - authMethod: 1 (AUTH_METHOD_CHALLENGE)
   ```

3. **Node provisioning:**
   - Node gửi PROVISION_REQUEST_PACKET (0xE0) qua LoRa
   - Bridge tự động xử lý và phản hồi PROVISION_RESPONSE_PACKET (0xE1)
   - Nếu thành công, node gửi PROVISION_COMPLETE_PACKET (0xE2)

4. **Kiểm tra trạng thái:**
   - Gửi UART_CMD_GET_PROVISIONING_STATUS để xem sessions hiện tại
   - Bridge trả về UartProvisioningStatus với activeSessions, totalRequests, v.v.

5. **Dừng provisioning:**
   - Gửi UART_CMD_STOP_PROVISIONING để tắt mode sớm
   - Hoặc chờ timeout tự động

## Log Output Ví dụ

```
[BRIDGE] *** PROVISIONING CONTROL RECEIVED FROM UART ***
[BRIDGE] Action: 1 (0=stop, 1=start, 2=get_status)
[BRIDGE] Starting provisioning mode via UART command - Duration: 600000ms
[BRIDGE] Set max concurrent sessions: 4
[BRIDGE] Set auth method: 1
[UART] Sent provisioning status - Active: YES, Sessions: 0/4
[BRIDGE] Provisioning Status - Active: YES, Sessions: 0/4, Total: 0
```

## Tích hợp với External ESP32

External ESP32 có thể sử dụng class tương tự như sau:

```cpp
void sendStartProvisioning(uint32_t durationMs = 600000) {
    UartPacket packet;
   packet.startBytes[0] = 0x4C;
   packet.startBytes[1] = 0x4D;
    packet.packetType = 0x04; // COMMAND
    packet.sequenceNumber = ++seq;
    
    UartProvisioningControl control;
    control.action = 1;
    control.durationMs = durationMs;
    control.maxSessions = 4;
    control.authMethod = 1;
    
    packet.payloadLength = 1 + sizeof(control);
    packet.payload[0] = 0x15; // UART_CMD_START_PROVISIONING
    memcpy(&packet.payload[1], &control, sizeof(control));
    
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = 0x55;
    
    Serial1.write((uint8_t*)&packet, 6 + packet.payloadLength);
}
```

## Lưu ý

- Bridge phải được khởi tạo với ProvisioningService và có network key trước khi provisioning
- Provisioning mode tự động enable fast HELLO (15s interval) thay vì normal interval
- Các packet provisioning (0xE0-0xE3) được xử lý tự động khi provisioning mode active
- Bridge lưu trữ provisioned devices trong NVS và AddressManagementService
- Bảo mật: hiện tại dùng AUTH_METHOD_CHALLENGE với auto-authorization enabled