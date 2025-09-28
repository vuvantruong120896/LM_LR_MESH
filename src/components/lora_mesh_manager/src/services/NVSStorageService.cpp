#include "NVSStorageService.h"
#include <cstring>
#include <esp_system.h>
#include <esp_timer.h>

// Static member initialization
bool NVSStorageService::nvs_initialized = false;
const char* NVSStorageService::TAG = "NVSStorage";

bool NVSStorageService::initialize() {
    if (nvs_initialized) {
        ESP_LOGI(TAG, "NVS already initialized");
        return true;
    }
    
    // Initialize NVS flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition needs erasing, performing erase...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS flash: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "NVS flash initialized successfully");
    nvs_initialized = true;
    
    // Initialize address pool if not exists
    if (!initializeAddressPool()) {
        ESP_LOGW(TAG, "Failed to initialize address pool");
    }
    
    return true;
}

bool NVSStorageService::isInitialized() {
    return nvs_initialized;
}

// Network Configuration Management

bool NVSStorageService::saveNetworkConfig(const NetworkConfig& config) {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    if (!validateNetworkConfig(config)) {
        ESP_LOGE(TAG, "Invalid network configuration");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_MESH, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    bool success = true;
    
    // Save network key
    err = nvs_set_blob(handle, NVS_KEY_NETWORK_KEY, config.networkKey, sizeof(config.networkKey));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save network key: %s", esp_err_to_name(err));
        success = false;
    }
    
    // Save auth token
    if (success) {
        err = nvs_set_blob(handle, NVS_KEY_AUTH_TOKEN, config.authToken, sizeof(config.authToken));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save auth token: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save network ID
    if (success) {
        err = nvs_set_u16(handle, NVS_KEY_NETWORK_ID, config.networkId);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save network ID: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save key version
    if (success) {
        err = nvs_set_u8(handle, NVS_KEY_KEY_VERSION, config.keyVersion);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save key version: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save initialization flag
    if (success) {
        err = nvs_set_u8(handle, NVS_KEY_BRIDGE_INITIALIZED, config.initialized ? 1 : 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save initialization flag: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Save complete configuration as blob for faster loading
    if (success) {
        err = nvs_set_blob(handle, "net_config", &config, sizeof(NetworkConfig));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save complete config: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    if (success) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    closeNVSHandle(handle);
    
    if (success) {
        ESP_LOGI(TAG, "Network configuration saved successfully");
    }
    
    return success;
}

bool NVSStorageService::loadNetworkConfig(NetworkConfig& config) {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_MESH, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS handle for reading: %s", esp_err_to_name(err));
        return false;
    }
    
    // Try to load complete configuration first
    size_t required_size = sizeof(NetworkConfig);
    err = nvs_get_blob(handle, "net_config", &config, &required_size);
    
    if (err == ESP_OK && required_size == sizeof(NetworkConfig)) {
        closeNVSHandle(handle);
        
        if (validateNetworkConfig(config)) {
            ESP_LOGI(TAG, "Network configuration loaded successfully (blob)");
            return true;
        } else {
            ESP_LOGW(TAG, "Loaded configuration is invalid, trying individual keys");
        }
    }
    
    // Fallback to loading individual keys
    memset(&config, 0, sizeof(NetworkConfig));
    bool success = true;
    
    // Load network key
    size_t key_size = sizeof(config.networkKey);
    err = nvs_get_blob(handle, NVS_KEY_NETWORK_KEY, config.networkKey, &key_size);
    if (err != ESP_OK || key_size != sizeof(config.networkKey)) {
        ESP_LOGW(TAG, "Failed to load network key: %s", esp_err_to_name(err));
        success = false;
    }
    
    // Load auth token
    if (success) {
        size_t token_size = sizeof(config.authToken);
        err = nvs_get_blob(handle, NVS_KEY_AUTH_TOKEN, config.authToken, &token_size);
        if (err != ESP_OK || token_size != sizeof(config.authToken)) {
            ESP_LOGW(TAG, "Failed to load auth token: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Load network ID
    if (success) {
        err = nvs_get_u16(handle, NVS_KEY_NETWORK_ID, &config.networkId);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load network ID: %s", esp_err_to_name(err));
            success = false;
        }
    }
    
    // Load key version
    if (success) {
        err = nvs_get_u8(handle, NVS_KEY_KEY_VERSION, &config.keyVersion);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load key version, using default");
            config.keyVersion = 1;
        }
    }
    
    // Load initialization flag
    if (success) {
        uint8_t init_flag;
        err = nvs_get_u8(handle, NVS_KEY_BRIDGE_INITIALIZED, &init_flag);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load initialization flag, assuming false");
            config.initialized = false;
        } else {
            config.initialized = (init_flag != 0);
        }
    }
    
    closeNVSHandle(handle);
    
    if (success) {
        // Set current timestamp
        config.timestamp = esp_timer_get_time() / 1000000;
        ESP_LOGI(TAG, "Network configuration loaded successfully (individual keys)");
    }
    
    return success;
}

bool NVSStorageService::isNetworkConfigured() {
    if (!nvs_initialized) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_MESH, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        return false;
    }
    
    uint8_t init_flag;
    err = nvs_get_u8(handle, NVS_KEY_BRIDGE_INITIALIZED, &init_flag);
    closeNVSHandle(handle);
    
    return (err == ESP_OK && init_flag != 0);
}

bool NVSStorageService::clearNetworkConfig() {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_MESH, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    // Erase all mesh configuration keys
    nvs_erase_key(handle, NVS_KEY_NETWORK_KEY);
    nvs_erase_key(handle, NVS_KEY_AUTH_TOKEN);
    nvs_erase_key(handle, NVS_KEY_NETWORK_ID);
    nvs_erase_key(handle, NVS_KEY_KEY_VERSION);
    nvs_erase_key(handle, NVS_KEY_BRIDGE_INITIALIZED);
    nvs_erase_key(handle, "net_config");
    
    err = nvs_commit(handle);
    closeNVSHandle(handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Network configuration cleared successfully");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS clear: %s", esp_err_to_name(err));
        return false;
    }
}

// Provisioned Device Management

bool NVSStorageService::saveProvisionedDevice(const ProvisionedDevice& device) {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    if (!validateDeviceData(device)) {
        ESP_LOGE(TAG, "Invalid device data");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_DEVICES, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open devices NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    char deviceKey[16];
    generateDeviceKey(device.address, deviceKey, sizeof(deviceKey));
    
    // Save complete device structure
    err = nvs_set_blob(handle, deviceKey, &device, sizeof(ProvisionedDevice));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save device data: %s", esp_err_to_name(err));
        closeNVSHandle(handle);
        return false;
    }
    
    // Update device count
    uint16_t deviceCount = getProvisionedDeviceCount();
    bool isNewDevice = !isDeviceProvisioned(device.address);
    
    if (isNewDevice) {
        deviceCount++;
        err = nvs_set_u16(handle, NVS_KEY_DEVICE_COUNT, deviceCount);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to update device count: %s", esp_err_to_name(err));
        }
    }
    
    err = nvs_commit(handle);
    closeNVSHandle(handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Device 0x%04X %s successfully", device.address, isNewDevice ? "provisioned" : "updated");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to commit device data: %s", esp_err_to_name(err));
        return false;
    }
}

bool NVSStorageService::loadProvisionedDevice(uint16_t address, ProvisionedDevice& device) {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_DEVICES, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        return false;
    }
    
    char deviceKey[16];
    generateDeviceKey(address, deviceKey, sizeof(deviceKey));
    
    size_t required_size = sizeof(ProvisionedDevice);
    err = nvs_get_blob(handle, deviceKey, &device, &required_size);
    closeNVSHandle(handle);
    
    if (err == ESP_OK && required_size == sizeof(ProvisionedDevice)) {
        return validateDeviceData(device);
    }
    
    return false;
}

