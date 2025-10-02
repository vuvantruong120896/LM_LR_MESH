# LoRaMesh Configuration Guide

## Mode Selection

To switch between Node and Bridge modes, edit the `DEVICE_MODE` value in `platformio.ini`:

### Option 1: Edit platformio.ini directly
Open `platformio.ini` and change the line:
```ini
-D DEVICE_MODE=1    # Node mode (default)
```
to:
```ini
-D DEVICE_MODE=2    # Bridge mode
```

### Option 2: Override via build command
```bash
# Build as Node (default)
pio run

# Build as Bridge
pio run --build-flag="-D DEVICE_MODE=2"

# Upload as Bridge
pio run --target upload --build-flag="-D DEVICE_MODE=2"
```

## Modes

### Node Mode (DEVICE_MODE=1)
- **Function**: Sensor node that sends periodic data packets
- **Features**: 
  - Simulates temperature, humidity, battery readings
  - Sends broadcast packets every 10 seconds
  - LED patterns for status indication
  - Routing table monitoring
- **Dependencies**: Only RadioLib
- **Config file**: `src/application/app_node/node_config.h`

### Bridge Mode (DEVICE_MODE=2)
- **Function**: WiFi/MQTT gateway for mesh network
- **Features**:
  - Connects to WiFi and MQTT broker
  - Forwards mesh packets to MQTT topics
  - Publishes bridge status and statistics
  - Subscribes to downlink commands
  - Advanced LED status patterns
- **Dependencies**: RadioLib + PubSubClient (WiFi/MQTT)
- **Config file**: `src/application/app_bridge/bridge_config.h`

## Configuration Files

### Node Configuration
Edit `src/application/app_node/node_config.h`:
- `NODE_ID` - Unique node identifier
- `SEND_INTERVAL_MS` - Packet transmission interval
- LoRa pin definitions (`LORA_CS`, `LORA_RST`, etc.)

### Bridge Configuration  
Edit `src/application/app_bridge/bridge_config.h`:
- `BRIDGE_ID` - Unique bridge identifier
- WiFi credentials (`WIFI_SSID`, `WIFI_PASSWORD`)
- MQTT settings (`MQTT_SERVER`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASSWORD`)
- `MQTT_TOPIC_BASE` - Base topic for MQTT messages
- LoRa pin definitions

## Quick Start

1. **For Node**: Just build and upload (default mode)
   ```bash
   pio run --target upload
   ```

2. **For Bridge**: 
   - First configure WiFi/MQTT in `bridge_config.h`
   - Then build with bridge mode:
   ```bash
   pio run --target upload --build-flag="-D DEVICE_MODE=2"
   ```

3. **Switch modes**: Change `DEVICE_MODE` in `platformio.ini` and rebuild

## Serial Output

Both modes provide detailed serial output:
- Initialization status
- Network connectivity (Bridge only)
- Packet transmission/reception
- Routing table information
- System statistics (heap, uptime, etc.)

## LED Indicators

- **Startup**: 3 blinks
- **Connected**: Solid on
- **Message received**: Quick flash pattern
- **Error**: Rapid blinking
- **Activity**: Various patterns for different events