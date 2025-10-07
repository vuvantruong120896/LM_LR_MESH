#include "secure_packet.h"
#include "mesh_security.h"
#include "PacketFactory.h"
#include "esp_log.h"

static const char* SECURE_PKT_TAG = "SecurePacket";

// Static sequence number counter (temporary solution)
// Use MeshSecurityService sequence generator for global monotonic sequence numbers

SecureDataPacket* SecurePacketService::wrapPacket(const DataPacket* originalPacket, 
                                                  size_t originalSize,
                                                  uint8_t securityLevel) {
    if (!originalPacket || originalSize == 0) {
        ESP_LOGE(SECURE_PKT_TAG, "Invalid parameters for packet wrapping");
        return nullptr;
    }
    
    // Calculate secure packet size
    size_t secureSize = SecureDataPacket::calculateSecurePacketSize(originalSize, securityLevel);
    
    // CRITICAL FIX: Validate size BEFORE allocation to prevent buffer overflow
    size_t maxSize = PacketFactory::getMaxPacketSize();
    if (secureSize > maxSize) {
        ESP_LOGE(SECURE_PKT_TAG, 
                 "Secure packet size (%zu bytes) exceeds maximum allowed (%zu bytes). "
                 "Original size: %zu, Security level: %d, Overhead: %zu bytes",
                 secureSize, maxSize, originalSize, securityLevel, 
                 secureSize - originalSize);
        ESP_LOGE(SECURE_PKT_TAG, "Cannot wrap packet - payload too large for secure transmission");
        return nullptr;
    }
    
    // Allocate secure packet
    SecureDataPacket* securePacket = SecurePacketFactory::allocateSecurePacket(secureSize);
    if (!securePacket) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate secure packet (free heap: %d)", 
                 esp_get_free_heap_size());
        return nullptr;
    }
    
    // Copy original header
    copyPacketHeader(originalPacket, &securePacket->header.originalHeader);
    
    // CRITICAL FIX: Copy via field for multi-hop routing
    // originalPacket is DataPacket which inherits from RouteDataPacket (has via field)
    securePacket->header.via = originalPacket->via;
    
    // Set up security header
    securePacket->header.securityLevel = securityLevel;
    securePacket->header.originalPayloadSize = originalSize - sizeof(PacketHeader);
    securePacket->header.encryptionFlags = ENCRYPTION_AES128_CBC | AUTH_HMAC_SHA256;
    
    // Initialize security header
    securePacket->header.securityHeader.securityType = SECURE_DATA_PACKET;
    securePacket->header.securityHeader.flags = 0;
    securePacket->header.securityHeader.sequenceNumber = MeshSecurityService::getNextSequenceNumber();
    
    // Generate nonce
    MeshSecurityService::generateNonce(securePacket->header.securityHeader.nonce);
    
    // Set security flags
    if (IS_PACKET_ENCRYPTED(securityLevel)) {
        securePacket->header.securityHeader.flags |= SEC_FLAG_ENCRYPTED;
    }
    if (IS_PACKET_AUTHENTICATED(securityLevel)) {
        securePacket->header.securityHeader.flags |= SEC_FLAG_AUTHENTICATED;
    }
    
    // Update packet type to secure version and set all header fields before MAC calculation
    securePacket->header.originalHeader.type = makeSecureType(originalPacket->type);
    securePacket->header.originalHeader.packetSize = secureSize;  // Set size early
    // Keep original packet ID unchanged for secure packets
    
    // Copy and process payload
    const uint8_t* originalPayload = originalPacket->payload;
    // Calculate payload size: total size minus header structures
    size_t headerSize = sizeof(PacketHeader) + sizeof(uint16_t); // PacketHeader + via field from RouteDataPacket
    size_t payloadSize = originalSize - headerSize;
    
    if (IS_PACKET_ENCRYPTED(securityLevel)) {
        // Encrypt payload
        if (encryptPacketPayload(securePacket, originalPayload, payloadSize) != MESH_SEC_OK) {
            delete securePacket;
            return nullptr;
        }
    } else {
        // Copy plaintext payload
        memcpy(securePacket->payload, originalPayload, payloadSize);
    }
    
    // Add authentication if needed
    if (IS_PACKET_AUTHENTICATED(securityLevel)) {
        if (authenticatePacket(securePacket, secureSize) != MESH_SEC_OK) {
            delete securePacket;
            return nullptr;
        }
    }
    
    ESP_LOGD(SECURE_PKT_TAG, "Packet wrapped - Original: %zu bytes, Secure: %zu bytes, Level: %d",
             originalSize, secureSize, securityLevel);
    
    return securePacket;
}

