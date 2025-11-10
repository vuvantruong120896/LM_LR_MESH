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
#define LORA_IRQ    6
#define LORA_IO1    -1

// Gateway-specific settings
#define GATEWAY_ID              0x01    // Unique gateway identifier

// WiFi configuration (store in NVS for production)
// WARNING: These are default values for testing only!
// In production, use NVS storage or environment variables
#ifndef WIFI_SSID
#define WIFI_SSID               "OXII"      // Change this!
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD           "sharitek-nerd-2019"  // Change this!
#endif

// Firebase configuration (store in NVS for production)
// Get these from Firebase Console: https://console.firebase.google.com/
#ifndef FIREBASE_HOST
#define FIREBASE_HOST           "https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/:null"  // Change this!
#endif
#ifndef FIREBASE_AUTH
#define FIREBASE_AUTH           "0kMDkyCxejcJB350HrFlgBmb3Y5PsOiR90ZXf1MV"    // Change this!
#endif

// Gateway ID for Firebase (based on MAC address)
// Format: GW_<MAC> (e.g., "GW_240AC4123456")
#define FIREBASE_GATEWAY_ID_PREFIX "0x"

// Gateway operation settings
#define GATEWAY_ROUTING_TABLE_INTERVAL 300000  // Backup upload every 5 minutes (real-time upload on changes)
#define GATEWAY_SENSOR_UPLOAD_TIMEOUT  10000    // Timeout for sensor data upload
#define GATEWAY_SENSOR_INTERVAL        600000  // Gateway sensor reading interval: 10 minutes
#define GATEWAY_STATUS_INTERVAL        300000  // Gateway status upload interval: 5 minutes

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