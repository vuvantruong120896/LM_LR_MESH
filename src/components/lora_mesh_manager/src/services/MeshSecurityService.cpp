#include "mesh_security.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/hkdf.h"

static const char* MESH_SEC_TAG = "MeshSecurity";

// Static member initialization
MeshSecurityConfig MeshSecurityService::config;
mbedtls_aes_context MeshSecurityService::aes_ctx;
bool MeshSecurityService::initialized = false;
uint32_t MeshSecurityService::sequenceCounter = 0;
uint16_t MeshSecurityService::authenticatedNodes[32] = {0};
uint8_t MeshSecurityService::authenticatedCount = 0;
uint32_t MeshSecurityService::lastSequenceNumbers[32] = {0};

bool MeshSecurityService::initialize(const MeshSecurityConfig& cfg) {
    if (initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service already initialized");
        return true;
    }
    
    config = cfg;
    
    // Initialize AES context
    mbedtls_aes_init(&aes_ctx);
    
    // Set encryption key
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, config.networkKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        ESP_LOGE(MESH_SEC_TAG, "Failed to set AES encryption key: %d", ret);
        return false;
    }
    
    // Initialize sequence counter with random value
    esp_fill_random((uint8_t*)&sequenceCounter, sizeof(sequenceCounter));
    
    initialized = true;
    
    ESP_LOGI(MESH_SEC_TAG, "Mesh security initialized - Encryption: %s, Auth: %s, Level: %d",
             config.enableEncryption ? "ON" : "OFF",
             config.enableAuthentication ? "ON" : "OFF",
             config.securityLevel);
    
    return true;
}

MeshSecurityResult MeshSecurityService::encryptPayload(const uint8_t* plaintext, size_t plaintextLen,
                                                       uint8_t* ciphertext, size_t* ciphertextLen,
                                                       const uint8_t* nonce) {
    if (!initialized || !config.enableEncryption) {
        // Copy plaintext to ciphertext if encryption disabled
        if (*ciphertextLen >= plaintextLen) {
            memcpy(ciphertext, plaintext, plaintextLen);
            *ciphertextLen = plaintextLen;
            return MESH_SEC_OK;
        }
        return MESH_SEC_INVALID_KEY;
    }
    
    if (!plaintext || !ciphertext || !nonce || plaintextLen == 0) {
        ESP_LOGE(MESH_SEC_TAG, "Invalid encryption parameters");
        return MESH_SEC_INVALID_KEY;
    }
    
    // Derive session key from master key and nonce
    uint8_t sessionKey[MESH_NETKEY_SIZE];
    deriveEncryptionKey(sessionKey, config.networkKey, nonce);
    
    // Set up AES context with session key
    mbedtls_aes_context session_ctx;
    mbedtls_aes_init(&session_ctx);
    
    int ret = mbedtls_aes_setkey_enc(&session_ctx, sessionKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&session_ctx);
        ESP_LOGE(MESH_SEC_TAG, "Failed to set session key: %d", ret);
        return MESH_SEC_INVALID_KEY;
    }
    
    // Ensure output buffer is large enough (AES block size alignment)
    size_t blocks = (plaintextLen + 15) / 16;  // Round up to block size
    size_t paddedLen = blocks * 16;
    
    if (*ciphertextLen < paddedLen) {
        mbedtls_aes_free(&session_ctx);
        ESP_LOGE(MESH_SEC_TAG, "Output buffer too small: need %zu, got %zu", paddedLen, *ciphertextLen);
        return MESH_SEC_INVALID_KEY;
    }
    
    // Prepare padded plaintext (PKCS7 padding)
    uint8_t paddedPlaintext[256];
    if (paddedLen > sizeof(paddedPlaintext)) {
        mbedtls_aes_free(&session_ctx);
        ESP_LOGE(MESH_SEC_TAG, "Plaintext too large for encryption");
        return MESH_SEC_INVALID_KEY;
    }
    
    memcpy(paddedPlaintext, plaintext, plaintextLen);
    uint8_t padValue = paddedLen - plaintextLen;
    for (size_t i = plaintextLen; i < paddedLen; i++) {
        paddedPlaintext[i] = padValue;
    }
    
    // Encrypt using CBC mode with nonce as IV
    uint8_t iv[16];
    memcpy(iv, nonce, MESH_NONCE_SIZE);
    memset(iv + MESH_NONCE_SIZE, 0, 16 - MESH_NONCE_SIZE); // Pad IV to 16 bytes
    
    ret = mbedtls_aes_crypt_cbc(&session_ctx, MBEDTLS_AES_ENCRYPT, paddedLen,
                                iv, paddedPlaintext, ciphertext);
    
    mbedtls_aes_free(&session_ctx);
    memset(sessionKey, 0, sizeof(sessionKey)); // Clear session key
    
    if (ret != 0) {
        ESP_LOGE(MESH_SEC_TAG, "AES encryption failed: %d", ret);
        return MESH_SEC_DECRYPT_FAILED;
    }
    
    *ciphertextLen = paddedLen;
    ESP_LOGV(MESH_SEC_TAG, "Payload encrypted: %zu bytes -> %zu bytes", plaintextLen, paddedLen);
    
    return MESH_SEC_OK;
}