DataPacket* SecurePacketService::unwrapPacket(const SecureDataPacket* securePacket,
                                             size_t* originalSize) {
    if (!securePacket || !originalSize) {
        ESP_LOGE(SECURE_PKT_TAG, "Invalid parameters for packet unwrapping");
        return nullptr;
    }
    
    // Validate secure packet
    if (!validateSecurePacket(securePacket, securePacket->header.originalHeader.packetSize)) {
        ESP_LOGE(SECURE_PKT_TAG, "Invalid secure packet structure");
        return nullptr;
    }
    
    uint8_t securityLevel = securePacket->header.securityLevel;
    size_t payloadSize = securePacket->header.originalPayloadSize;
    
    // Calculate original packet size
    size_t headerSize = sizeof(PacketHeader) + sizeof(uint16_t); // PacketHeader + via field from RouteDataPacket
    *originalSize = headerSize + payloadSize;
    
    ESP_LOGV(SECURE_PKT_TAG, "Allocating original packet: header=%zu, payload=%zu, total=%zu", 
             headerSize, payloadSize, *originalSize);
    
    // Allocate original packet with extra safety margin
    size_t allocSize = *originalSize + 16; // Extra 16 bytes for safety
    DataPacket* originalPacket = (DataPacket*)pvPortMalloc(allocSize);
    if (!originalPacket) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate original packet (%zu bytes)", allocSize);
        return nullptr;
    }
    
    // Clear the entire allocated memory
    memset(originalPacket, 0, allocSize);
    
    // Restore original header  
    copyPacketHeader(&securePacket->header.originalHeader, originalPacket);
    originalPacket->type = getOriginalType(originalPacket->type);
    originalPacket->packetSize = *originalSize;
    
    // CRITICAL FIX: Restore via field for multi-hop routing
    // originalPacket is DataPacket which inherits from RouteDataPacket (has via field)
    originalPacket->via = securePacket->header.via;
    
    // Verify authentication if enabled
    if (IS_PACKET_AUTHENTICATED(securityLevel)) {
        MeshSecurityResult authResult = verifyPacketAuthentication(securePacket, securePacket->header.originalHeader.packetSize);
        if (authResult != MESH_SEC_OK) {
            ESP_LOGW(SECURE_PKT_TAG, "Packet authentication failed - Error code: %d", authResult);
            ESP_LOGW(SECURE_PKT_TAG, "Packet details - Src: 0x%04X, Dst: 0x%04X, Seq: %lu, Size: %d",
                     securePacket->header.originalHeader.src,
                     securePacket->header.originalHeader.dst,
                     securePacket->header.securityHeader.sequenceNumber,
                     securePacket->header.originalHeader.packetSize);
            
            // Log MAC for debugging
            ESP_LOGW(SECURE_PKT_TAG, "Received MAC: %02X%02X%02X%02X",
                     securePacket->header.securityHeader.mac[0],
                     securePacket->header.securityHeader.mac[1],
                     securePacket->header.securityHeader.mac[2],
                     securePacket->header.securityHeader.mac[3]);
            
            vPortFree(originalPacket);
            return nullptr;
        }
    }
    
    // Check sequence number for replay protection
    if (MeshSecurityService::getConfig().enableReplayProtection) {
        uint16_t sender = securePacket->header.originalHeader.src;
        uint32_t seq = securePacket->header.securityHeader.sequenceNumber;
        // Add debug info: if replay protection fails, log the incoming sequence and last seen value
        if (!MeshSecurityService::isValidSequenceNumber(
                sender,
                seq)) {
            // Try to obtain last seen value for sender for logging (MeshSecurityService tracks lastSequenceNumbers)
            // Since lastSequenceNumbers and authenticatedNodes are internal, use a diagnostic function if available.
            // Fallback: log only the incoming sequence.
            ESP_LOGW(SECURE_PKT_TAG, "Packet rejected due to replay protection - Src: 0x%04X, Seq: %lu", sender, seq);
            
            // Layer 2: Trigger resync if we keep getting replay rejections from a node
            // This will help the sender know their sequence is out of sync
            // Note: In practice, the sender should detect OUR rejections, not us detecting theirs
            // But this provides additional resilience
            
            vPortFree(originalPacket);
            return nullptr;
        }
    }
    
    // Decrypt payload if needed
    if (IS_PACKET_ENCRYPTED(securityLevel)) {
        // Calculate encrypted payload size (with AES padding)
        size_t encryptedSize = getEncryptedSize(payloadSize);
        size_t decryptedSize = encryptedSize; // Start with encrypted size for buffer
        
        ESP_LOGV(SECURE_PKT_TAG, "Decryption: payload=%zu, encrypted=%zu, buffer=%zu", 
                 payloadSize, encryptedSize, decryptedSize);
        
        if (decryptPacketPayload(securePacket, originalPacket->payload, &decryptedSize) != MESH_SEC_OK) {
            ESP_LOGW(SECURE_PKT_TAG, "Packet decryption failed");
            vPortFree(originalPacket);
            return nullptr;
        }
        
        // Validate decrypted size - should be <= original payload size
        if (decryptedSize > payloadSize) {
            ESP_LOGE(SECURE_PKT_TAG, "Decrypted size too large: %zu > %zu", decryptedSize, payloadSize);
            vPortFree(originalPacket);
            return nullptr;
        }
        
        // Update actual payload size to match decrypted size
        *originalSize = sizeof(PacketHeader) + sizeof(uint16_t) + decryptedSize;
        originalPacket->packetSize = *originalSize;
        
        ESP_LOGV(SECURE_PKT_TAG, "Decrypted size: %zu bytes, adjusted original size: %zu", 
                 decryptedSize, *originalSize);
    } else {
        // Copy plaintext payload
        memcpy(originalPacket->payload, securePacket->payload, payloadSize);
    }
    
    ESP_LOGD(SECURE_PKT_TAG, "Packet unwrapped - Secure: %zu bytes, Original: %zu bytes",
             securePacket->header.originalHeader.packetSize, *originalSize);
    
    return originalPacket;
}

