#ifndef _ADDRESS_MANAGEMENT_SERVICE_H
#define _ADDRESS_MANAGEMENT_SERVICE_H

#include <Arduino.h>
#include "NVSStorageService.h"

/**
 * @brief Address Management Service for mesh network address allocation
 * 
 * Provides centralized address management for mesh network nodes with
 * persistent storage, collision detection, and allocation tracking.
 * Uses NVS Storage Service for persistence.
 */
class AddressManagementService {
public:
    /**
     * @brief Address allocation result structure
     */
    struct AllocationResult {
        uint16_t address;           // Allocated address (0 if allocation failed)
        bool success;               // Whether allocation was successful
        const char* errorMessage;   // Error description if allocation failed
    };
    
    /**
     * @brief Address pool statistics
     */
    struct PoolStatistics {
        uint16_t totalDevices;      // Total devices that can be supported
        uint16_t allocatedDevices;  // Number of currently allocated devices
        uint16_t availableDevices;  // Number of available address slots
        uint16_t nextAddress;       // Next address that will be allocated
        float utilizationPercent;   // Pool utilization percentage
    };
    
    /**
     * @brief Initialize address management service
     * @return true if initialization successful
     */
    static bool initialize();
    
    /**
     * @brief Check if service is initialized
     * @return true if service is ready
     */
    static bool isInitialized();
    
    // Core Address Management
    
    /**
     * @brief Allocate next available address for a new device
     * @param deviceUUID Device unique identifier (16 bytes)
     * @param deviceType Device type/capability flags
     * @param deviceName Human-readable device name
     * @return Allocation result with address and status
     */
    static AllocationResult allocateAddress(const uint8_t* deviceUUID, 
                                          uint8_t deviceType = 0x01, 
                                          const char* deviceName = "Unknown Device");
    
    /**
     * @brief Reserve a specific address if available
     * @param requestedAddress Address to reserve
     * @param deviceUUID Device unique identifier
     * @param deviceType Device type flags
     * @param deviceName Device name
     * @return Allocation result with status
     */
    static AllocationResult reserveAddress(uint16_t requestedAddress,
                                         const uint8_t* deviceUUID,
                                         uint8_t deviceType = 0x01,
                                         const char* deviceName = "Unknown Device");
    
    /**
     * @brief Release an allocated address back to the pool
     * @param address Address to release
     * @return true if release was successful
     */
    static bool releaseAddress(uint16_t address);
    
    /**
     * @brief Check if an address is currently allocated
     * @param address Address to check
     * @return true if address is allocated to a device
     */
    static bool isAddressAllocated(uint16_t address);
    
    /**
     * @brief Check if an address is available for allocation
     * @param address Address to check
     * @return true if address can be allocated
     */
    static bool isAddressAvailable(uint16_t address);
    
    /**
     * @brief Validate address range and restrictions
     * @param address Address to validate
     * @return true if address is in valid range and not restricted
     */
    static bool isValidAddress(uint16_t address);
    
    // Device Information Management
    
    /**
     * @brief Get device information by address
     * @param address Device address
     * @param device Output buffer for device information
     * @return true if device found
     */
    static bool getDeviceInfo(uint16_t address, ProvisionedDevice& device);
    
    /**
     * @brief Update device status and last seen timestamp
     * @param address Device address
     * @param status New device status
     * @return true if update successful
     */
    static bool updateDeviceStatus(uint16_t address, DeviceStatus status);
    
    /**
     * @brief Update device last seen timestamp
     * @param address Device address
     * @return true if update successful
     */
    static bool updateDeviceLastSeen(uint16_t address);
    
    /**
     * @brief Find device by UUID
     * @param deviceUUID UUID to search for
     * @param device Output buffer for device information
     * @return true if device found
     */
    static bool findDeviceByUUID(const uint8_t* deviceUUID, ProvisionedDevice& device);
    
    // Address Pool Statistics and Management
    
    /**
     * @brief Get current address pool statistics
     * @param stats Output buffer for statistics
     * @return true if statistics retrieved successfully
     */
    static bool getPoolStatistics(PoolStatistics& stats);
    
