# MAC Authentication Fix - Root Cause Analysis

## 🔍 Root Cause Discovered

### **Problem 1: Random Key Generation**
```cpp
// OLD CODE (BROKEN):
MeshSecurityConfig() {
    esp_fill_random(networkKey, MESH_NETKEY_SIZE);  // ❌ Each device gets different keys!
    esp_fill_random(authToken, MESH_AUTH_TOKEN_SIZE);
}

// FIXED:
MeshSecurityConfig() {
    memset(networkKey, 0, MESH_NETKEY_SIZE);       // ✅ Initialize to zero
    memset(authToken, 0, MESH_AUTH_TOKEN_SIZE);    // ✅ Keys set explicitly later
}
```

### **Problem 2: Incorrect MAC Calculation**
```cpp
// OLD CODE (BROKEN):
size_t dataSize = totalSize - MESH_MAC_SIZE;      // ❌ Includes MAC field in calculation!
generateMAC(packetData, dataSize, ...);

// FIXED:
// ✅ Exclude MAC field completely from calculation
// ✅ Use proper offset calculation
// ✅ Create clean data buffer without MAC field
```

## 🛠️ Key Changes Made

### 1. **Centralized Key Management**
- Created `mesh_security_keys.h` for unified keys
- All devices now use same `MESH_MASTER_NETWORK_KEY`
- Eliminated random key generation

### 2. **Fixed MAC Calculation**
- **Generation**: Exclude MAC field from data used to calculate MAC
- **Verification**: Use same logic to exclude MAC field
- **Proper offset**: Calculate MAC position in packet structure
- **Clean buffer**: Create temporary buffer without MAC field

### 3. **Enhanced Debugging**
- Log network keys at startup (first 8 bytes)
- Log packet data used for MAC calculation
- Log generated vs received MAC values
- Log MAC field offset and buffer sizes

## 📊 Expected Results

### **Before Fix:**
```
[D] Using Network Key: 2B7E151628AED2A6...
[D] Expected MAC: 95B17355
[D] Received MAC: D745809D    ← Different!
[W] MAC verification failed: -5
```

### **After Fix:**
```
[D] Using Network Key: 2B7E151628AED2A6...
[D] Expected MAC: A1B2C3D4
[D] Received MAC: A1B2C3D4    ← Same! ✅
[V] MAC verification successful
[I] Packet decrypted successfully
```

## 🧪 Testing Instructions

1. **Flash both devices** with new firmware
2. **Check startup logs** - network keys must match
3. **Monitor packet exchange** - MAC values should match
4. **Verify success** - no more "authentication failed" errors

## 📝 Technical Details

### MAC Field Position
```
SecureDataPacket Structure:
├── header (SecurePacketHeader)
│   ├── originalHeader (PacketHeader)
│   ├── securityHeader (SecurityPacketHeader)
│   │   ├── securityType, flags, sequenceNumber, nonce
│   │   └── mac[4]  ← MAC field position
│   └── securityLevel, originalPayloadSize, encryptionFlags
└── payload[]
```

### MAC Calculation Method
```cpp
// Calculate offset of MAC field
size_t macOffset = offsetof(SecureDataPacket, header) + 
                   offsetof(SecurePacketHeader, securityHeader) + 
                   offsetof(SecurityPacketHeader, mac);

// Split data: [before_mac] + [after_mac] 
// Exclude MAC field itself from calculation
```

This fix ensures that MAC calculation is **deterministic** and **consistent** between sender and receiver.