MeshSecurityResult SecurePacketService::encryptPacketPayload(SecureDataPacket* packet,
                                                           const uint8_t* originalPayload,
                                                           size_t originalSize) {
    if (!packet || !originalPayload || originalSize == 0) {
        return MESH_SEC_INVALID_KEY;
    }
    
    size_t encryptedSize = getEncryptedSize(originalSize);
    
    return MeshSecurityService::encryptPayload(
        originalPayload, originalSize,
        packet->payload, &encryptedSize,
        packet->header.securityHeader.nonce);
}

MeshSecurityResult SecurePacketService::decryptPacketPayload(const SecureDataPacket* packet,
                                                           uint8_t* decryptedPayload,
                                                           size_t* decryptedSize) {
    if (!packet || !decryptedPayload || !decryptedSize) {
        return MESH_SEC_INVALID_KEY;
    }
    
    // Calculate encrypted payload size
    size_t originalPayloadSize = packet->header.originalPayloadSize;
    size_t encryptedSize = getEncryptedSize(originalPayloadSize);
    
    return MeshSecurityService::decryptPayload(
        packet->payload, encryptedSize,
        decryptedPayload, decryptedSize,
        packet->header.securityHeader.nonce);
}

MeshSecurityResult SecurePacketService::authenticatePacket(SecureDataPacket* packet,
                                                         size_t totalSize) {
    if (!packet || totalSize == 0) {
        return MESH_SEC_INVALID_KEY;
    }
    
    // CRITICAL: MAC must be calculated on data excluding MAC field itself
    
    // Find where MAC field is located in the packet structure
    size_t macOffset = offsetof(SecureDataPacket, header) + 
                       offsetof(SecurePacketHeader, securityHeader) + 
                       offsetof(SecurityPacketHeader, mac);
    
    ESP_LOGV(SECURE_PKT_TAG, "Generating MAC - offset: %zu, total size: %zu", macOffset, totalSize);
    
    // Split data into two parts: before MAC and after MAC
    const uint8_t* packetData = (const uint8_t*)packet;
    size_t beforeMacSize = macOffset;
    size_t afterMacSize = totalSize - macOffset - MESH_MAC_SIZE;
    
    // Log current network key being used (first 8 bytes for debugging)
    const MeshSecurityConfig& config = MeshSecurityService::getConfig();
    ESP_LOGD(SECURE_PKT_TAG, "Generating MAC with Network Key: %02X%02X%02X%02X%02X%02X%02X%02X...",
             config.networkKey[0], config.networkKey[1], config.networkKey[2], config.networkKey[3],
             config.networkKey[4], config.networkKey[5], config.networkKey[6], config.networkKey[7]);
    
    // Use a temporary buffer to concatenate data without MAC field
    uint8_t* tempBuffer = (uint8_t*)malloc(totalSize - MESH_MAC_SIZE);
    if (!tempBuffer) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate temporary buffer for MAC generation");
        return MESH_SEC_INVALID_KEY;
    }
    
    // Copy data before MAC field
    memcpy(tempBuffer, packetData, beforeMacSize);
    
    // Copy data after MAC field (if any)
    if (afterMacSize > 0) {
        memcpy(tempBuffer + beforeMacSize, packetData + macOffset + MESH_MAC_SIZE, afterMacSize);
    }
    
    size_t dataForMacSize = beforeMacSize + afterMacSize;
    
    ESP_LOGD(SECURE_PKT_TAG, "MAC generation data: %zu bytes", dataForMacSize);
    ESP_LOGD(SECURE_PKT_TAG, "MAC gen data (first 16 bytes): %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             tempBuffer[0], tempBuffer[1], tempBuffer[2], tempBuffer[3],
             tempBuffer[4], tempBuffer[5], tempBuffer[6], tempBuffer[7],
             tempBuffer[8], tempBuffer[9], tempBuffer[10], tempBuffer[11],
             tempBuffer[12], tempBuffer[13], tempBuffer[14], tempBuffer[15]);
    
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        tempBuffer, dataForMacSize,
        packet->header.securityHeader.mac,
        config.networkKey);
    
    free(tempBuffer); // Always free the buffer
    
    if (result == MESH_SEC_OK) {
        ESP_LOGD(SECURE_PKT_TAG, "Generated MAC: %02X%02X%02X%02X",
                 packet->header.securityHeader.mac[0],
                 packet->header.securityHeader.mac[1],
                 packet->header.securityHeader.mac[2],
                 packet->header.securityHeader.mac[3]);
    } else {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to generate MAC: %d", result);
    }
    
    return result;
}

