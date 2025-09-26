#ifndef _NODE_CONFIG_H
#define _NODE_CONFIG_H

// Hardware pin definitions for Node (TTGO T-BEAM v1.1)
#define BOARD_LED   0
#define LED_ON      LOW
#define LED_OFF     HIGH

// LoRa configuration for node
#define LORA_CS     6
#define LORA_RST    4
#define LORA_IRQ    3
#define LORA_IO1    -1

// Node behavior settings
#define SEND_INTERVAL_MS        30000   // Send packet every 20 seconds
#define NODE_ID                 0x1000  // Unique node identifier
#define ENABLE_SENSOR_SIMULATION true   // Simulate sensor data

// LoRa module type
#define LORA_MODULE             LoraMesher::LoraModules::SX1276_MOD

// Mesh security configuration for Node
// ENABLE_MESH_SECURITY is now defined in platformio.ini
#define NODE_SECURITY_LEVEL     2       // Security level: 0=none, 1=auth, 2=encrypt+auth
#define AUTO_JOIN_NETWORK       true    // Automatically attempt to join secure network
#define MAX_JOIN_ATTEMPTS       3       // Maximum join attempts before fallback

// Network security keys (these should be configured per deployment)
// WARNING: These are example keys - CHANGE THEM for production!
#define MESH_NETWORK_KEY        {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, \
                                 0xab, 0xf7, 0x97, 0x75, 0x46, 0xcf, 0x26, 0xa8}
#define MESH_AUTH_TOKEN         {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}

#endif // _NODE_CONFIG_H