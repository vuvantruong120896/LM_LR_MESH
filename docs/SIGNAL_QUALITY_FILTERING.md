# Signal Quality Filtering for LoRa Mesh Network

## Overview

Signal quality filtering has been implemented to improve mesh network stability by rejecting direct neighbors (1-hop routes) with poor signal quality. This prevents unstable links that cause packet loss and network degradation.

## Implementation Details

### Signal Quality Thresholds

**Constants defined in `BuildOptions.h`:**
- `RSSI_MIN_THRESHOLD = -100 dBm` 
- `SNR_MIN_THRESHOLD = -5 dB`

### Quality Guidelines

**RSSI (Received Signal Strength Indicator):**
- `> -80 dBm`: Excellent (close proximity, excellent quality)
- `-80 to -100 dBm`: Good (acceptable for routing)
- `-100 to -120 dBm`: Poor (temporary use, but unstable)  
- `< -120 dBm`: Very Poor (should be avoided)

**SNR (Signal to Noise Ratio):**
- `> 5 dB`: Excellent
- `0 to 5 dB`: Good
- `-5 to 0 dB`: Fair (acceptable)
- `-10 to -5 dB`: Poor
- `< -10 dB`: Very Poor

### Filtering Logic

1. **Applied to**: Direct neighbors only (1-hop routes from Hello packets)
2. **Location**: `RoutingTableService::processRoute(RoutePacket* p, int8_t receivedSNR, int8_t receivedRSSI)`
3. **Behavior**: If either RSSI or SNR is below threshold, the entire Hello packet is rejected
4. **Multi-hop routes**: Not affected (rely on direct neighbor quality assessment)

### Code Changes

**Modified Files:**
- `src/components/lora_mesh_manager/src/core/BuildOptions.h` - Added threshold constants
- `src/components/lora_mesh_manager/src/services/RoutingTableService.h` - Updated function signature
- `src/components/lora_mesh_manager/src/services/RoutingTableService.cpp` - Added filtering logic
- `src/components/lora_mesh_manager/src/core/LoraMesher.cpp` - Pass RSSI parameter

## Benefits

1. **Network Stability**: Prevents weak links that cause frequent retransmissions
2. **Reduced Packet Loss**: Only accepts neighbors with reliable connectivity
3. **Better Route Selection**: Forces routing through more stable intermediate nodes
4. **Lower Latency**: Eliminates routes prone to transmission failures

## Tuning Recommendations

### Conservative (High Reliability)
```cpp
#define RSSI_MIN_THRESHOLD -90   // Stricter
#define SNR_MIN_THRESHOLD 0      // Stricter
```

### Balanced (Default)
```cpp
#define RSSI_MIN_THRESHOLD -100  // Current setting
#define SNR_MIN_THRESHOLD -5     // Current setting  
```

### Permissive (Maximum Connectivity)
```cpp
#define RSSI_MIN_THRESHOLD -110  // More lenient
#define SNR_MIN_THRESHOLD -10    // More lenient
```

## Monitoring

**Log Messages:**
- Normal: `Route packet from XXXX with size N, Network ID: 0xXXXX, RSSI: -XX, SNR: X`
- Filtered: `Rejected direct route to 0xXXXX due to poor signal quality - RSSI: -XX < -100, SNR: X < -5`

## Considerations

1. **Environment Adaptation**: Thresholds may need adjustment based on deployment environment
2. **Network Density**: In sparse networks, may need more permissive thresholds
3. **Antenna Design**: Better antennas may allow stricter thresholds
4. **Power Settings**: Higher transmission power may improve signal quality
5. **Frequency Band**: Different bands have different propagation characteristics

## Testing Recommendations

1. **Range Testing**: Verify connectivity at expected operational distances
2. **Obstruction Testing**: Test with physical barriers (walls, vegetation)
3. **Network Convergence**: Ensure network still forms with realistic node distribution
4. **Edge Case Testing**: Test with nodes at threshold boundaries