MeshSecurityResult SecurePacketService::verifyPacketAuthentication(const SecureDataPacket* packet,
                                                                 size_t totalSize) {
    if (!packet || totalSize == 0) {
        ESP_LOGE(SECURE_PKT_TAG, "verifyPacketAuthentication: Invalid parameters");
        return MESH_SEC_INVALID_KEY;
    }
    
    // CRITICAL: MAC must be calculated on the EXACT same data as when generated
    // The MAC field itself should be excluded from calculation
    
    // Find where MAC field is located in the packet structure
    size_t macOffset = offsetof(SecureDataPacket, header) + 
                       offsetof(SecurePacketHeader, securityHeader) + 
                       offsetof(SecurityPacketHeader, mac);
    
    ESP_LOGV(SECURE_PKT_TAG, "MAC offset: %zu, Total size: %zu", macOffset, totalSize);
    
    // Split data into two parts: before MAC and after MAC
    const uint8_t* packetData = (const uint8_t*)packet;
    size_t beforeMacSize = macOffset;
    size_t afterMacSize = totalSize - macOffset - MESH_MAC_SIZE;
    
    ESP_LOGV(SECURE_PKT_TAG, "Before MAC: %zu bytes, After MAC: %zu bytes", beforeMacSize, afterMacSize);
    
    // Log current network key being used (first 8 bytes for debugging)
    const MeshSecurityConfig& config = MeshSecurityService::getConfig();
    ESP_LOGD(SECURE_PKT_TAG, "Using Network Key: %02X%02X%02X%02X%02X%02X%02X%02X...",
             config.networkKey[0], config.networkKey[1], config.networkKey[2], config.networkKey[3],
             config.networkKey[4], config.networkKey[5], config.networkKey[6], config.networkKey[7]);
    
    // Log packet structure for debugging
    ESP_LOGD(SECURE_PKT_TAG, "Packet header (first 16 bytes): %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             packetData[0], packetData[1], packetData[2], packetData[3],
             packetData[4], packetData[5], packetData[6], packetData[7],
             packetData[8], packetData[9], packetData[10], packetData[11],
             packetData[12], packetData[13], packetData[14], packetData[15]);
    
    // Generate MAC using HMAC over data excluding MAC field
    uint8_t expectedMac[MESH_MAC_SIZE];
    
    // Use a temporary buffer to concatenate data without MAC field
    uint8_t* tempBuffer = (uint8_t*)malloc(totalSize - MESH_MAC_SIZE);
    if (!tempBuffer) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate temporary buffer");
        return MESH_SEC_INVALID_KEY;
    }
    
    // Copy data before MAC field
    memcpy(tempBuffer, packetData, beforeMacSize);
    
    // Copy data after MAC field (if any)
    if (afterMacSize > 0) {
        memcpy(tempBuffer + beforeMacSize, packetData + macOffset + MESH_MAC_SIZE, afterMacSize);
    }
    
    size_t dataForMacSize = beforeMacSize + afterMacSize;
    
    ESP_LOGD(SECURE_PKT_TAG, "Data for MAC calculation: %zu bytes", dataForMacSize);
    ESP_LOGD(SECURE_PKT_TAG, "MAC calc data (first 16 bytes): %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             tempBuffer[0], tempBuffer[1], tempBuffer[2], tempBuffer[3],
             tempBuffer[4], tempBuffer[5], tempBuffer[6], tempBuffer[7],
             tempBuffer[8], tempBuffer[9], tempBuffer[10], tempBuffer[11],
             tempBuffer[12], tempBuffer[13], tempBuffer[14], tempBuffer[15]);
    
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        tempBuffer, dataForMacSize,
        expectedMac,
        config.networkKey);
    
    if (result != MESH_SEC_OK) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to generate MAC for verification: %d", result);
        free(tempBuffer);
        return result;
    }
    
    // Log MAC comparison for debugging
    ESP_LOGD(SECURE_PKT_TAG, "Expected MAC: %02X%02X%02X%02X",
             expectedMac[0], expectedMac[1], expectedMac[2], expectedMac[3]);
    ESP_LOGD(SECURE_PKT_TAG, "Received MAC: %02X%02X%02X%02X",
             packet->header.securityHeader.mac[0],
             packet->header.securityHeader.mac[1],
             packet->header.securityHeader.mac[2],
             packet->header.securityHeader.mac[3]);

    // Verify MAC using constant-time comparison
    result = MeshSecurityService::verifyMAC(
        tempBuffer, dataForMacSize,
        packet->header.securityHeader.mac,
        config.networkKey);
    
    free(tempBuffer);
    
