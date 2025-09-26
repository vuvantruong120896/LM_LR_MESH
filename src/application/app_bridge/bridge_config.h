#ifndef _BRIDGE_CONFIG_H
#define _BRIDGE_CONFIG_H

// Hardware pin definitions for Bridge (TTGO T-BEAM v1.1)
#define BOARD_LED   0
#define LED_ON      LOW
#define LED_OFF     HIGH

// LoRa configuration for Bridge
#define LORA_CS     6
#define LORA_RST    4
#define LORA_IRQ    3
#define LORA_IO1    -1

// Bridge-specific settings
#define BRIDGE_ID               0x01    // Unique bridge identifier

// UART configuration for communication with external ESP32
#define UART_NUM                1       // Use UART1 for external communication
#define UART_BAUD_RATE          115200  // Baud rate for UART communication
#define UART_TX_PIN             17      // GPIO pin for UART TX
#define UART_RX_PIN             16      // GPIO pin for UART RX
#define UART_BUFFER_SIZE        1024    // UART buffer size in bytes

// UART protocol settings
#define UART_PACKET_START_BYTE  0xAA    // Start delimiter for UART packets
#define UART_PACKET_END_BYTE    0x55    // End delimiter for UART packets
#define UART_MAX_PAYLOAD_SIZE   200     // Maximum payload size for UART packets
#define UART_TIMEOUT_MS         1000    // Timeout for UART operations

// Bridge operation settings
#define BRIDGE_STATUS_INTERVAL  30000   // Send bridge status every 30 seconds
#define BRIDGE_HEARTBEAT_INTERVAL 5000  // Send heartbeat every 5 seconds

// LoRa module type
#define LORA_MODULE     LoraMesher::LoraModules::SX1276_MOD

// Mesh security configuration for Bridge
// ENABLE_MESH_SECURITY is now defined in platformio.ini
#define BRIDGE_SECURITY_LEVEL   2       // Security level: 0=none, 1=auth, 2=encrypt+auth
#define BRIDGE_IS_GATEWAY       true    // Bridge can accept join requests
#define MAX_AUTHENTICATED_NODES 32      // Maximum authenticated nodes to track

// Network security keys (these should be configured per deployment)  
// WARNING: These are example keys - CHANGE THEM for production!
#define MESH_NETWORK_KEY        {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, \
                                 0xab, 0xf7, 0x97, 0x75, 0x46, 0xcf, 0x26, 0xa8}
#define MESH_AUTH_TOKEN         {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}

#endif // _BRIDGE_CONFIG_H