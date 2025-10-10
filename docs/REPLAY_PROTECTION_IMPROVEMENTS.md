# Replay Protection Security Improvements

## Overview
This document describes the security improvements made to the replay protection mechanism in the LoRa Mesh network.

## Date
2024-12-XX

## Changes Implemented

### PRIORITY 1: Hardcoded Replay Protection

**Problem**: Replay protection could be disabled via `config.enableReplayProtection` flag, creating a security vulnerability.

**Solution**: Removed the ability to disable replay protection at runtime.

**Files Modified**:
1. `MeshSecurityService.cpp` - `isValidSequenceNumber()`
   - **Before**: `if (!config.enableReplayProtection) return true;`
   - **After**: Removed the check entirely - replay protection is always enabled
   - Added comment: "Replay protection is always enabled (hardcoded for security)"

2. `SecurePacketService.cpp` - Packet decryption
   - **Before**: `if (MeshSecurityService::getConfig().enableReplayProtection) { ... }`
   - **After**: Removed conditional check, replay protection always runs
   - Added comment: "Check sequence number for replay protection (always enabled for security)"

**Note**: The `enableReplayProtection` flag still exists in `MeshSecurityConfig` for backward compatibility, but it is no longer checked in the code. The flag is always set to `true` during initialization and cannot be changed.

### PRIORITY 2: Increased Node Tracking Capacity

**Problem**: Limited capacity to track only 32 nodes, insufficient for larger networks.

**Solution**: Doubled the capacity from 32 to 64 nodes.

**Files Modified**:

1. `bridge_config.h`
   ```cpp
   // Before
   #define MAX_AUTHENTICATED_NODES 32
   
   // After
   #define MAX_AUTHENTICATED_NODES 64
   ```

2. `mesh_security.h` - Static array declarations
   ```cpp
   // Before
   static uint16_t authenticatedNodes[32];
   static uint32_t lastSequenceNumbers[32];
   static uint32_t recentReceiveBitmap[32];
   
   // After (with comments)
   static uint16_t authenticatedNodes[64];  // increased from 32 to 64
   static uint32_t lastSequenceNumbers[64];  // increased from 32 to 64
   static uint32_t recentReceiveBitmap[64];  // increased from 32 to 64
   ```

3. `MeshSecurityService.cpp` - Static member initialization
   ```cpp
   // Before
   uint16_t MeshSecurityService::authenticatedNodes[32] = {0};
   uint32_t MeshSecurityService::lastSequenceNumbers[32] = {0};
   uint32_t MeshSecurityService::recentReceiveBitmap[32] = {0};
   
   // After
   uint16_t MeshSecurityService::authenticatedNodes[64] = {0};
   uint32_t MeshSecurityService::lastSequenceNumbers[64] = {0};
   uint32_t MeshSecurityService::recentReceiveBitmap[64] = {0};
   ```

4. `MeshSecurityService.cpp` - Loop bounds
   ```cpp
   // Before
   for (int i = 0; i < 32 && i < authenticatedCount; i++)
   if (authenticatedCount < 32)
   
   // After
   for (int i = 0; i < 64 && i < authenticatedCount; i++)
   if (authenticatedCount < 64)
   ```

5. `MeshSecurityService.cpp` - `loadPeerTableFromNVS()`
   ```cpp
   // Before
   if (count > 32) count = 32;
   
   // After
   if (count > 64) count = 64;
   ```

### PRIORITY 2: Increased Sliding Window Size

**Problem**: Small window size (32) could cause legitimate out-of-order packets to be rejected in high-latency scenarios.

**Solution**: Doubled the window size from 32 to 64.

**Files Modified**:

1. `MeshSecurityService.cpp` - `isValidSequenceNumber()`
   ```cpp
   // Before
   const uint32_t WINDOW = 32; // Accept sequences up to last + WINDOW
   
   // After
   const uint32_t WINDOW = 64; // Accept sequences up to last + WINDOW
   ```

## Memory Impact

### RAM Usage
- **Before**: 3 arrays × 32 entries = 96 entries total
  - `authenticatedNodes`: 32 × 2 bytes = 64 bytes
  - `lastSequenceNumbers`: 32 × 4 bytes = 128 bytes
  - `recentReceiveBitmap`: 32 × 4 bytes = 128 bytes
  - **Total**: 320 bytes

- **After**: 3 arrays × 64 entries = 192 entries total
  - `authenticatedNodes`: 64 × 2 bytes = 128 bytes
  - `lastSequenceNumbers`: 64 × 4 bytes = 256 bytes
  - `recentReceiveBitmap`: 64 × 4 bytes = 256 bytes
  - **Total**: 640 bytes

- **Increase**: 320 bytes (acceptable for ESP32-C3 with 320KB RAM)

### Flash Usage
Minimal increase due to larger static array declarations and slightly more loop iterations.

## Build Results

Both builds completed successfully:
- **esp32c3-node**: 
  - RAM: 5.6% (18,484 bytes / 327,680 bytes)
  - Flash: 34.2% (448,824 bytes / 1,310,720 bytes)
  
- **esp32c3-bridge**:
  - RAM: 5.5% (18,036 bytes / 327,680 bytes)
  - Flash: 35.3% (462,770 bytes / 1,310,720 bytes)

Memory usage remains well within acceptable limits.

## Security Benefits

1. **Elimination of Attack Vector**: Replay protection can no longer be disabled, removing a potential vulnerability
2. **Larger Network Support**: Can now track up to 64 authenticated nodes instead of 32
3. **Better Out-of-Order Tolerance**: Larger window (64 vs 32) reduces false positives from network delays
4. **Consistent Security**: Replay protection is always enforced regardless of configuration

## Backward Compatibility

### NVS Data
The system will continue to work with existing NVS data:
- `loadPeerTableFromNVS()` safely handles peer tables with fewer than 64 entries
- No migration needed for existing deployments

### Configuration
- The `enableReplayProtection` flag remains in `MeshSecurityConfig` structure
- Existing code that sets this flag will continue to compile
- The flag is simply ignored at runtime (always treated as `true`)

## Testing Recommendations

1. **Network Scale Testing**: Test with more than 32 nodes to verify tracking works correctly
2. **Out-of-Order Packets**: Verify packets arriving out of order within window are accepted
3. **Replay Attack**: Verify duplicate packets are still rejected
4. **Memory Monitoring**: Monitor RAM usage in production to ensure stability
5. **NVS Persistence**: Verify peer table saves/loads correctly with larger capacity

## Related Documentation

- Original analysis: `COMPLETE_SUCCESS.md` (Replay Attack Analysis section)
- Routing improvements: `LINK_QUALITY_IMPLEMENTATION_SUMMARY.md`
- Security overview: `CONFIG.md`

## Summary

These improvements strengthen the security posture by:
1. Making replay protection mandatory (cannot be bypassed)
2. Supporting larger networks (32→64 nodes)
3. Better handling of network delays (32→64 window size)

All changes maintain backward compatibility and use minimal additional memory resources.