MeshSecurityResult MeshSecurityService::decryptPayload(const uint8_t* ciphertext, size_t ciphertextLen,
                                                       uint8_t* plaintext, size_t* plaintextLen,
                                                       const uint8_t* nonce) {
    if (!initialized || !config.enableEncryption) {
        // Copy ciphertext to plaintext if encryption disabled
        if (*plaintextLen >= ciphertextLen) {
            memcpy(plaintext, ciphertext, ciphertextLen);
            *plaintextLen = ciphertextLen;
            return MESH_SEC_OK;
        }
        return MESH_SEC_INVALID_KEY;
    }
    
    if (!ciphertext || !plaintext || !nonce || ciphertextLen == 0 || (ciphertextLen % 16) != 0) {
        ESP_LOGE(MESH_SEC_TAG, "Invalid decryption parameters");
        return MESH_SEC_INVALID_KEY;
    }
    
    // Derive session key from master key and nonce
    uint8_t sessionKey[MESH_NETKEY_SIZE];
    deriveEncryptionKey(sessionKey, config.networkKey, nonce);
    
    // Set up AES context with session key
    mbedtls_aes_context session_ctx;
    mbedtls_aes_init(&session_ctx);
    
    int ret = mbedtls_aes_setkey_dec(&session_ctx, sessionKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_aes_free(&session_ctx);
        ESP_LOGE(MESH_SEC_TAG, "Failed to set decryption key: %d", ret);
        return MESH_SEC_INVALID_KEY;
    }
    
    // Ensure output buffer is large enough
    if (*plaintextLen < ciphertextLen) {
        mbedtls_aes_free(&session_ctx);
        ESP_LOGE(MESH_SEC_TAG, "Plaintext buffer too small");
        return MESH_SEC_INVALID_KEY;
    }
    
    // Decrypt using CBC mode with nonce as IV
    uint8_t iv[16];
    memcpy(iv, nonce, MESH_NONCE_SIZE);
    memset(iv + MESH_NONCE_SIZE, 0, 16 - MESH_NONCE_SIZE); // Pad IV to 16 bytes
    
    ret = mbedtls_aes_crypt_cbc(&session_ctx, MBEDTLS_AES_DECRYPT, ciphertextLen,
                                iv, ciphertext, plaintext);
    
    mbedtls_aes_free(&session_ctx);
    memset(sessionKey, 0, sizeof(sessionKey)); // Clear session key
    
    if (ret != 0) {
        ESP_LOGE(MESH_SEC_TAG, "AES decryption failed: %d", ret);
        return MESH_SEC_DECRYPT_FAILED;
    }
    
    // Remove PKCS7 padding
    uint8_t padValue = plaintext[ciphertextLen - 1];
    if (padValue > 16 || padValue == 0) {
        ESP_LOGE(MESH_SEC_TAG, "Invalid padding in decrypted data");
        return MESH_SEC_DECRYPT_FAILED;
    }
    
    // Verify padding
    for (size_t i = ciphertextLen - padValue; i < ciphertextLen; i++) {
        if (plaintext[i] != padValue) {
            ESP_LOGE(MESH_SEC_TAG, "Corrupted padding in decrypted data");
            return MESH_SEC_DECRYPT_FAILED;
        }
    }
    
    *plaintextLen = ciphertextLen - padValue;
    ESP_LOGV(MESH_SEC_TAG, "Payload decrypted: %zu bytes -> %zu bytes", ciphertextLen, *plaintextLen);
    
    return MESH_SEC_OK;
}

