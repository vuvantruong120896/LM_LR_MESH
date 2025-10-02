#ifndef _NVS_STORAGE_SERVICE_H
#define _NVS_STORAGE_SERVICE_H

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_log.h"

// NVS namespace constants
#define NVS_NAMESPACE_MESH "mesh_config"
#define NVS_NAMESPACE_DEVICES "mesh_devices"
#define NVS_NAMESPACE_ADDRESS "mesh_address"

// NVS key constants
#define NVS_KEY_NETWORK_KEY "net_key"
#define NVS_KEY_AUTH_TOKEN "auth_token"
#define NVS_KEY_NETWORK_ID "net_id"
#define NVS_KEY_KEY_VERSION "key_version"
#define NVS_KEY_BRIDGE_INITIALIZED "bridge_init"
#define NVS_KEY_DEVICE_COUNT "dev_count"
#define NVS_KEY_NEXT_ADDRESS "next_addr"
#define NVS_KEY_DEVICE_PREFIX "dev_"
#define NVS_KEY_GATEWAY_ADDR "gw_addr"
#define NVS_KEY_GATEWAY_HOPS "gw_hops"
#define NVS_KEY_GATEWAY_TIME "gw_time"
#define NVS_KEY_ROUTING_COUNT "rt_count"
#define NVS_KEY_ROUTING_PREFIX "rt_"

// Maximum values
#define MAX_PROVISIONED_DEVICES 64
#define MIN_NODE_ADDRESS 0x0002
#define MAX_NODE_ADDRESS 0xFFFF
#define BRIDGE_ADDRESS 0x0001

/**
 * @brief Network configuration structure for NVS storage
 */
struct NetworkConfig {
    uint8_t networkKey[16];        // 128-bit network key
    uint8_t authToken[8];          // Authentication token
    uint16_t networkId;            // Network identifier
    uint8_t keyVersion;            // Key version for rotation
    uint32_t timestamp;            // Configuration timestamp
    bool initialized;              // Configuration validity flag
};

/**
 * @brief Provisioned device information for NVS storage
 */
struct ProvisionedDevice {
    uint16_t address;              // Assigned unicast address
    uint8_t deviceUUID[16];        // Device unique identifier
    uint32_t provisionTime;        // Provisioning timestamp
    uint8_t deviceType;            // Device type/capability flags
    uint8_t status;                // Device status (active, inactive, revoked)
    char deviceName[32];           // Human-readable device name
    uint32_t lastSeen;             // Last communication timestamp
};

/**
 * @brief Device status constants
 */
enum DeviceStatus : uint8_t {
    DEVICE_STATUS_ACTIVE = 0x01,
    DEVICE_STATUS_INACTIVE = 0x02,
    DEVICE_STATUS_REVOKED = 0x03,
    DEVICE_STATUS_PENDING = 0x04
};

/**
 * @brief Address pool information for NVS storage
 */
struct AddressPool {
    uint16_t nextAvailable;        // Next available address to assign
    uint16_t totalAssigned;        // Total addresses assigned
    uint16_t maxDevices;           // Maximum devices supported
    uint32_t lastUpdate;           // Last pool update timestamp
};

/**
 * @brief Gateway information for NVS storage
 */
struct GatewayInfo {
    uint16_t address;              // Gateway mesh address
    uint8_t hopCount;              // Number of hops to gateway
    uint32_t lastSeen;             // Last time gateway was seen
    bool isValid;                  // Gateway info validity flag
};

/**
 * @brief Routing table entry for NVS storage
 */
struct RouteEntry {
    uint16_t address;              // Node address
    uint16_t via;                  // Next hop address
    uint8_t metric;                // Hop count
    uint8_t role;                  // Node role flags
    uint16_t networkId;            // Network ID of the node (0 = unknown/any)
    uint32_t lastSeen;             // Last time entry was updated
    bool isValid;                  // Entry validity flag
};

/**
 * @brief NVS Storage Service for mesh network persistent data
 * 
 * Manages persistent storage of network configuration, provisioned devices,
 * and address allocation using ESP32 NVS (Non-Volatile Storage).
 */
class NVSStorageService {
public:
    /**
     * @brief Initialize NVS storage system
     * @return true if initialization successful
     */
    static bool initialize();
    
    /**
     * @brief Check if NVS system is initialized
     * @return true if initialized
     */
    static bool isInitialized();
    
    // Network Configuration Management
    
    /**
     * @brief Save network configuration to NVS
     * @param config Network configuration to save
     * @return true if save successful
     */
    static bool saveNetworkConfig(const NetworkConfig& config);
    
    /**
     * @brief Load network configuration from NVS
     * @param config Output buffer for loaded configuration
     * @return true if load successful
     */
    static bool loadNetworkConfig(NetworkConfig& config);
    
    /**
     * @brief Check if network is configured (has valid netkey)
     * @return true if network is configured
     */
    static bool isNetworkConfigured();
    
    /**
     * @brief Clear network configuration from NVS
     * @return true if clear successful
     */
    static bool clearNetworkConfig();
    
    // Provisioned Device Management
    
    /**
     * @brief Save provisioned device to NVS
     * @param device Device information to save
     * @return true if save successful
     */
    static bool saveProvisionedDevice(const ProvisionedDevice& device);
    
    /**
     * @brief Load provisioned device by address
     * @param address Device address to load
     * @param device Output buffer for device information
     * @return true if device found and loaded
     */
    static bool loadProvisionedDevice(uint16_t address, ProvisionedDevice& device);
    
