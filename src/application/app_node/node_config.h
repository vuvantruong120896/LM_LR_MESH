#ifndef _NODE_CONFIG_H
#define _NODE_CONFIG_H

// Hardware pin definitions for Node (TTGO T-BEAM v1.1)
#define BOARD_LED   0
#define LED_ON      LOW
#define LED_OFF     HIGH

// SPI pin configuration for Node (ESP32-C3)
#ifndef SPI_SCK
#define SPI_SCK     9
#endif
#ifndef SPI_MISO
#define SPI_MISO    8
#endif
#ifndef SPI_MOSI
#define SPI_MOSI    7
#endif
#ifndef SPI_CS
#define SPI_CS      6
#endif

// LoRa configuration for node
#define LORA_CS     6
#define LORA_RST    4
#define LORA_IRQ    3
#define LORA_IO1    -1

// Node behavior settings
#define SEND_INTERVAL_MS        40000   // Send packet every 40 seconds
#define NODE_ID                 0x1000  // Unique node identifier
#define ENABLE_SENSOR_SIMULATION true   // Simulate sensor data

// LoRa module type
#define LORA_MODULE             LoraMesher::LoraModules::SX1276_MOD

// Mesh security configuration for Node
// ENABLE_MESH_SECURITY is now defined in platformio.ini
#define NODE_SECURITY_LEVEL     2       // Security level: 0=none, 1=auth, 2=encrypt+auth
#define AUTO_JOIN_NETWORK       true    // Automatically attempt to join secure network
#define MAX_JOIN_ATTEMPTS       3       // Maximum join attempts before fallback

// Network security keys - now centralized in mesh_security_keys.h
// All nodes MUST use the same keys for authentication to work!
// Keys are now defined in: src/components/lora_mesh_manager/include/mesh_security_keys.h
// #define MESH_NETWORK_KEY and MESH_AUTH_TOKEN are handled by mesh_security_config.h

// Provisioning configuration
#define PROVISION_RETRY_INTERVAL_MS           30000   // 30 seconds between provision attempts
#define PROVISION_TIMEOUT_MS                  10000   // 10 seconds timeout for provision response
#define PROVISION_FAILURE_RETRY_INTERVAL_MS   120000  // 2 minutes wait after failure
#define MAX_PROVISION_RETRIES                 5       // Maximum retry attempts before failure
#define BROADCAST_ADDR                        0xFFFF  // Broadcast address

#endif // _NODE_CONFIG_H