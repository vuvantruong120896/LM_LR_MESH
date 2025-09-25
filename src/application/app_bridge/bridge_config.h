#ifndef _BRIDGE_CONFIG_H
#define _BRIDGE_CONFIG_H

// Hardware pin definitions for Bridge/Gateway
#define BOARD_LED   2       // Different LED pin for bridge
#define LED_ON      HIGH    // Different LED logic
#define LED_OFF     LOW

// LoRa configuration for bridge
#define LORA_CS     5
#define LORA_RST    14
#define LORA_IRQ    2
#define LORA_IO1    15

// Bridge-specific settings
#define BRIDGE_ID               0x2001  // Unique bridge identifier
#define WIFI_CONNECT_TIMEOUT    10000   // 10 second WiFi timeout
#define MQTT_RECONNECT_INTERVAL 30000   // 30 second MQTT reconnect

// WiFi credentials (should be moved to separate config)
#define WIFI_SSID       "YourWiFiSSID"
#define WIFI_PASSWORD   "YourWiFiPassword"

// MQTT settings
#define MQTT_SERVER     "mqtt.broker.address"
#define MQTT_PORT       1883
#define MQTT_USER       "mqtt_user"
#define MQTT_PASSWORD   "mqtt_pass"
#define MQTT_TOPIC_BASE "loramesher/bridge"

// LoRa module type (might be different for bridge)
#define LORA_MODULE     LoraMesher::LoraModules::SX1276_MOD

#endif // _BRIDGE_CONFIG_H