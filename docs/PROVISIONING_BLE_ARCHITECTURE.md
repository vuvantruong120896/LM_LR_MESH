# BLE Provisioning Architecture - Phase 3

## Overview

This document describes the unified BLE provisioning system for both Gateway and Node devices in the Kagri system. Both devices use BLE (Bluetooth Low Energy) for secure provisioning from the Mobile App, with automatic netkey derivation for LoRa mesh network consistency.

---

## Architecture Components

### 1. Gateway BLE Provisioning

#### Service UUID
```
0000ffb0-0000-1000-8000-00805f9b34fb
```

#### Characteristics
| Name | UUID Suffix | Properties | Purpose |
|------|-------------|-----------|---------|
| Provisioning Data | ffb1 | Read/Write | Receive provisioning payload from Mobile App |
| Response Data | ffb2 | Notify | Send Gateway MAC and status to Mobile App |

#### Gateway BLE Advertising
```
Device Name: KAGRI-GW-XXXX (where XXXX = last 4 digits of Gateway MAC)
Service UUID: 0000ffb0-xxxx (16-bit format)
TX Power: -3dBm
Connectable: Yes
Discoverable: Yes
```

### 2. Node BLE Provisioning

#### Service UUID
```
0000ffc0-0000-1000-8000-00805f9b34fb
```

#### Characteristics
| Name | UUID Suffix | Properties | Purpose |
|------|-------------|-----------|---------|
| Provisioning Data | ffc1 | Read/Write | Receive provisioning payload with Gateway MAC |
| Response Data | ffc2 | Notify | Send node address and provisioning status |

#### Node BLE Advertising
```
Device Name: KAGRI-NODE-XXXX (where XXXX = last 4 digits of Node MAC)
Service UUID: 0000ffc0-xxxx (16-bit format)
TX Power: -3dBm
Connectable: Yes
Discoverable: Yes (only when not provisioned)
```

---

## Provisioning Payload Formats

### Gateway Provisioning Request (Mobile App → Gateway)

**JSON Format:**
```json
{
  "userUID": "user-uuid-from-auth",
  "deviceMode": 2,
  "isWiFi": true,
  "wifiSSID": "Network-Name",
  "wifiPassword": "password123",
  "timestamp": 1700000000
}
```

**For Cellular Gateway:**
```json
{
  "userUID": "user-uuid-from-auth",
  "deviceMode": 2,
  "isWiFi": false,
  "timestamp": 1700000000
}
```

**Payload Structure (binary):**
- Byte 0-19: userUID (20 bytes, ASCII)
- Byte 20-31: wifiSSID (12 bytes, null-padded if shorter)
- Byte 32-63: wifiPassword (32 bytes, null-padded if shorter)
- Byte 64-67: isWiFi flag (1 = WiFi, 0 = Cellular)
- Byte 68-71: timestamp (4 bytes, little-endian)

**Total Size:** ~73 bytes

### Gateway Response (Gateway → Mobile App)

**JSON Format:**
```json
{
  "status": "success",
  "message": "Gateway provisioned successfully",
  "gatewayMAC": "AA:BB:CC:DD:EE:FF",
  "deviceMode": 2,
  "timestamp": 1700000001
}
```

**Gateway Error Response:**
```json
{
  "status": "error",
  "message": "WiFi credentials required",
  "code": "ERR_WIFI_MISSING"
}
```

### Node Provisioning Request (Mobile App → Node)

**JSON Format:**
```json
{
  "userUID": "user-uuid-from-auth",
  "gatewayMAC": "AA:BB:CC:DD:EE:FF",
  "deviceMode": 3,
  "timestamp": 1700000000
}
```

**Payload Structure (binary):**
- Byte 0-19: userUID (20 bytes, ASCII)
- Byte 20-35: gatewayMAC (16 bytes, ASCII "AA:BB:CC:DD:EE:FF")
- Byte 36-39: timestamp (4 bytes, little-endian)

**Total Size:** ~40 bytes

### Node Response (Node → Mobile App)

**JSON Format:**
```json
{
  "status": "success",
  "message": "Node provisioned successfully",
  "nodeAddress": 2,
  "gatewayMAC": "AA:BB:CC:DD:EE:FF",
  "deviceMode": 3,
  "timestamp": 1700000001
}
```

**Node Error Response:**
```json
{
  "status": "error",
  "message": "Invalid Gateway MAC",
  "code": "ERR_INVALID_GATEWAY_MAC"
}
```

---

## Netkey Derivation Algorithm

### Shared Algorithm (Gateway & Node)

