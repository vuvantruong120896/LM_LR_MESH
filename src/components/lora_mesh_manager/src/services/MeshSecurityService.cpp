#include "mesh_security.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/hkdf.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "Arduino.h"
#include "esp_wifi.h"

static const char* MESH_SEC_TAG = "MeshSecurity";

// Static member initialization
MeshSecurityConfig MeshSecurityService::config;
mbedtls_aes_context MeshSecurityService::aes_ctx;
bool MeshSecurityService::initialized = false;
uint32_t MeshSecurityService::sequenceCounter = 0;
uint16_t MeshSecurityService::authenticatedNodes[32] = {0};
uint8_t MeshSecurityService::authenticatedCount = 0;
uint32_t MeshSecurityService::lastSequenceNumbers[32] = {0};
uint32_t MeshSecurityService::recentReceiveBitmap[32] = {0};

// Per-peer persistence tracking
uint32_t peerUpdatesSinceLastSave = 0;
uint32_t lastPeerSaveTime = 0;

// NVS persistence tracking
uint32_t MeshSecurityService::sequencesSinceLastSave = 0;
uint32_t MeshSecurityService::lastSaveTime = 0;
const uint32_t MeshSecurityService::SEQUENCE_SAVE_INTERVAL;
const uint32_t MeshSecurityService::TIME_SAVE_INTERVAL;
const uint32_t MeshSecurityService::PEER_SAVE_INTERVAL;
const uint32_t MeshSecurityService::PEER_SAVE_COUNT;

// Layer 2 resync tracking 
bool MeshSecurityService::resyncInProgress = false;
uint16_t MeshSecurityService::resyncTargetAddress = 0;
unsigned long MeshSecurityService::resyncRequestTime = 0;
uint8_t MeshSecurityService::resyncRetryCount = 0;
const uint8_t MeshSecurityService::MAX_RESYNC_RETRIES;
const unsigned long MeshSecurityService::RESYNC_TIMEOUT;

void MeshSecurityService::resetReplayState() {
    // Clear authenticated nodes tracking and last sequence numbers
    memset(authenticatedNodes, 0, sizeof(authenticatedNodes));
    memset(lastSequenceNumbers, 0, sizeof(lastSequenceNumbers));
    authenticatedCount = 0;
    
    // Save current sequence counter to NVS (important: preserve our sequence across replay resets)
    saveSequenceToNVS();
    
    ESP_LOGI(MESH_SEC_TAG, "Replay protection state reset, sequence preserved: %lu", sequenceCounter);
}