#ifdef DEBUG_SKIP_MAC_VERIFICATION
    ESP_LOGD(SECURE_PKT_TAG, "DEBUG: Skipping MAC verification due to DEBUG_SKIP_MAC_VERIFICATION flag");
    result = MESH_SEC_OK;  // Force success for debugging
#endif
    
    if (result != MESH_SEC_OK) {
        ESP_LOGW(SECURE_PKT_TAG, "MAC verification failed: %d", result);
    } else {
        ESP_LOGV(SECURE_PKT_TAG, "MAC verification successful");
    }
    
    return result;
}

bool SecurePacketService::validateSecurePacket(const SecureDataPacket* packet, size_t packetSize) {
    if (!packet || packetSize < sizeof(SecurePacketHeader)) {
        return false;
    }
    
    // Check security level
    uint8_t secLevel = packet->header.securityLevel;
    if (secLevel > 2) {
        ESP_LOGW(SECURE_PKT_TAG, "Invalid security level: %d", secLevel);
        return false;
    }
    
    // Check packet type
    if (!isSecurePacket(packet->header.originalHeader.type)) {
        ESP_LOGW(SECURE_PKT_TAG, "Not a secure packet type: 0x%02X", 
                 packet->header.originalHeader.type);
        return false;
    }
    
    // Validate sizes
    size_t expectedSize = SecureDataPacket::calculateSecurePacketSize(
        packet->header.originalPayloadSize, secLevel);
    
    if (packetSize < sizeof(SecurePacketHeader)) {
        ESP_LOGW(SECURE_PKT_TAG, "Packet too small: %zu < %zu", 
                 packetSize, sizeof(SecurePacketHeader));
        return false;
    }
    
    return true;
}

// Helper methods implementation
void SecurePacketService::copyPacketHeader(const PacketHeader* src, PacketHeader* dst) {
    if (src && dst) {
        memcpy(dst, src, sizeof(PacketHeader));
    }
}

size_t SecurePacketService::getEncryptedSize(size_t originalSize) {
    // Round up to AES block size (16 bytes)
    return ((originalSize + 15) / 16) * 16;
}

void SecurePacketService::generatePacketNonce(uint8_t* nonce, uint16_t src, uint16_t dst, uint32_t sequence) {
    if (!nonce) return;
    
    // Create nonce from src, dst, and sequence
    memset(nonce, 0, MESH_NONCE_SIZE);
    
    nonce[0] = (src >> 8) & 0xFF;
    nonce[1] = src & 0xFF;
    nonce[2] = (dst >> 8) & 0xFF;
    nonce[3] = dst & 0xFF;
    nonce[4] = (sequence >> 24) & 0xFF;
    nonce[5] = (sequence >> 16) & 0xFF;
    nonce[6] = (sequence >> 8) & 0xFF;
    nonce[7] = sequence & 0xFF;
    
    // Add random bytes for remaining nonce
    esp_fill_random(nonce + 8, MESH_NONCE_SIZE - 8);
}

// SecurePacketFactory implementation
SecureDataPacket* SecurePacketFactory::allocateSecurePacket(size_t totalSize) {
    SecureDataPacket* packet = (SecureDataPacket*)pvPortMalloc(totalSize);
    if (packet) {
        memset(packet, 0, totalSize);
    }
    return packet;
}