bool NVSStorageService::isDeviceProvisioned(uint16_t address) {
    ProvisionedDevice device;
    return loadProvisionedDevice(address, device);
}

uint16_t NVSStorageService::getProvisionedDeviceCount() {
    if (!nvs_initialized) {
        return 0;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_DEVICES, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        return 0;
    }
    
    uint16_t count = 0;
    err = nvs_get_u16(handle, NVS_KEY_DEVICE_COUNT, &count);
    closeNVSHandle(handle);
    
    return (err == ESP_OK) ? count : 0;
}

uint16_t NVSStorageService::loadAllProvisionedDevices(ProvisionedDevice* devices, uint16_t maxDevices) {
    if (!nvs_initialized || !devices || maxDevices == 0) {
        return 0;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_DEVICES, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        return 0;
    }
    
    uint16_t loadedCount = 0;
    char deviceKey[16];
    
    // Iterate through possible device addresses
    for (uint16_t addr = MIN_NODE_ADDRESS; addr <= MAX_NODE_ADDRESS && loadedCount < maxDevices; addr++) {
        generateDeviceKey(addr, deviceKey, sizeof(deviceKey));
        
        size_t required_size = sizeof(ProvisionedDevice);
        err = nvs_get_blob(handle, deviceKey, &devices[loadedCount], &required_size);
        
        if (err == ESP_OK && required_size == sizeof(ProvisionedDevice)) {
            if (validateDeviceData(devices[loadedCount])) {
                loadedCount++;
            }
        }
    }
    
    closeNVSHandle(handle);
    ESP_LOGI(TAG, "Loaded %d provisioned devices", loadedCount);
    return loadedCount;
}