bool MeshSecurityService::initialize(const MeshSecurityConfig& cfg) {
    if (initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service already initialized");
        return true;
    }
    
    config = cfg;
    
    // Set initialized early so helper functions work
    initialized = true;
    
    // Initialize AES context
    mbedtls_aes_init(&aes_ctx);
    
    // Set encryption key
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, config.networkKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        ESP_LOGE(MESH_SEC_TAG, "Failed to set AES encryption key: %d", ret);
        return false;
    }
    
    // Initialize sequence counter: try to load from NVS first, fallback to deterministic base
    if (!loadSequenceFromNVS()) {
        // NVS load failed, generate deterministic sequence base from nodeId + networkKey
        sequenceCounter = generateDeterministicSequenceBase();
        ESP_LOGI(MESH_SEC_TAG, "NVS sequence load failed, starting from deterministic base: %lu", sequenceCounter);
        // Save initial deterministic base to NVS
        saveSequenceToNVS();
    }
    
    // Initialize NVS tracking for sequence and peer table
    sequencesSinceLastSave = 0;
    lastSaveTime = millis();
    peerUpdatesSinceLastSave = 0;
    lastPeerSaveTime = millis();

    // Load persisted per-peer table (if any)
    loadPeerTableFromNVS();
    
    ESP_LOGI(MESH_SEC_TAG, "Mesh security initialized - Encryption: %s, Auth: %s, Level: %d, Sequence: %lu",
             config.enableEncryption ? "ON" : "OFF",
             config.enableAuthentication ? "ON" : "OFF",
             config.securityLevel, sequenceCounter);
    
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
    // Sliding window size (allow small out-of-order packets)
    const uint32_t WINDOW = 32; // Accept sequences up to last + WINDOW

    // Find node in tracking array
    for (int i = 0; i < 32 && i < authenticatedCount; i++) {
        if (authenticatedNodes[i] == nodeId) {
            uint32_t last = lastSequenceNumbers[i];
            if (sequenceNumber > last) {
                uint32_t diff = sequenceNumber - last;
                if (diff < 32) {
                    // Shift bitmap and mark new bit
                    if (diff < 32) {
                        recentReceiveBitmap[i] <<= diff;
                        recentReceiveBitmap[i] |= 1u; // mark newest
                    } else {
                        recentReceiveBitmap[i] = 1u;
                    }
                    lastSequenceNumbers[i] = sequenceNumber;
                } else {
                    // Too far ahead => accept but reset bitmap
                    recentReceiveBitmap[i] = 1u;
                    lastSequenceNumbers[i] = sequenceNumber;
                }

                // Mark peer as updated for persistence
                peerUpdatesSinceLastSave++;
                uint32_t now = millis();
                if (peerUpdatesSinceLastSave >= PEER_SAVE_COUNT || (now - lastPeerSaveTime) >= PEER_SAVE_INTERVAL) {
                    savePeerTableToNVS();
                    peerUpdatesSinceLastSave = 0;
                    lastPeerSaveTime = now;
                }

                return true;
            }

            // sequenceNumber <= last => check bitmap for recent reception (out-of-order)
            uint32_t offset = last - sequenceNumber;
            if (offset < WINDOW) {
                // Check bit
                if ((recentReceiveBitmap[i] >> offset) & 0x1u) {
                    ESP_LOGW(MESH_SEC_TAG, "Replay attack detected (duplicate) from node 0x%04X: seq %lu <= last %lu",
                             nodeId, sequenceNumber, last);
                    return false; // duplicate
                } else {
                    // within window and not seen, mark bit
                    recentReceiveBitmap[i] |= (1u << offset);
                    // Persist with throttle
                    peerUpdatesSinceLastSave++;
                    uint32_t now = millis();
                    if (peerUpdatesSinceLastSave >= PEER_SAVE_COUNT || (now - lastPeerSaveTime) >= PEER_SAVE_INTERVAL) {
                        savePeerTableToNVS();
                        peerUpdatesSinceLastSave = 0;
                        lastPeerSaveTime = now;
                    }
                    return true;
                }
            }

            ESP_LOGW(MESH_SEC_TAG, "Replay attack detected from node 0x%04X: seq %lu <= last %lu (out of window)",
                     nodeId, sequenceNumber, last);
            return false;
        }
    }

    // New node, add to tracking
    if (authenticatedCount < 32) {
        authenticatedNodes[authenticatedCount] = nodeId;
        lastSequenceNumbers[authenticatedCount] = sequenceNumber;
        recentReceiveBitmap[authenticatedCount] = 1u; // mark present
        authenticatedCount++;

        // Persist peer table with throttle
        peerUpdatesSinceLastSave++;
        uint32_t now = millis();
        if (peerUpdatesSinceLastSave >= PEER_SAVE_COUNT || (now - lastPeerSaveTime) >= PEER_SAVE_INTERVAL) {
            savePeerTableToNVS();
            peerUpdatesSinceLastSave = 0;
            lastPeerSaveTime = now;
        }

        return true;
    }

    ESP_LOGW(MESH_SEC_TAG, "Too many authenticated nodes, cannot track sequence for 0x%04X", nodeId);
    return true; // Allow if tracking full
}