    /**
     * @brief Get list of all allocated addresses
     * @param addresses Output array for allocated addresses
     * @param maxAddresses Maximum number of addresses to return
     * @return Number of addresses returned
     */
    static uint16_t getAllocatedAddresses(uint16_t* addresses, uint16_t maxAddresses);
    
    /**
     * @brief Get list of all available addresses in range
     * @param startAddress Start of address range
     * @param endAddress End of address range  
     * @param addresses Output array for available addresses
     * @param maxAddresses Maximum number of addresses to return
     * @return Number of available addresses found
     */
    static uint16_t getAvailableAddresses(uint16_t startAddress, uint16_t endAddress,
                                        uint16_t* addresses, uint16_t maxAddresses);
    
    /**
     * @brief Reset address pool (WARNING: clears all allocations)
     * @return true if reset successful
     */
    static bool resetAddressPool();
    
    // Device Lifecycle Management
    
    /**
     * @brief Mark device as active (recently seen)
     * @param address Device address
     * @return true if marked successfully
     */
    static bool markDeviceActive(uint16_t address);
    
    /**
     * @brief Mark device as inactive (not responding)
     * @param address Device address
     * @return true if marked successfully
     */
    static bool markDeviceInactive(uint16_t address);
    
    /**
     * @brief Revoke device access (security breach)
     * @param address Device address
     * @return true if revoked successfully
     */
    static bool revokeDevice(uint16_t address);
    
    /**
     * @brief Clean up expired/inactive devices
     * @param inactivityThresholdSeconds Remove devices inactive for this long
     * @return Number of devices cleaned up
     */
    static uint16_t cleanupInactiveDevices(uint32_t inactivityThresholdSeconds = 86400); // 24 hours
    
    // Collision Detection and Recovery
    
    /**
     * @brief Check for address collisions in the network
     * @param suspectedAddress Address to check for collision
     * @return true if collision detected
     */
    static bool detectAddressCollision(uint16_t suspectedAddress);
    
    /**
     * @brief Resolve address collision by reassigning addresses
     * @param conflictedAddress Address that has collision
     * @return New address assigned to resolve conflict (0 if failed)
     */
    static uint16_t resolveAddressCollision(uint16_t conflictedAddress);
    
    /**
     * @brief Validate address pool integrity
     * @return true if pool is consistent and valid
     */
    static bool validateAddressPool();
    
    // Utility Functions
    
    /**
     * @brief Generate UUID for device identification
     * @param deviceUUID Output buffer for generated UUID (16 bytes)
     */
    static void generateDeviceUUID(uint8_t* deviceUUID);
    
    /**
     * @brief Get human-readable address pool status
     * @param statusBuffer Output buffer for status string
     * @param maxSize Maximum buffer size
     * @return true if status generated successfully
     */
    static bool getAddressPoolStatus(char* statusBuffer, size_t maxSize);
    
    /**
     * @brief Export address pool to JSON for debugging
     * @param jsonBuffer Output buffer for JSON string
     * @param maxSize Maximum buffer size
     * @return true if export successful
     */
    static bool exportAddressPoolJSON(char* jsonBuffer, size_t maxSize);

private:
    static bool initialized;
    static const char* TAG;
    
    /**
     * @brief Create ProvisionedDevice structure from allocation parameters
     * @param address Allocated address
     * @param deviceUUID Device UUID
     * @param deviceType Device type
     * @param deviceName Device name
     * @param device Output device structure
     */
    static void createProvisionedDevice(uint16_t address, const uint8_t* deviceUUID,
                                      uint8_t deviceType, const char* deviceName,
                                      ProvisionedDevice& device);
    
    /**
     * @brief Log address allocation event
     * @param address Allocated address
     * @param deviceName Device name
     * @param operation Operation type (ALLOCATE, RELEASE, etc.)
     */
    static void logAddressOperation(uint16_t address, const char* deviceName, const char* operation);
    
    /**
     * @brief Check if device UUID already exists
     * @param deviceUUID UUID to check
     * @return Address of existing device (0 if not found)
     */
    static uint16_t findExistingDeviceByUUID(const uint8_t* deviceUUID);
    
    /**
     * @brief Calculate pool utilization statistics
     * @param stats Output statistics structure
     */
    static void calculatePoolStatistics(PoolStatistics& stats);
};

#endif // _ADDRESS_MANAGEMENT_SERVICE_H