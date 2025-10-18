#include "crypto_utils.h"
#include "mbedtls/sha256.h"
#include "esp_mac.h"

static const char* TAG = "CryptoUtils";

bool CryptoUtils::deriveNetkey(const String& userUID, const uint8_t* gatewayMac, uint8_t* netkeyOut) {
    if (userUID.isEmpty() || !gatewayMac || !netkeyOut) {
        ESP_LOGE(TAG, "Invalid parameters for netkey derivation");
        return false;
    }
    
    // Format: "userUID|AA:BB:CC:DD:EE:FF"
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             gatewayMac[0], gatewayMac[1], gatewayMac[2],
             gatewayMac[3], gatewayMac[4], gatewayMac[5]);
    
    String input = userUID + "|" + macStr;
    
    ESP_LOGI(TAG, "Deriving netkey from: %s", input.c_str());
    
    // Compute SHA-256
    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const unsigned char*)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    
    // Take first 16 bytes as netkey (128-bit)
    memcpy(netkeyOut, hash, 16);
    
    ESP_LOGI(TAG, "Netkey derived: %s", toHexString(netkeyOut, 16).c_str());
    
    return true;
}

String CryptoUtils::toHexString(const uint8_t* data, size_t len) {
    String result;
    result.reserve(len * 2);
    
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        result += buf;
    }
    
    return result;
}

String CryptoUtils::getGatewayMAC() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return String(macStr);
}