// Persist the per-peer table (authenticatedNodes, lastSequenceNumbers, recentReceiveBitmap)
bool MeshSecurityService::savePeerTableToNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("mesh_security", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for peer table save: %s", esp_err_to_name(err));
        return false;
    }

    // Prepare a compact blob: [count][entries...] where entry = nodeId(2) + seq(4) + bitmap(4)
    size_t entrySize = sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint32_t);
    size_t blobSize = sizeof(uint8_t) + (authenticatedCount * entrySize);
    uint8_t* blob = (uint8_t*)malloc(blobSize);
    if (!blob) {
        nvs_close(nvs_handle);
        ESP_LOGW(MESH_SEC_TAG, "Out of memory saving peer table");
        return false;
    }

    uint8_t* p = blob;
    *p++ = (uint8_t)authenticatedCount;
    for (int i = 0; i < authenticatedCount; i++) {
        uint16_t nid = authenticatedNodes[i];
        uint32_t seq = lastSequenceNumbers[i];
        uint32_t bm = recentReceiveBitmap[i];
        memcpy(p, &nid, sizeof(nid)); p += sizeof(nid);
        memcpy(p, &seq, sizeof(seq)); p += sizeof(seq);
        memcpy(p, &bm, sizeof(bm)); p += sizeof(bm);
    }

    err = nvs_set_blob(nvs_handle, "peer_table", blob, blobSize);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to write peer table to NVS: %s", esp_err_to_name(err));
        free(blob);
        nvs_close(nvs_handle);
        return false;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    free(blob);

    if (err == ESP_OK) {
        ESP_LOGD(MESH_SEC_TAG, "Peer table saved to NVS (%d entries)", authenticatedCount);
        return true;
    }
    ESP_LOGW(MESH_SEC_TAG, "Failed to commit peer table to NVS: %s", esp_err_to_name(err));
    return false;
}