bool NVSStorageService::updateDeviceStatus(uint16_t address, DeviceStatus status) {
    ProvisionedDevice device;
    if (!loadProvisionedDevice(address, device)) {
        ESP_LOGE(TAG, "Device 0x%04X not found for status update", address);
        return false;
    }
    
    device.status = status;
    device.lastSeen = esp_timer_get_time() / 1000000;
    
    return saveProvisionedDevice(device);
}

bool NVSStorageService::removeProvisionedDevice(uint16_t address) {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    if (!isDeviceProvisioned(address)) {
        ESP_LOGW(TAG, "Device 0x%04X not provisioned", address);
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_DEVICES, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open devices NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    char deviceKey[16];
    generateDeviceKey(address, deviceKey, sizeof(deviceKey));
    
    err = nvs_erase_key(handle, deviceKey);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove device: %s", esp_err_to_name(err));
        closeNVSHandle(handle);
        return false;
    }
    
    // Update device count
    uint16_t deviceCount = getProvisionedDeviceCount();
    if (deviceCount > 0) {
        deviceCount--;
        nvs_set_u16(handle, NVS_KEY_DEVICE_COUNT, deviceCount);
    }
    
    err = nvs_commit(handle);
    closeNVSHandle(handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Device 0x%04X removed successfully", address);
        // Release the address back to pool
        releaseAddress(address);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to commit device removal: %s", esp_err_to_name(err));
        return false;
    }
}

// Address Pool Management

bool NVSStorageService::initializeAddressPool() {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_ADDRESS, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open address NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    // Check if address pool already exists
    uint16_t nextAddr;
    err = nvs_get_u16(handle, NVS_KEY_NEXT_ADDRESS, &nextAddr);
    
    if (err != ESP_OK) {
        // Initialize new address pool
        AddressPool pool = {
            .nextAvailable = MIN_NODE_ADDRESS,
            .totalAssigned = 0,
            .maxDevices = MAX_PROVISIONED_DEVICES,
            .lastUpdate = (uint32_t)(esp_timer_get_time() / 1000000)
        };
        
        err = nvs_set_u16(handle, NVS_KEY_NEXT_ADDRESS, pool.nextAvailable);
        if (err == ESP_OK) {
            err = nvs_set_blob(handle, "addr_pool", &pool, sizeof(AddressPool));
        }
        
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        
        closeNVSHandle(handle);
        
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Address pool initialized successfully");
            return true;
        } else {
            ESP_LOGE(TAG, "Failed to initialize address pool: %s", esp_err_to_name(err));
            return false;
        }
    } else {
        closeNVSHandle(handle);
        ESP_LOGI(TAG, "Address pool already initialized, next address: 0x%04X", nextAddr);
        return true;
    }
}