MeshSecurityResult MeshSecurityService::generateMAC(const uint8_t* data, size_t dataLen,
                                                    uint8_t* mac, const uint8_t* key) {
    if (!data || !mac || !key || dataLen == 0) {
        return MESH_SEC_INVALID_KEY;
    }
    
    // Use HMAC-SHA256 truncated to MESH_MAC_SIZE bytes
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        mbedtls_md_free(&md_ctx);
        return MESH_SEC_INVALID_KEY;
    }
    
    int ret = mbedtls_md_setup(&md_ctx, md_info, 1); // 1 for HMAC
    if (ret != 0) {
        mbedtls_md_free(&md_ctx);
        return MESH_SEC_INVALID_KEY;
    }
    
    ret = mbedtls_md_hmac_starts(&md_ctx, key, MESH_NETKEY_SIZE);
    if (ret != 0) {
        mbedtls_md_free(&md_ctx);
        return MESH_SEC_INVALID_KEY;
    }
    
    ret = mbedtls_md_hmac_update(&md_ctx, data, dataLen);
    if (ret != 0) {
        mbedtls_md_free(&md_ctx);
        return MESH_SEC_INVALID_KEY;
    }
    
    uint8_t fullMac[32]; // SHA256 output
    ret = mbedtls_md_hmac_finish(&md_ctx, fullMac);
    mbedtls_md_free(&md_ctx);
    
    if (ret != 0) {
        return MESH_SEC_INVALID_KEY;
    }
    
    // Use first MESH_MAC_SIZE bytes
    memcpy(mac, fullMac, MESH_MAC_SIZE);
    
    return MESH_SEC_OK;
}

MeshSecurityResult MeshSecurityService::verifyMAC(const uint8_t* data, size_t dataLen,
                                                  const uint8_t* mac, const uint8_t* key) {
    uint8_t computedMac[MESH_MAC_SIZE];
    
    MeshSecurityResult result = generateMAC(data, dataLen, computedMac, key);
    if (result != MESH_SEC_OK) {
        return result;
    }
    
    // Constant-time comparison to prevent timing attacks
    int diff = 0;
    for (int i = 0; i < MESH_MAC_SIZE; i++) {
        diff |= (mac[i] ^ computedMac[i]);
    }
    
    return (diff == 0) ? MESH_SEC_OK : MESH_SEC_INVALID_MAC;
}

void MeshSecurityService::generateNonce(uint8_t* nonce) {
    if (!nonce) return;
    
    // Generate cryptographically secure random nonce
    esp_fill_random(nonce, MESH_NONCE_SIZE);
}

bool MeshSecurityService::isValidSequenceNumber(uint16_t nodeId, uint32_t sequenceNumber) {
    if (!config.enableReplayProtection) {
        return true;
    }
    
    // Find node in tracking array
    for (int i = 0; i < 32 && i < authenticatedCount; i++) {
        if (authenticatedNodes[i] == nodeId) {
            if (sequenceNumber > lastSequenceNumbers[i]) {
                lastSequenceNumbers[i] = sequenceNumber;
                return true;
            }
            ESP_LOGW(MESH_SEC_TAG, "Replay attack detected from node 0x%04X: seq %lu <= last %lu",
                     nodeId, sequenceNumber, lastSequenceNumbers[i]);
            return false;
        }
    }
    
    // New node, add to tracking
    if (authenticatedCount < 32) {
        authenticatedNodes[authenticatedCount] = nodeId;
        lastSequenceNumbers[authenticatedCount] = sequenceNumber;
        authenticatedCount++;
        return true;
    }
    
    ESP_LOGW(MESH_SEC_TAG, "Too many authenticated nodes, cannot track sequence for 0x%04X", nodeId);
    return true; // Allow if tracking full
}

uint32_t MeshSecurityService::getNextSequenceNumber() {
    return ++sequenceCounter;
}

void MeshSecurityService::deriveEncryptionKey(uint8_t* derivedKey, const uint8_t* masterKey, const uint8_t* nonce) {
    // Use HKDF to derive session key from master key and nonce
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    // Simple key derivation if HKDF not available
    if (!md_info || mbedtls_md_setup(&md_ctx, md_info, 1) != 0) {
        // Fallback: XOR master key with nonce (repeated)
        for (int i = 0; i < MESH_NETKEY_SIZE; i++) {
            derivedKey[i] = masterKey[i] ^ nonce[i % MESH_NONCE_SIZE];
        }
        return;
    }
    
    // Proper HKDF-like derivation
    mbedtls_md_hmac_starts(&md_ctx, masterKey, MESH_NETKEY_SIZE);
    mbedtls_md_hmac_update(&md_ctx, nonce, MESH_NONCE_SIZE);
    mbedtls_md_hmac_update(&md_ctx, (const uint8_t*)"MeshKey", 7);
    
    uint8_t hash[32];
    mbedtls_md_hmac_finish(&md_ctx, hash);
    mbedtls_md_free(&md_ctx);
    
    memcpy(derivedKey, hash, MESH_NETKEY_SIZE);
}

const MeshSecurityConfig& MeshSecurityService::getConfig() {
    return config;
}

bool MeshSecurityService::isNodeAuthenticated(uint16_t nodeId) {
    if (!config.enableAuthentication) {
        return true; // Authentication disabled
    }
    
    for (int i = 0; i < authenticatedCount && i < 32; i++) {
        if (authenticatedNodes[i] == nodeId) {
            return true;
        }
    }
    return false;
}

// Additional authentication methods would be implemented here
// For brevity, keeping the basic structure