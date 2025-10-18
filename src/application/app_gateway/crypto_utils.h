#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <Arduino.h>

class CryptoUtils {
public:
    /**
     * @brief Generate 128-bit network key from userUID and gateway MAC
     * @param userUID Firebase user UID string
     * @param gatewayMac Gateway MAC address (6 bytes)
     * @param netkeyOut Output buffer (must be at least 16 bytes)
     * @return true on success
     */
    static bool deriveNetkey(const String& userUID, const uint8_t* gatewayMac, uint8_t* netkeyOut);
    
    /**
     * @brief Convert binary data to hex string
     * @param data Input binary data
     * @param len Length of input data
     * @return Hex string (lowercase)
     */
    static String toHexString(const uint8_t* data, size_t len);
    
    /**
     * @brief Get gateway MAC address as string (AA:BB:CC:DD:EE:FF format)
     * @return MAC address string
     */
    static String getGatewayMAC();
    
private:
    CryptoUtils() {} // Static class, no instances
};

#endif // CRYPTO_UTILS_H
