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
#define SEND_INTERVAL_MS        20000   // Send packet every 20 seconds
#define NODE_ID                 0x1001  // Unique node identifier
#define ENABLE_SENSOR_SIMULATION true   // Simulate sensor data

// LoRa module type
#define LORA_MODULE             LoraMesher::LoraModules::SX1276_MOD

#endif // _NODE_CONFIG_H