uint16_t NVSStorageService::allocateAddress() {
    if (!nvs_initialized) {
        return 0;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_ADDRESS, &handle, NVS_READWRITE);
    if (err != ESP_OK) {
        return 0;
    }
    
    uint16_t nextAddr;
    err = nvs_get_u16(handle, NVS_KEY_NEXT_ADDRESS, &nextAddr);
    if (err != ESP_OK) {
        closeNVSHandle(handle);
        return 0;
    }
    
    // Find next available address
    uint16_t candidateAddr = nextAddr;
    uint16_t allocatedAddr = 0;
    
    for (int attempts = 0; attempts < (MAX_NODE_ADDRESS - MIN_NODE_ADDRESS + 1); attempts++) {
        if (candidateAddr > MAX_NODE_ADDRESS) {
            candidateAddr = MIN_NODE_ADDRESS;
        }
        
        if (candidateAddr == BRIDGE_ADDRESS) {
            candidateAddr++;
            continue;
        }
        
        if (!isDeviceProvisioned(candidateAddr)) {
            allocatedAddr = candidateAddr;
            nextAddr = candidateAddr + 1;
            break;
        }
        
        candidateAddr++;
    }
    
    if (allocatedAddr > 0) {
        // Update next available address
        err = nvs_set_u16(handle, NVS_KEY_NEXT_ADDRESS, nextAddr);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update next address: %s", esp_err_to_name(err));
            allocatedAddr = 0;
        } else {
            ESP_LOGI(TAG, "Allocated address 0x%04X", allocatedAddr);
        }
    } else {
        ESP_LOGW(TAG, "Address pool exhausted");
    }
    
    closeNVSHandle(handle);
    return allocatedAddr;
}

bool NVSStorageService::releaseAddress(uint16_t address) {
    if (!nvs_initialized || address == BRIDGE_ADDRESS || address < MIN_NODE_ADDRESS) {
        return false;
    }
    
    // For now, just log the release. In a more sophisticated implementation,
    // we could maintain a bitmap of available addresses
    ESP_LOGI(TAG, "Released address 0x%04X", address);
    return true;
}

bool NVSStorageService::isAddressAvailable(uint16_t address) {
    if (address == BRIDGE_ADDRESS || address < MIN_NODE_ADDRESS || address > MAX_NODE_ADDRESS) {
        return false;
    }
    
    return !isDeviceProvisioned(address);
}

bool NVSStorageService::getAddressPoolInfo(AddressPool& pool) {
    if (!nvs_initialized) {
        return false;
    }
    
    nvs_handle_t handle;
    esp_err_t err = openNVSHandle(NVS_NAMESPACE_ADDRESS, &handle, NVS_READONLY);
    if (err != ESP_OK) {
        return false;
    }
    
    size_t required_size = sizeof(AddressPool);
    err = nvs_get_blob(handle, "addr_pool", &pool, &required_size);
    
    if (err != ESP_OK) {
        // Fallback: construct pool info from current state
        uint16_t nextAddr;
        err = nvs_get_u16(handle, NVS_KEY_NEXT_ADDRESS, &nextAddr);
        
        if (err == ESP_OK) {
            pool.nextAvailable = nextAddr;
            pool.totalAssigned = getProvisionedDeviceCount();
            pool.maxDevices = MAX_PROVISIONED_DEVICES;
            pool.lastUpdate = esp_timer_get_time() / 1000000;
        }
    }
    
    closeNVSHandle(handle);
    return (err == ESP_OK);
}

// Utility Functions