Both Gateway and Node use the **same algorithm** to derive the network key, ensuring they produce identical netkeys.

**Algorithm:**
```
netkey = SHA256(userUID + gatewayMAC)
```

**Implementation (C++):**
```cpp
void CryptoUtils::deriveNetkey(const String& userUID, 
                               const unsigned char* gatewayMacBytes, 
                               unsigned char* netkey) {
    // Create SHA256 context
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA256
    
    // Hash userUID (ASCII string)
    mbedtls_sha256_update(&ctx, (unsigned char*)userUID.c_str(), userUID.length());
    
    // Hash Gateway MAC (6 bytes binary)
    mbedtls_sha256_update(&ctx, gatewayMacBytes, 6);
    
    // Finalize hash
    mbedtls_sha256_finish(&ctx, netkey);
    mbedtls_sha256_free(&ctx);
}
```

### Example Derivation

```
userUID = "user-12345678901234567890"
gatewayMAC = AA:BB:CC:DD:EE:FF (binary: 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF)

Input = "user-12345678901234567890" + 0xAA 0xBB 0xCC 0xDD 0xEE 0xFF
Output (netkey) = 32-byte SHA256 hash
```

---

## Storage Architecture

### Gateway NVS Storage

**Namespace:** `kagri_prov` (Gateway provisioning)

| Key | Size | Type | Description |
|-----|------|------|-------------|
| `provisioned` | 1 byte | uint8 | Provisioning status (1=provisioned, 0=not) |
| `user_uid` | 20 bytes | String | User identifier from auth system |
| `netkey` | 32 bytes | Binary | Derived network key |
| `wifi_ssid` | 32 bytes | String | WiFi SSID (empty if Cellular) |
| `wifi_pass` | 32 bytes | String | WiFi password (empty if Cellular) |

**Namespace:** `mesh_config` (Shared mesh configuration)

| Key | Size | Type | Description |
|-----|------|------|-------------|
| `networkConfig` | 64+ bytes | Binary | LoRa mesh network parameters |

### Node NVS Storage

**Namespace:** `kagri_node` (Node provisioning)

| Key | Size | Type | Description |
|-----|------|------|-------------|
| `provisioned` | 1 byte | uint8 | Provisioning status (1=provisioned, 0=not) |
| `user_uid` | 20 bytes | String | User identifier from auth system |
| `netkey` | 32 bytes | Binary | Derived network key (same as Gateway) |
| `node_addr` | 1 byte | uint8 | Node address in mesh network (1-254) |

---

## Provisioning Flow Diagram

### Gateway Provisioning Flow

```
Mobile App                  Gateway
    |                          |
    |-- BLE Discovery -------->|
    |<-- KAGRI-GW-XXXX --------|
    |                          |
    |-- Connect ------------->|
    |<-- Connected ------------|
    |                          |
    |-- Write Prov Data ------>| (userUID, WiFi/Cellular)
    |                          | (Validate credentials if WiFi)
    |                          | (Derive netkey)
    |                          | (Save to NVS)
    |<-- Notify Response ------|  (status, gatewayMAC)
    |                          |
```

### Node Provisioning Flow

```
Mobile App                  Node
    |                          |
    |-- BLE Discovery -------->|
    |<-- KAGRI-NODE-XXXX ------|
    |                          |
    |-- Connect ------------->|
    |<-- Connected ------------|
    |                          |
    |-- Write Prov Data ------>| (userUID, gatewayMAC)
    |                          | (Parse gatewayMAC)
    |                          | (Derive netkey using gatewayMAC)
    |                          | (Assign node address)
    |                          | (Save to NVS)
    |<-- Notify Response ------|  (status, nodeAddress)
    |                          |
```

### Multi-Node Provisioning Flow

```
Mobile App                  Gateway         Node1          Node2
    |                          |              |              |
    |-- Provision Gateway ---->|              |              |
    |<-- gatewayMAC response --|              |              |
    |                          |              |              |
    |-- Discover Nodes --------|              |              |
    |<-- KAGRI-NODE-1111 ------|              |              |
    |<-- KAGRI-NODE-2222 ------|              |              |
    |                          |              |              |
    |-- Provision Node1 -------|------------>|              |
    |                          |              | (netkey = SHA256(uid+gw_mac))
    |<-- Node1 Address --------|<------------|              |
    |                          |              |              |
    |-- Provision Node2 -------|------------------------>|
    |                          |              |              | (netkey = SHA256(uid+gw_mac))
    |<-- Node2 Address --------|<------------------------|
    |                          |              |              |
    | (All devices have same netkey)        |              |
```

