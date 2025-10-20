#pragma once

#include <cstdint>

/**
 * @brief Device type enumeration for multi-sensor IoT system
 * 
 * Each node/gateway can have a different sensor type attached.
 * This allows the system to handle heterogeneous sensor networks.
 * 
 * @note Add new sensor types here as the system expands
 */
enum class DeviceType : uint8_t {
    UNKNOWN = 0,           ///< Unknown/unconfigured device
    GATEWAY = 1,           ///< Gateway device (no sensors, just routing)
    SOIL_SENSOR = 2,       ///< Soil moisture sensor (7 parameters: moisture, temp, pH, EC, N, P, K)
    ENV_SENSOR = 3,        ///< Environment sensor (temp, humidity, pressure, light)
    WATER_SENSOR = 4,      ///< Water quality sensor (pH, TDS, temp, turbidity)
    CAMERA = 5,            ///< Camera + AI vision
    ACTUATOR = 6,          ///< Actuator device (relay, valve, motor control)
    CUSTOM = 255           ///< Custom sensor type (user-defined)
};

/**
 * @brief Convert DeviceType enum to string for logging/Firebase
 * @param type Device type enum value
 * @return String representation of device type
 */
inline const char* deviceTypeToString(DeviceType type) {
    switch (type) {
        case DeviceType::UNKNOWN:      return "unknown";
        case DeviceType::GATEWAY:      return "gateway";
        case DeviceType::SOIL_SENSOR:  return "soil_sensor";
        case DeviceType::ENV_SENSOR:   return "env_sensor";
        case DeviceType::WATER_SENSOR: return "water_sensor";
        case DeviceType::CAMERA:       return "camera";
        case DeviceType::ACTUATOR:     return "actuator";
        case DeviceType::CUSTOM:       return "custom";
        default:                       return "unknown";
    }
}

/**
 * @brief Convert string to DeviceType enum (for parsing commands)
 * @param str String representation of device type
 * @return DeviceType enum value
 */
inline DeviceType stringToDeviceType(const char* str) {
    if (strcmp(str, "gateway") == 0)      return DeviceType::GATEWAY;
    if (strcmp(str, "soil_sensor") == 0)  return DeviceType::SOIL_SENSOR;
    if (strcmp(str, "env_sensor") == 0)   return DeviceType::ENV_SENSOR;
    if (strcmp(str, "water_sensor") == 0) return DeviceType::WATER_SENSOR;
    if (strcmp(str, "camera") == 0)       return DeviceType::CAMERA;
    if (strcmp(str, "actuator") == 0)     return DeviceType::ACTUATOR;
    if (strcmp(str, "custom") == 0)       return DeviceType::CUSTOM;
    return DeviceType::UNKNOWN;
}