bool NVSStorageService::formatStorage() {
    if (!nvs_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return false;
    }
    
    ESP_LOGW(TAG, "Formatting mesh storage - all data will be lost!");
    
    // Erase all mesh-related namespaces
    esp_err_t err1 = nvs_flash_erase_partition(NVS_DEFAULT_PART_NAME);
    
    // Reinitialize NVS
    esp_err_t err2 = nvs_flash_init();
    
    if (err1 == ESP_OK && err2 == ESP_OK) {
        ESP_LOGI(TAG, "Storage formatted successfully");
        // Reinitialize address pool
        initializeAddressPool();
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to format storage: erase=%s, init=%s", 
                esp_err_to_name(err1), esp_err_to_name(err2));
        return false;
    }
}

bool NVSStorageService::getStorageStats(size_t* usedEntries, size_t* totalEntries) {
    if (!nvs_initialized || !usedEntries || !totalEntries) {
        return false;
    }
    
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &nvs_stats);
    if (err == ESP_OK) {
        *usedEntries = nvs_stats.used_entries;
        *totalEntries = nvs_stats.total_entries;
        return true;
    }
    
    return false;
}

bool NVSStorageService::backupToJSON(char* jsonOutput, size_t maxSize) {
    if (!nvs_initialized || !jsonOutput || maxSize < 100) {
        return false;
    }
    
    NetworkConfig config;
    int pos = 0;
    
    pos += snprintf(jsonOutput + pos, maxSize - pos, "{\"network\":{");
    
    if (loadNetworkConfig(config)) {
        pos += snprintf(jsonOutput + pos, maxSize - pos, 
                       "\"configured\":true,\"networkId\":%u,\"keyVersion\":%u",
                       config.networkId, config.keyVersion);
    } else {
        pos += snprintf(jsonOutput + pos, maxSize - pos, "\"configured\":false");
    }
    
    pos += snprintf(jsonOutput + pos, maxSize - pos, "},\"devices\":{\"count\":%u}", 
                   getProvisionedDeviceCount());
    
    AddressPool pool;
    if (getAddressPoolInfo(pool)) {
        pos += snprintf(jsonOutput + pos, maxSize - pos, 
                       ",\"addresses\":{\"next\":\"0x%04X\",\"assigned\":%u,\"max\":%u}",
                       pool.nextAvailable, pool.totalAssigned, pool.maxDevices);
    }
    
    pos += snprintf(jsonOutput + pos, maxSize - pos, "}");
    
    return (pos < maxSize);
}

// Private Methods

esp_err_t NVSStorageService::openNVSHandle(const char* namespace_name, nvs_handle_t* handle, nvs_open_mode_t open_mode) {
    return nvs_open(namespace_name, open_mode, handle);
}

void NVSStorageService::closeNVSHandle(nvs_handle_t handle) {
    nvs_close(handle);
}

void NVSStorageService::generateDeviceKey(uint16_t address, char* keyBuffer, size_t maxSize) {
    snprintf(keyBuffer, maxSize, "%s%04X", NVS_KEY_DEVICE_PREFIX, address);
}

bool NVSStorageService::validateNetworkConfig(const NetworkConfig& config) {
    // Check if network key is not all zeros
    bool hasValidKey = false;
    for (int i = 0; i < 16; i++) {
        if (config.networkKey[i] != 0) {
            hasValidKey = true;
            break;
        }
    }
    
    if (!hasValidKey) {
        ESP_LOGE(TAG, "Network key is all zeros");
        return false;
    }
    
    if (config.keyVersion == 0) {
        ESP_LOGE(TAG, "Invalid key version");
        return false;
    }
    
    return true;
}

bool NVSStorageService::validateDeviceData(const ProvisionedDevice& device) {
    if (device.address < MIN_NODE_ADDRESS || device.address > MAX_NODE_ADDRESS) {
        ESP_LOGE(TAG, "Invalid device address: 0x%04X", device.address);
        return false;
    }
    
    if (device.address == BRIDGE_ADDRESS) {
        ESP_LOGE(TAG, "Device cannot use bridge address");
        return false;
    }
    
    if (device.status == 0 || device.status > DEVICE_STATUS_PENDING) {
        ESP_LOGE(TAG, "Invalid device status: %u", device.status);
        return false;
    }
    
    return true;
}