---

## Conditional Compilation Modes

### WiFi Mode (Default)
```cpp
// platformio.ini
// No USE_CELLULAR defined
```

**Gateway Behavior:**
- Requires WiFi SSID and password in provisioning
- Connects to WiFi on boot
- Uses Firebase HTTPS over WiFi

### Cellular Mode
```cpp
// platformio.ini
// -D USE_CELLULAR
```

**Gateway Behavior:**
- WiFi provisioning fields ignored
- Uses A7682S modem for cellular connectivity
- Uses Firebase HTTPS over cellular (2G/3G/4G)
- No WiFi configuration needed

**Node Behavior:**
- Identical for both modes
- Only LoRa mesh communication

---

## Security Considerations

### Netkey Derivation
- **Input:** userUID (from auth token) + Gateway MAC
- **Output:** 256-bit SHA256 hash
- **Strength:** Unique per user per gateway
- **Attack Vector:** Requires both userUID AND Gateway MAC to derive netkey

### Data Storage
- All netkeys stored in NVS (encrypted by ESP32 flash encryption)
- WiFi credentials stored only on Gateway
- Each device has isolated NVS namespace

### BLE Communication
- No encryption at BLE level (relies on WiFi/Cellular for remote provisioning)
- Local provisioning only (no remote BLE)
- MAC address verification recommended before connecting

---

## Provisioning State Machine

### Gateway States
```
NOT_PROVISIONED
    ↓ (BLE Write triggered)
VALIDATING (WiFi check if applicable)
    ↓
PROVISIONING (Netkey derivation)
    ↓
PROVISIONED (Save to NVS)
```

### Node States
```
NOT_PROVISIONED (BLE Advertising)
    ↓ (BLE Write triggered)
VALIDATING (Gateway MAC format check)
    ↓
PROVISIONING (Netkey derivation, Address assignment)
    ↓
PROVISIONED (Save to NVS, Stop advertising)
```

---

## Error Handling

### Gateway Errors

| Code | Message | Cause | Recovery |
|------|---------|-------|----------|
| `ERR_WIFI_MISSING` | WiFi SSID required | isWiFi=true but no SSID | Resend with SSID |
| `ERR_WIFI_INVALID` | WiFi connection failed | Cannot connect to network | Check credentials, retry |
| `ERR_INVALID_PAYLOAD` | Invalid provisioning data | Malformed JSON/binary | Validate payload format |
| `ERR_ALREADY_PROVISIONED` | Device already provisioned | Re-provisioning attempt | Reset device first |

### Node Errors

| Code | Message | Cause | Recovery |
|------|---------|-------|----------|
| `ERR_INVALID_GATEWAY_MAC` | Invalid Gateway MAC format | Malformed MAC address | Check MAC format (AA:BB:CC:DD:EE:FF) |
| `ERR_INVALID_PAYLOAD` | Invalid provisioning data | Malformed JSON/binary | Validate payload format |
| `ERR_ADDRESS_CONFLICT` | Node address unavailable | All addresses taken | Remove unused nodes |
| `ERR_ALREADY_PROVISIONED` | Device already provisioned | Re-provisioning attempt | Reset device first |

---

## Testing Checklist

- [ ] Gateway BLE discovery works correctly
- [ ] Gateway accepts WiFi credentials and validates connection
- [ ] Gateway derives correct netkey from userUID
- [ ] Gateway responds with correct MAC address
- [ ] Node BLE discovery works correctly
- [ ] Node accepts Gateway MAC in provisioning payload
- [ ] Node derives same netkey as Gateway
- [ ] Node receives unique address
- [ ] Multiple nodes derive same netkey (netkey consistency)
- [ ] Nodes can form LoRa mesh after provisioning
- [ ] Gateway-Node communication works over mesh
- [ ] WiFi/Cellular mode switching works
- [ ] NVS data persists after reboot
- [ ] Reprovisioning after factory reset works

---

## Files Reference

### Gateway Implementation
- `src/application/app_gateway/ble_provisioning.h/cpp`
- `src/application/app_gateway/provision_manager.h/cpp`
- `src/application/app_gateway/crypto_utils.h/cpp`

### Node Implementation
- `src/application/app_node/ble_provisioning_node.h/cpp`
- `src/application/app_node/provision_manager_node.h/cpp`

### Configuration
- `platformio.ini` (esp32-gateway, esp32-node environments)
- `partitions_custom_gateway.csv` (Gateway partition layout)
- `partitions_custom_node.csv` (Node partition layout)