    /**
     * @brief Check if device is provisioned
     * @param address Device address to check
     * @return true if device is provisioned
     */
    static bool isDeviceProvisioned(uint16_t address);
    
    /**
     * @brief Get count of provisioned devices
     * @return Number of provisioned devices
     */
    static uint16_t getProvisionedDeviceCount();
    
    /**
     * @brief Load all provisioned devices
     * @param devices Output array for device list
     * @param maxDevices Maximum devices to load
     * @return Number of devices loaded
     */
    static uint16_t loadAllProvisionedDevices(ProvisionedDevice* devices, uint16_t maxDevices);
    
    /**
     * @brief Update device status
     * @param address Device address
     * @param status New device status
     * @return true if update successful
     */
    static bool updateDeviceStatus(uint16_t address, DeviceStatus status);
    
    /**
     * @brief Remove provisioned device
     * @param address Device address to remove
     * @return true if removal successful
     */
    static bool removeProvisionedDevice(uint16_t address);
    
    // Address Pool Management
    
    /**
     * @brief Initialize address pool
     * @return true if initialization successful
     */
    static bool initializeAddressPool();
    
    /**
     * @brief Allocate next available address
     * @return Next available address, or 0 if pool exhausted
     */
    static uint16_t allocateAddress();
    
    /**
     * @brief Release allocated address back to pool
     * @param address Address to release
     * @return true if release successful
     */
    static bool releaseAddress(uint16_t address);
    
    /**
     * @brief Check if address is available
     * @param address Address to check
     * @return true if address is available
     */
    static bool isAddressAvailable(uint16_t address);
    
    /**
     * @brief Get address pool statistics
     * @param pool Output buffer for pool information
     * @return true if statistics retrieved successfully
     */
    static bool getAddressPoolInfo(AddressPool& pool);
    
    // Gateway and Routing Table Management
    
    /**
     * @brief Save gateway information to NVS
     * @param gateway Gateway information to save
     * @return true if save successful
     */
    static bool saveGatewayInfo(const GatewayInfo& gateway);
    
    /**
     * @brief Load gateway information from NVS
     * @param gateway Output buffer for gateway information
     * @return true if load successful
     */
    static bool loadGatewayInfo(GatewayInfo& gateway);
    
    /**
     * @brief Check if gateway information exists in NVS
     * @return true if gateway info exists
     */
    static bool hasGatewayInfo();
    
    /**
     * @brief Clear gateway information from NVS
     * @return true if clear successful
     */
    static bool clearGatewayInfo();
    
    /**
     * @brief Save routing table entry to NVS
     * @param entry Routing entry to save
     * @return true if save successful
     */
    static bool saveRouteEntry(const RouteEntry& entry);
    
    /**
     * @brief Load routing table entry by address
     * @param address Node address
     * @param entry Output buffer for routing entry
     * @return true if entry found and loaded
     */
    static bool loadRouteEntry(uint16_t address, RouteEntry& entry);
    
    /**
     * @brief Save multiple routing table entries
     * @param entries Array of routing entries
     * @param count Number of entries
     * @return true if save successful
     */
    static bool saveRoutingTable(const RouteEntry* entries, uint16_t count);
    
    /**
     * @brief Load all routing table entries
     * @param entries Output array for routing entries
     * @param maxEntries Maximum entries to load
     * @return Number of entries loaded
     */
    static uint16_t loadRoutingTable(RouteEntry* entries, uint16_t maxEntries);
    
    /**
     * @brief Clear all routing table entries from NVS
     * @return true if clear successful
     */
    static bool clearRoutingTable();
    
    // Utility Functions
    
    /**
     * @brief Format/erase all mesh-related NVS data
     * @return true if format successful
     */
    static bool formatStorage();
    
    /**
     * @brief Get NVS storage statistics
     * @param usedEntries Output for used entries count
     * @param totalEntries Output for total entries count
     * @return true if statistics retrieved
     */
    static bool getStorageStats(size_t* usedEntries, size_t* totalEntries);
    
    /**
     * @brief Backup configuration to JSON string (for debugging)
     * @param jsonOutput Output buffer for JSON string
     * @param maxSize Maximum output buffer size
     * @return true if backup successful
     */
    static bool backupToJSON(char* jsonOutput, size_t maxSize);

private:
    static bool nvs_initialized;
    static const char* TAG;
    
    /**
     * @brief Open NVS handle for specific namespace
     * @param namespace_name NVS namespace name
     * @param handle Output handle
     * @param open_mode NVS open mode (read/write)
     * @return ESP_OK if successful
     */
    static esp_err_t openNVSHandle(const char* namespace_name, nvs_handle_t* handle, nvs_open_mode_t open_mode);
    
    /**
     * @brief Close NVS handle
     * @param handle Handle to close
     */
    static void closeNVSHandle(nvs_handle_t handle);
    
    /**
     * @brief Generate device key for NVS storage
     * @param address Device address
     * @param keyBuffer Output buffer for key string
     * @param maxSize Maximum key buffer size
     */
    static void generateDeviceKey(uint16_t address, char* keyBuffer, size_t maxSize);
    
    /**
     * @brief Validate network configuration
     * @param config Configuration to validate
     * @return true if configuration is valid
     */
    static bool validateNetworkConfig(const NetworkConfig& config);
    
    /**
     * @brief Validate provisioned device data
     * @param device Device data to validate
     * @return true if device data is valid
     */
    static bool validateDeviceData(const ProvisionedDevice& device);
};

#endif // _NVS_STORAGE_SERVICE_H