bool MeshSecurityService::loadPeerTableFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("mesh_security", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for peer table load: %s", esp_err_to_name(err));
        return false;
    }

    // Read size first
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, "peer_table", NULL, &required_size);
    if (err != ESP_OK || required_size == 0) {
        nvs_close(nvs_handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(MESH_SEC_TAG, "No peer table in NVS (first run)");
            return true; // Not an error
        }
        ESP_LOGW(MESH_SEC_TAG, "Failed to query peer table size: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t* blob = (uint8_t*)malloc(required_size);
    if (!blob) {
        nvs_close(nvs_handle);
        ESP_LOGW(MESH_SEC_TAG, "Out of memory loading peer table");
        return false;
    }

    err = nvs_get_blob(nvs_handle, "peer_table", blob, &required_size);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        free(blob);
        ESP_LOGW(MESH_SEC_TAG, "Failed to read peer table from NVS: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t* p = blob;
    uint8_t count = *p++;
    if (count > 32) count = 32;
    authenticatedCount = count;
    for (int i = 0; i < authenticatedCount; i++) {
        uint16_t nid; memcpy(&nid, p, sizeof(nid)); p += sizeof(nid);
        uint32_t seq; memcpy(&seq, p, sizeof(seq)); p += sizeof(seq);
        uint32_t bm; memcpy(&bm, p, sizeof(bm)); p += sizeof(bm);
        authenticatedNodes[i] = nid;
        lastSequenceNumbers[i] = seq;
        recentReceiveBitmap[i] = bm;
    }

    free(blob);
    ESP_LOGI(MESH_SEC_TAG, "Loaded peer table from NVS (%d entries)", authenticatedCount);
    return true;
}

uint32_t MeshSecurityService::getNextSequenceNumber() {
    uint32_t nextSeq = ++sequenceCounter;
    
    // Auto-save sequence to NVS based on count or time
    sequencesSinceLastSave++;
    uint32_t currentTime = millis();
    
    if (sequencesSinceLastSave >= SEQUENCE_SAVE_INTERVAL || 
        (currentTime - lastSaveTime) >= TIME_SAVE_INTERVAL) {
        
        // Save to NVS (non-blocking, fire and forget)
        if (saveSequenceToNVS()) {
            sequencesSinceLastSave = 0;
            lastSaveTime = currentTime;
        }
    }
    
    return nextSeq;
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

bool MeshSecurityService::updateConfig(const MeshSecurityConfig& newConfig) {
    ESP_LOGI(MESH_SEC_TAG, "Updating security configuration");
    
    // Update the configuration
    config = newConfig;
    
    // Re-initialize AES context with new key
    mbedtls_aes_free(&aes_ctx);
    mbedtls_aes_init(&aes_ctx);
    
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, config.networkKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        ESP_LOGE(MESH_SEC_TAG, "Failed to set new AES encryption key: %d", ret);
        return false;
    }
    
    // Save sequence counter on config update (important security event)
    saveSequenceToNVS();
    
    ESP_LOGI(MESH_SEC_TAG, "Security configuration updated successfully");
    return true;
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

// ============================================================================
// NVS PERSISTENCE FOR SEQUENCE COUNTER (Layer 1 Replay Protection)
// ============================================================================

bool MeshSecurityService::loadSequenceFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS
    err = nvs_open("mesh_security", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for sequence read: %s", esp_err_to_name(err));
        return false;
    }
    
    // Read sequence counter
    uint32_t savedSequence = 0;
    size_t required_size = sizeof(savedSequence);
    err = nvs_get_blob(nvs_handle, "seq_counter", &savedSequence, &required_size);
    
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        // Add recovery offset to ensure we're always above the last saved value
        // This handles cases where we saved but sent more packets before crash
        sequenceCounter = savedSequence + 1000;
        ESP_LOGI(MESH_SEC_TAG, "NVS sequence loaded: %lu, starting from: %lu", 
                 savedSequence, sequenceCounter);
        return true;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(MESH_SEC_TAG, "No saved sequence found in NVS (first boot)");
    } else {
        ESP_LOGW(MESH_SEC_TAG, "Failed to read sequence from NVS: %s", esp_err_to_name(err));
    }
    
    return false;
}

bool MeshSecurityService::saveSequenceToNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS
    err = nvs_open("mesh_security", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for sequence save: %s", esp_err_to_name(err));
        return false;
    }
    
    // Save current sequence counter
    err = nvs_set_blob(nvs_handle, "seq_counter", &sequenceCounter, sizeof(sequenceCounter));
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to write sequence to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGD(MESH_SEC_TAG, "Sequence counter saved to NVS: %lu", sequenceCounter);
        return true;
    } else {
        ESP_LOGW(MESH_SEC_TAG, "Failed to commit sequence to NVS: %s", esp_err_to_name(err));
        return false;
    }
}

bool MeshSecurityService::clearSequenceFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // Open NVS
    err = nvs_open("mesh_security", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for sequence clear: %s", esp_err_to_name(err));
        return false;
    }
    
    // Erase sequence counter key
    err = nvs_erase_key(nvs_handle, "seq_counter");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to clear sequence from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(MESH_SEC_TAG, "Sequence counter cleared from NVS");
        return true;
    } else {
        ESP_LOGW(MESH_SEC_TAG, "Failed to commit sequence clear to NVS: %s", esp_err_to_name(err));
        return false;
    }
}

// ======================
// Layer 2: Resync Handshake Implementation
// ======================

