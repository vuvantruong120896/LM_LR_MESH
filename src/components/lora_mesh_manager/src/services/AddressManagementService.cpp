#include "AddressManagementService.h"
#include <esp_timer.h>
#include <esp_random.h>
#include <cstring>

// Static member initialization
bool AddressManagementService::initialized = false;
const char* AddressManagementService::TAG = "AddrMgmt";

bool AddressManagementService::initialize() {
    if (initialized) {
        ESP_LOGI(TAG, "Address Management Service already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Address Management Service...");
    
    // Initialize NVS storage if not already done
    if (!NVSStorageService::isInitialized()) {
        if (!NVSStorageService::initialize()) {
            ESP_LOGE(TAG, "Failed to initialize NVS Storage Service");
            return false;
        }
    }
    
    // Validate address pool integrity
    if (!validateAddressPool()) {
        ESP_LOGW(TAG, "Address pool validation issues detected, attempting repair...");
        // Could implement pool repair logic here
    }
    
    PoolStatistics stats;
    if (getPoolStatistics(stats)) {
        ESP_LOGI(TAG, "Address Pool Statistics:");
        ESP_LOGI(TAG, "  Total Capacity: %d devices", stats.totalDevices);
        ESP_LOGI(TAG, "  Currently Allocated: %d devices", stats.allocatedDevices);
        ESP_LOGI(TAG, "  Available Slots: %d devices", stats.availableDevices);
        ESP_LOGI(TAG, "  Pool Utilization: %.1f%%", stats.utilizationPercent);
        ESP_LOGI(TAG, "  Next Address: 0x%04X", stats.nextAddress);
    }
    
    initialized = true;
    ESP_LOGI(TAG, "Address Management Service initialized successfully");
    return true;
}

bool AddressManagementService::isInitialized() {
    return initialized;
}

AddressManagementService::AllocationResult AddressManagementService::allocateAddress(
    const uint8_t* deviceUUID, uint8_t deviceType, const char* deviceName) {
    
    AllocationResult result = {0, false, "Service not initialized"};
    
    if (!initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return result;
    }
    
    if (!deviceUUID || !deviceName) {
        result.errorMessage = "Invalid parameters";
        ESP_LOGE(TAG, "Invalid parameters for address allocation");
        return result;
    }
    
    // Check if device already exists
    uint16_t existingAddress = findExistingDeviceByUUID(deviceUUID);
    if (existingAddress > 0) {
        result.address = existingAddress;
        result.success = true;
        result.errorMessage = "Device already has allocated address";
        ESP_LOGI(TAG, "Device %s already allocated address 0x%04X", deviceName, existingAddress);
        return result;
    }
    
    // Allocate new address from NVS storage service
    uint16_t newAddress = NVSStorageService::allocateAddress();
    if (newAddress == 0) {
        result.errorMessage = "Address pool exhausted";
        ESP_LOGE(TAG, "Failed to allocate address - pool exhausted");
        return result;
    }
    
    // Create provisioned device record
    ProvisionedDevice device;
    createProvisionedDevice(newAddress, deviceUUID, deviceType, deviceName, device);
    
    // Save device to persistent storage
    if (!NVSStorageService::saveProvisionedDevice(device)) {
        // If save failed, release the allocated address
        NVSStorageService::releaseAddress(newAddress);
        result.errorMessage = "Failed to save device record";
        ESP_LOGE(TAG, "Failed to save device record for address 0x%04X", newAddress);
        return result;
    }
    
    result.address = newAddress;
    result.success = true;
    result.errorMessage = "Address allocated successfully";
    
    logAddressOperation(newAddress, deviceName, "ALLOCATE");
    ESP_LOGI(TAG, "Successfully allocated address 0x%04X to device %s", newAddress, deviceName);
    
    return result;
}

AddressManagementService::AllocationResult AddressManagementService::reserveAddress(
    uint16_t requestedAddress, const uint8_t* deviceUUID, 
    uint8_t deviceType, const char* deviceName) {
    
    AllocationResult result = {0, false, "Service not initialized"};
    
    if (!initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return result;
    }
    
    if (!isValidAddress(requestedAddress)) {
        result.errorMessage = "Invalid address";
        ESP_LOGE(TAG, "Invalid address 0x%04X requested", requestedAddress);
        return result;
    }
    
    if (!isAddressAvailable(requestedAddress)) {
        result.errorMessage = "Address not available";
        ESP_LOGE(TAG, "Address 0x%04X not available for reservation", requestedAddress);
        return result;
    }
    
    // Create provisioned device record
    ProvisionedDevice device;
    createProvisionedDevice(requestedAddress, deviceUUID, deviceType, deviceName, device);
    
    // Save device to persistent storage
    if (!NVSStorageService::saveProvisionedDevice(device)) {
        result.errorMessage = "Failed to save device record";
        ESP_LOGE(TAG, "Failed to save device record for reserved address 0x%04X", requestedAddress);
        return result;
    }
    
    result.address = requestedAddress;
    result.success = true;
    result.errorMessage = "Address reserved successfully";
    
    logAddressOperation(requestedAddress, deviceName, "RESERVE");
    ESP_LOGI(TAG, "Successfully reserved address 0x%04X for device %s", requestedAddress, deviceName);
    
    return result;
}

bool AddressManagementService::releaseAddress(uint16_t address) {
    if (!initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return false;
    }
    
    if (!isValidAddress(address)) {
        ESP_LOGE(TAG, "Invalid address 0x%04X for release", address);
        return false;
    }
    
    // Get device info for logging
    ProvisionedDevice device;
    bool deviceExists = NVSStorageService::loadProvisionedDevice(address, device);
    
    // Remove device from storage
    if (NVSStorageService::removeProvisionedDevice(address)) {
        logAddressOperation(address, deviceExists ? device.deviceName : "Unknown", "RELEASE");
        ESP_LOGI(TAG, "Successfully released address 0x%04X", address);
        return true;
    }
    
    ESP_LOGE(TAG, "Failed to release address 0x%04X", address);
    return false;
}

bool AddressManagementService::isAddressAllocated(uint16_t address) {
    if (!initialized || !isValidAddress(address)) {
        return false;
    }
    
    return NVSStorageService::isDeviceProvisioned(address);
}

bool AddressManagementService::isAddressAvailable(uint16_t address) {
    if (!initialized || !isValidAddress(address)) {
        return false;
    }
    
    return NVSStorageService::isAddressAvailable(address);
}

bool AddressManagementService::isValidAddress(uint16_t address) {
    // Check if address is in valid range
    if (address < MIN_NODE_ADDRESS || address > MAX_NODE_ADDRESS) {
        return false;
    }
    
    // Check if address is reserved (bridge address)
    if (address == BRIDGE_ADDRESS) {
        return false;
    }
    
    return true;
}

bool AddressManagementService::getDeviceInfo(uint16_t address, ProvisionedDevice& device) {
    if (!initialized) {
        return false;
    }
    
    return NVSStorageService::loadProvisionedDevice(address, device);
}

bool AddressManagementService::updateDeviceStatus(uint16_t address, DeviceStatus status) {
    if (!initialized) {
        return false;
    }
    
    bool success = NVSStorageService::updateDeviceStatus(address, status);
    if (success) {
        const char* statusStr = (status == DEVICE_STATUS_ACTIVE) ? "ACTIVE" :
                              (status == DEVICE_STATUS_INACTIVE) ? "INACTIVE" :
                              (status == DEVICE_STATUS_REVOKED) ? "REVOKED" :
                              (status == DEVICE_STATUS_PENDING) ? "PENDING" : "UNKNOWN";
        ESP_LOGI(TAG, "Updated device 0x%04X status to %s", address, statusStr);
    }
    
    return success;
}

bool AddressManagementService::updateDeviceLastSeen(uint16_t address) {
    if (!initialized) {
        return false;
    }
    
    ProvisionedDevice device;
    if (!NVSStorageService::loadProvisionedDevice(address, device)) {
        return false;
    }
    
    device.lastSeen = esp_timer_get_time() / 1000000; // Current timestamp in seconds
    return NVSStorageService::saveProvisionedDevice(device);
}

bool AddressManagementService::findDeviceByUUID(const uint8_t* deviceUUID, ProvisionedDevice& device) {
    if (!initialized || !deviceUUID) {
        return false;
    }
    
    uint16_t address = findExistingDeviceByUUID(deviceUUID);
    if (address > 0) {
        return NVSStorageService::loadProvisionedDevice(address, device);
    }
    
    return false;
}

bool AddressManagementService::getPoolStatistics(PoolStatistics& stats) {
    if (!initialized) {
        return false;
    }
    
    calculatePoolStatistics(stats);
    return true;
}

uint16_t AddressManagementService::getAllocatedAddresses(uint16_t* addresses, uint16_t maxAddresses) {
    if (!initialized || !addresses || maxAddresses == 0) {
        return 0;
    }
    
    ProvisionedDevice* devices = (ProvisionedDevice*)malloc(maxAddresses * sizeof(ProvisionedDevice));
    if (!devices) {
        ESP_LOGE(TAG, "Failed to allocate memory for device list");
        return 0;
    }
    
    uint16_t deviceCount = NVSStorageService::loadAllProvisionedDevices(devices, maxAddresses);
    
    for (uint16_t i = 0; i < deviceCount; i++) {
        addresses[i] = devices[i].address;
    }
    
    free(devices);
    return deviceCount;
}

uint16_t AddressManagementService::getAvailableAddresses(uint16_t startAddress, uint16_t endAddress,
                                                       uint16_t* addresses, uint16_t maxAddresses) {
    if (!initialized || !addresses || maxAddresses == 0 || startAddress > endAddress) {
        return 0;
    }
    
    uint16_t count = 0;
    
    for (uint16_t addr = startAddress; addr <= endAddress && count < maxAddresses; addr++) {
        if (isAddressAvailable(addr)) {
            addresses[count++] = addr;
        }
    }
    
    return count;
}

bool AddressManagementService::resetAddressPool() {
    if (!initialized) {
        return false;
    }
    
    ESP_LOGW(TAG, "Resetting address pool - all device allocations will be lost!");
    
    // Clear all device records
    bool success = NVSStorageService::formatStorage();
    
    if (success) {
        ESP_LOGI(TAG, "Address pool reset completed");
    } else {
        ESP_LOGE(TAG, "Address pool reset failed");
    }
    
    return success;
}

bool AddressManagementService::markDeviceActive(uint16_t address) {
    return updateDeviceStatus(address, DEVICE_STATUS_ACTIVE);
}

bool AddressManagementService::markDeviceInactive(uint16_t address) {
    return updateDeviceStatus(address, DEVICE_STATUS_INACTIVE);
}

bool AddressManagementService::revokeDevice(uint16_t address) {
    bool success = updateDeviceStatus(address, DEVICE_STATUS_REVOKED);
    if (success) {
        logAddressOperation(address, "Device", "REVOKE");
    }
    return success;
}

uint16_t AddressManagementService::cleanupInactiveDevices(uint32_t inactivityThresholdSeconds) {
    if (!initialized) {
        return 0;
    }
    
    ProvisionedDevice devices[MAX_PROVISIONED_DEVICES];
    uint16_t deviceCount = NVSStorageService::loadAllProvisionedDevices(devices, MAX_PROVISIONED_DEVICES);
    
    uint32_t currentTime = esp_timer_get_time() / 1000000; // Current time in seconds
    uint16_t cleanedCount = 0;
    
    for (uint16_t i = 0; i < deviceCount; i++) {
        // Check if device is inactive for too long
        if (currentTime - devices[i].lastSeen > inactivityThresholdSeconds &&
            devices[i].status != DEVICE_STATUS_REVOKED) {
            
            ESP_LOGI(TAG, "Cleaning up inactive device 0x%04X (%s), last seen %u seconds ago",
                     devices[i].address, devices[i].deviceName,
                     currentTime - devices[i].lastSeen);
            
            if (releaseAddress(devices[i].address)) {
                cleanedCount++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Cleaned up %d inactive devices", cleanedCount);
    return cleanedCount;
}

bool AddressManagementService::detectAddressCollision(uint16_t suspectedAddress) {
    // This would involve mesh network discovery to detect if multiple devices
    // are responding to the same address. For now, just check internal consistency.
    
    if (!initialized) {
        return false;
    }
    
    // Simple check: verify the address is in our records
    return isAddressAllocated(suspectedAddress);
}

uint16_t AddressManagementService::resolveAddressCollision(uint16_t conflictedAddress) {
    if (!initialized) {
        return 0;
    }
    
    ESP_LOGW(TAG, "Resolving address collision for 0x%04X", conflictedAddress);
    
    ProvisionedDevice device;
    if (!getDeviceInfo(conflictedAddress, device)) {
        ESP_LOGE(TAG, "Cannot resolve collision - device info not found");
        return 0;
    }
    
    // Allocate new address for the conflicted device
    AllocationResult result = allocateAddress(device.deviceUUID, device.deviceType, device.deviceName);
    
    if (result.success && result.address != conflictedAddress) {
        // Remove old conflicted address
        NVSStorageService::removeProvisionedDevice(conflictedAddress);
        
        ESP_LOGI(TAG, "Address collision resolved: 0x%04X -> 0x%04X", conflictedAddress, result.address);
        return result.address;
    }
    
    return 0;
}

bool AddressManagementService::validateAddressPool() {
    if (!initialized) {
        return false;
    }
    
    // Load all devices and validate consistency
    ProvisionedDevice devices[MAX_PROVISIONED_DEVICES];
    uint16_t deviceCount = NVSStorageService::loadAllProvisionedDevices(devices, MAX_PROVISIONED_DEVICES);
    
    bool isValid = true;
    
    // Check for duplicate addresses
    for (uint16_t i = 0; i < deviceCount; i++) {
        for (uint16_t j = i + 1; j < deviceCount; j++) {
            if (devices[i].address == devices[j].address) {
                ESP_LOGE(TAG, "Duplicate address detected: 0x%04X", devices[i].address);
                isValid = false;
            }
        }
        
        // Check address validity
        if (!isValidAddress(devices[i].address)) {
            ESP_LOGE(TAG, "Invalid address in pool: 0x%04X", devices[i].address);
            isValid = false;
        }
    }
    
    return isValid;
}

void AddressManagementService::generateDeviceUUID(uint8_t* deviceUUID) {
    if (!deviceUUID) {
        return;
    }
    
    // Generate random UUID using ESP32 hardware random number generator
    for (int i = 0; i < 16; i += 4) {
        uint32_t random = esp_random();
        memcpy(&deviceUUID[i], &random, 4);
    }
    
    // Set version (4) and variant bits for UUID v4
    deviceUUID[6] = (deviceUUID[6] & 0x0F) | 0x40;  // Version 4
    deviceUUID[8] = (deviceUUID[8] & 0x3F) | 0x80;  // Variant bits
}

bool AddressManagementService::getAddressPoolStatus(char* statusBuffer, size_t maxSize) {
    if (!initialized || !statusBuffer || maxSize < 100) {
        return false;
    }
    
    PoolStatistics stats;
    if (!getPoolStatistics(stats)) {
        return false;
    }
    
    snprintf(statusBuffer, maxSize,
             "Address Pool Status:\n"
             "  Total Capacity: %d devices\n"
             "  Allocated: %d devices (%.1f%%)\n"
             "  Available: %d devices\n"
             "  Next Address: 0x%04X\n"
             "  Pool Health: %s",
             stats.totalDevices,
             stats.allocatedDevices, stats.utilizationPercent,
             stats.availableDevices,
             stats.nextAddress,
             (stats.utilizationPercent < 90.0) ? "GOOD" : "CRITICAL");
    
    return true;
}

bool AddressManagementService::exportAddressPoolJSON(char* jsonBuffer, size_t maxSize) {
    if (!initialized || !jsonBuffer || maxSize < 200) {
        return false;
    }
    
    PoolStatistics stats;
    calculatePoolStatistics(stats);
    
    int pos = snprintf(jsonBuffer, maxSize,
                      "{\"addressPool\":{"
                      "\"totalCapacity\":%d,"
                      "\"allocated\":%d,"
                      "\"available\":%d,"
                      "\"utilization\":%.1f,"
                      "\"nextAddress\":\"0x%04X\","
                      "\"devices\":[",
                      stats.totalDevices, stats.allocatedDevices, stats.availableDevices,
                      stats.utilizationPercent, stats.nextAddress);
    
    // Add device list (limited to avoid buffer overflow)
    ProvisionedDevice devices[10]; // Limited sample
    uint16_t deviceCount = NVSStorageService::loadAllProvisionedDevices(devices, 10);
    
    for (uint16_t i = 0; i < deviceCount && pos < (maxSize - 100); i++) {
        pos += snprintf(jsonBuffer + pos, maxSize - pos,
                       "%s{\"address\":\"0x%04X\",\"name\":\"%s\",\"status\":%d}",
                       (i > 0) ? "," : "",
                       devices[i].address, devices[i].deviceName, devices[i].status);
    }
    
    snprintf(jsonBuffer + pos, maxSize - pos, "]}}");
    
    return true;
}

// Private Methods

void AddressManagementService::createProvisionedDevice(uint16_t address, const uint8_t* deviceUUID,
                                                      uint8_t deviceType, const char* deviceName,
                                                      ProvisionedDevice& device) {
    memset(&device, 0, sizeof(ProvisionedDevice));
    
    device.address = address;
    memcpy(device.deviceUUID, deviceUUID, sizeof(device.deviceUUID));
    device.deviceType = deviceType;
    device.status = DEVICE_STATUS_PENDING; // New devices start as pending
    device.provisionTime = esp_timer_get_time() / 1000000; // Current timestamp
    device.lastSeen = device.provisionTime;
    
    // Copy device name with bounds checking
    size_t nameLen = strlen(deviceName);
    if (nameLen >= sizeof(device.deviceName)) {
        nameLen = sizeof(device.deviceName) - 1;
    }
    memcpy(device.deviceName, deviceName, nameLen);
    device.deviceName[nameLen] = '\0';
}

void AddressManagementService::logAddressOperation(uint16_t address, const char* deviceName, const char* operation) {
    ESP_LOGI(TAG, "[%s] Address: 0x%04X, Device: %s", operation, address, deviceName);
}

uint16_t AddressManagementService::findExistingDeviceByUUID(const uint8_t* deviceUUID) {
    if (!deviceUUID) {
        return 0;
    }
    
    ProvisionedDevice devices[MAX_PROVISIONED_DEVICES];
    uint16_t deviceCount = NVSStorageService::loadAllProvisionedDevices(devices, MAX_PROVISIONED_DEVICES);
    
    for (uint16_t i = 0; i < deviceCount; i++) {
        if (memcmp(devices[i].deviceUUID, deviceUUID, 16) == 0) {
            return devices[i].address;
        }
    }
    
    return 0; // Not found
}

void AddressManagementService::calculatePoolStatistics(PoolStatistics& stats) {
    stats.totalDevices = MAX_PROVISIONED_DEVICES;
    stats.allocatedDevices = NVSStorageService::getProvisionedDeviceCount();
    stats.availableDevices = stats.totalDevices - stats.allocatedDevices;
    
    AddressPool pool;
    if (NVSStorageService::getAddressPoolInfo(pool)) {
        stats.nextAddress = pool.nextAvailable;
    } else {
        stats.nextAddress = MIN_NODE_ADDRESS;
    }
    
    stats.utilizationPercent = (stats.totalDevices > 0) ? 
                              ((float)stats.allocatedDevices / stats.totalDevices * 100.0f) : 0.0f;
}