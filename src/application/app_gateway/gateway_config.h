#ifndef _GATEWAY_CONFIG_H
#define _GATEWAY_CONFIG_H

// Hardware pin definitions for Gateway (ESP32 DOIT DevKit V1)
#define BOARD_LED   0
#define LED_ON      LOW
#define LED_OFF     HIGH

// SPI pin configuration for Gateway (ESP32)
#ifndef SPI_SCK
#define SPI_SCK     18
#endif
#ifndef SPI_MISO
#define SPI_MISO    16
#endif
#ifndef SPI_MOSI
#define SPI_MOSI    19
#endif
#ifndef SPI_CS
#define SPI_CS      5
#endif

// LoRa configuration for Gateway
#define LORA_CS     5
#define LORA_RST    4
#define LORA_IRQ    15
#define LORA_IO1    -1

// Gateway-specific settings
#define GATEWAY_ID              0x01    // Unique gateway identifier

// UART configuration for communication with external ESP32
#define UART_NUM                1       // Use UART1 for external communication
#define UART_BAUD_RATE          115200  // Baud rate for UART communication
#define UART_TX_PIN             21      // GPIO pin for UART TX
#define UART_RX_PIN             20      // GPIO pin for UART RX
#define UART_BUFFER_SIZE        1024    // UART buffer size in bytes

#define UART_PACKET_START_BYTE1 0x4C    // Start delimiter byte 1 for UART packets ('L')
#define UART_PACKET_START_BYTE2 0x4D    // Start delimiter byte 2 for UART packets ('M')
#define UART_PACKET_END_BYTE    0x55    // End delimiter for UART packets
#define UART_MAX_PAYLOAD_SIZE   200     // Maximum payload size for UART packets
#define UART_TIMEOUT_MS         1000    // Timeout for UART operations

// Gateway operation settings
#define GATEWAY_STATUS_INTERVAL  30000   // Send gateway status every 30 seconds
#define GATEWAY_HEARTBEAT_INTERVAL 5000  // Send heartbeat every 5 seconds

// LoRa module type
#define LORA_MODULE     LoraMesher::LoraModules::SX1276_MOD

// Mesh security configuration for Gateway
// ENABLE_MESH_SECURITY is now defined in platformio.ini
#define GATEWAY_SECURITY_LEVEL   2       // Security level: 0=none, 1=auth, 2=encrypt+auth
#define GATEWAY_IS_GATEWAY       true    // Gateway can accept join requests
#define MAX_AUTHENTICATED_NODES 64      // Maximum authenticated nodes to track (increased from 32 for better security)

// Network security keys - now centralized in mesh_security_keys.h
// All devices MUST use the same keys for authentication to work!
// Keys are now defined in: src/components/lora_mesh_manager/include/mesh_security_keys.h
// #define MESH_NETWORK_KEY and MESH_AUTH_TOKEN are handled by mesh_security_config.h

#endif // _GATEWAY_CONFIG_H