bool MeshSecurityService::sendResyncRequest(uint16_t targetAddress, ResyncReasonCode reason) {
    if (!initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service not initialized");
        return false;
    }
    
    // Check if resync already in progress
    if (resyncInProgress && (millis() - resyncRequestTime) < RESYNC_TIMEOUT) {
        ESP_LOGW(MESH_SEC_TAG, "Resync already in progress with node %u", resyncTargetAddress);
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Sending resync request to node %u (reason: %d)", targetAddress, reason);
    
    // Create resync request packet
    ResyncRequestPacket request;
    request.header.securityType = SECURITY_RESYNC_REQUEST;
    request.header.flags = 0x00;
    request.header.sequenceNumber = getNextSequenceNumber();
    memset(request.header.nonce, 0, MESH_NONCE_SIZE);
    memset(request.header.mac, 0, MESH_MAC_SIZE);
    request.nodeId = targetAddress;  // Target node ID
    request.currentSequence = sequenceCounter; // Our current sequence
    request.timestamp = millis();
    request.reason = (uint8_t)reason;
    
    // TODO: Send packet via LoraMesher - will need integration with radio layer
    // For now, we'll track the state and return success
    
    // Update resync tracking state
    resyncInProgress = true;
    resyncTargetAddress = targetAddress;
    resyncRequestTime = millis();
    resyncRetryCount = 0;
    
    ESP_LOGD(MESH_SEC_TAG, "Resync request queued for node %u", targetAddress);
    return true;
}

bool MeshSecurityService::processResyncRequest(const ResyncRequestPacket* request, uint16_t senderAddress) {
    if (!initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service not initialized");
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Processing resync request from node %u (reason: %d, seq: %lu)", 
             senderAddress, request->reason, request->currentSequence);
    
    // Create resync response
    ResyncResponsePacket response;
    response.header.securityType = SECURITY_RESYNC_RESPONSE;
    response.header.flags = 0x00; 
    response.header.sequenceNumber = getNextSequenceNumber();
    memset(response.header.nonce, 0, MESH_NONCE_SIZE);
    memset(response.header.mac, 0, MESH_MAC_SIZE);
    response.nodeId = senderAddress;  // Target is the requester
    response.timestamp = millis();
    
    // Determine if we should accept the resync
    // Accept if: sender is authenticated OR reason is valid boot/corruption
    bool shouldAccept = isNodeAuthenticated(senderAddress) || 
                       (request->reason == RESYNC_REASON_BOOT) ||
                       (request->reason == RESYNC_REASON_NVS_CORRUPT);
    
    response.accepted = shouldAccept;
    
    if (shouldAccept) {
        // Set allowed sequence - sender can start from their current + margin
        response.allowedSequence = request->currentSequence;
        
        // Reset replay state for this node
        for (int i = 0; i < 32; i++) {
            if (authenticatedNodes[i] == senderAddress) {
                lastSequenceNumbers[i] = request->currentSequence - 1; // Allow their sequence
                ESP_LOGI(MESH_SEC_TAG, "Reset replay state for node %u, allowed seq: %lu", 
                         senderAddress, request->currentSequence);
                break;
            }
        }
        
        ESP_LOGI(MESH_SEC_TAG, "Resync accepted for node %u", senderAddress);
    } else {
        response.allowedSequence = 0;
        ESP_LOGW(MESH_SEC_TAG, "Resync rejected for node %u", senderAddress);
    }
    
    // TODO: Send response packet via LoraMesher - will need integration with radio layer
    
    return true;
}

bool MeshSecurityService::processResyncResponse(const ResyncResponsePacket* response, uint16_t senderAddress) {
    if (!initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service not initialized");
        return false;
    }
    
    // Check if this response is for our pending resync
    if (!resyncInProgress || resyncTargetAddress != senderAddress) {
        ESP_LOGW(MESH_SEC_TAG, "Unexpected resync response from node %u", senderAddress);
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Processing resync response from node %u (accepted: %s, seq: %lu)",
             senderAddress, response->accepted ? "YES" : "NO", response->allowedSequence);
    
    // Clear resync state
    resyncInProgress = false;
    resyncTargetAddress = 0;
    resyncRequestTime = 0;
    resyncRetryCount = 0;
    
    if (response->accepted) {
        // Resync accepted - we can now send packets without replay rejection
        ESP_LOGI(MESH_SEC_TAG, "Resync accepted by node %u, can resume normal operation", senderAddress);
        return true;
    } else {
        // Resync rejected - handle appropriately
        ESP_LOGW(MESH_SEC_TAG, "Resync rejected by node %u", senderAddress);
        return false;
    }
}

bool MeshSecurityService::handleReplayRejection(uint16_t targetAddress) {
    if (!initialized) {
        ESP_LOGW(MESH_SEC_TAG, "Security service not initialized");
        return false;
    }
    
    ESP_LOGW(MESH_SEC_TAG, "Packet rejected as replay by node %u - initiating resync", targetAddress);
    
    // Automatically trigger resync request when our packets are rejected
    return sendResyncRequest(targetAddress, RESYNC_REASON_REPLAY_REJECT);
}

// Generate deterministic sequence base from nodeId and network key
uint32_t MeshSecurityService::generateDeterministicSequenceBase() {
    // Get node ID from WiFi MAC or stored config (this should be actual node ID)
    // For now, derive a pseudo node ID from MAC or use a fixed approach
    uint16_t nodeId = getLocalNodeId();
    
    // Read boot count from NVS (increment each boot)
    uint32_t bootCount = getBootCountFromNVS();
    
    // Use network-wide epoch concept:
    // All nodes in same network share same epoch base, but different sequence ranges per node
    uint32_t networkEpoch = calculateNetworkEpoch(config.networkKey);
    
    // Each node gets a sequence range: epoch + (nodeId * RANGE_SIZE) + bootCount * BOOT_INCREMENT
    const uint32_t RANGE_SIZE = 1000000;  // 1M sequences per node
    const uint32_t BOOT_INCREMENT = 10000; // 10K sequences per boot
    
    uint32_t base = networkEpoch + (nodeId % 1000) * RANGE_SIZE + (bootCount % 100) * BOOT_INCREMENT;
    
    ESP_LOGI(MESH_SEC_TAG, "Generated deterministic sequence base: nodeId=0x%04X, epoch=%lu, boot=%lu, base=%lu", 
             nodeId, networkEpoch, bootCount, base);
    
    return base;
}

uint32_t MeshSecurityService::calculateNetworkEpoch(const uint8_t* networkKey) {
    // Generate network-wide epoch from network key hash
    uint8_t hash[32];
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info && mbedtls_md_setup(&md_ctx, md_info, 0) == 0) {
        mbedtls_md_starts(&md_ctx);
        mbedtls_md_update(&md_ctx, networkKey, MESH_NETKEY_SIZE);
        mbedtls_md_update(&md_ctx, (const uint8_t*)"MESH_EPOCH", 10);
        mbedtls_md_finish(&md_ctx, hash);
        mbedtls_md_free(&md_ctx);
    } else {
        // Fallback: use network key directly
        memcpy(hash, networkKey, MESH_NETKEY_SIZE);
        memset(hash + MESH_NETKEY_SIZE, 0xAA, 32 - MESH_NETKEY_SIZE);
    }
    
    // Convert to epoch in safe range (100M - 500M)
    uint32_t epoch = (hash[0] << 24) | (hash[1] << 16) | (hash[2] << 8) | hash[3];
    epoch = (epoch % 400000000) + 100000000;
    
    return epoch;
}

uint16_t MeshSecurityService::getLocalNodeId() {
    // This should get the actual node ID from WiFi MAC or configuration
    // For now, use a simple approach based on MAC
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    return (mac[4] << 8) | mac[5]; // Use last 2 bytes of MAC as node ID
}

uint32_t MeshSecurityService::getBootCountFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("mesh_security", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to open NVS for boot count: %s", esp_err_to_name(err));
        return 1; // Default boot count
    }
    
    uint32_t bootCount = 1;
    size_t required_size = sizeof(bootCount);
    err = nvs_get_blob(nvs_handle, "boot_count", &bootCount, &required_size);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        bootCount = 1; // First boot
    } else if (err != ESP_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Failed to read boot count: %s", esp_err_to_name(err));
        bootCount = 1;
    }
    
    // Increment and save boot count for next boot
    bootCount++;
    err = nvs_set_blob(nvs_handle, "boot_count", &bootCount, sizeof(bootCount));
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    
    ESP_LOGI(MESH_SEC_TAG, "Boot count: %lu", bootCount);
    return bootCount;
}