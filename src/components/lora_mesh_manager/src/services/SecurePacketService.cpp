#include "secure_packet.h"
#include "mesh_security.h"
#include "esp_log.h"

static const char* SECURE_PKT_TAG = "SecurePacket";

// Static sequence number counter (temporary solution)
static uint32_t s_sequenceCounter = 0;

static uint32_t getNextSequenceNumber() {
    return ++s_sequenceCounter;
}

SecureDataPacket* SecurePacketService::wrapPacket(const DataPacket* originalPacket, 
                                                  size_t originalSize,
                                                  uint8_t securityLevel) {
    if (!originalPacket || originalSize == 0) {
        ESP_LOGE(SECURE_PKT_TAG, "Invalid parameters for packet wrapping");
        return nullptr;
    }
    
    // Calculate secure packet size
    size_t secureSize = SecureDataPacket::calculateSecurePacketSize(originalSize, securityLevel);
    
    // Allocate secure packet
    SecureDataPacket* securePacket = SecurePacketFactory::allocateSecurePacket(secureSize);
    if (!securePacket) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate secure packet");
        return nullptr;
    }
    
    // Copy original header
    copyPacketHeader(originalPacket, &securePacket->header.originalHeader);
    
    // Set up security header
    securePacket->header.securityLevel = securityLevel;
    securePacket->header.originalPayloadSize = originalSize - sizeof(PacketHeader);
    securePacket->header.encryptionFlags = ENCRYPTION_AES128_CBC | AUTH_HMAC_SHA256;
    
    // Initialize security header
    securePacket->header.securityHeader.securityType = SECURE_DATA_PACKET;
    securePacket->header.securityHeader.flags = 0;
    securePacket->header.securityHeader.sequenceNumber = getNextSequenceNumber();
    
    // Generate nonce
    MeshSecurityService::generateNonce(securePacket->header.securityHeader.nonce);
    
    // Set security flags
    if (IS_PACKET_ENCRYPTED(securityLevel)) {
        securePacket->header.securityHeader.flags |= SEC_FLAG_ENCRYPTED;
    }
    if (IS_PACKET_AUTHENTICATED(securityLevel)) {
        securePacket->header.securityHeader.flags |= SEC_FLAG_AUTHENTICATED;
    }
    
    // Update packet type to secure version
    securePacket->header.originalHeader.type = makeSecureType(originalPacket->type);
    
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
    
    // Update packet size
    securePacket->header.originalHeader.packetSize = secureSize;
    
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
    
    // Allocate original packet
    DataPacket* originalPacket = (DataPacket*)pvPortMalloc(*originalSize);
    if (!originalPacket) {
        ESP_LOGE(SECURE_PKT_TAG, "Failed to allocate original packet");
        return nullptr;
    }
    
    // Restore original header  
    copyPacketHeader(&securePacket->header.originalHeader, originalPacket);
    originalPacket->type = getOriginalType(originalPacket->type);
    originalPacket->packetSize = *originalSize;
    
    // Verify authentication if enabled
    if (IS_PACKET_AUTHENTICATED(securityLevel)) {
        if (verifyPacketAuthentication(securePacket, securePacket->header.originalHeader.packetSize) != MESH_SEC_OK) {
            ESP_LOGW(SECURE_PKT_TAG, "Packet authentication failed");
            vPortFree(originalPacket);
            return nullptr;
        }
    }
    
    // Check sequence number for replay protection
    if (MeshSecurityService::getConfig().enableReplayProtection) {
        if (!MeshSecurityService::isValidSequenceNumber(
                securePacket->header.originalHeader.src,
                securePacket->header.securityHeader.sequenceNumber)) {
            ESP_LOGW(SECURE_PKT_TAG, "Packet rejected due to replay protection");
            vPortFree(originalPacket);
            return nullptr;
        }
    }
    
    // Decrypt payload if needed
    if (IS_PACKET_ENCRYPTED(securityLevel)) {
        size_t decryptedSize = payloadSize;
        if (decryptPacketPayload(securePacket, originalPacket->payload, &decryptedSize) != MESH_SEC_OK) {
            ESP_LOGW(SECURE_PKT_TAG, "Packet decryption failed");
            vPortFree(originalPacket);
            return nullptr;
        }
        
        if (decryptedSize != payloadSize) {
            ESP_LOGW(SECURE_PKT_TAG, "Decrypted size mismatch: expected %zu, got %zu", 
                     payloadSize, decryptedSize);
            vPortFree(originalPacket);
            return nullptr;
        }
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
    
    // Generate MAC for entire packet except MAC field itself
    const uint8_t* packetData = (const uint8_t*)packet;
    size_t dataSize = totalSize - MESH_MAC_SIZE;
    
    return MeshSecurityService::generateMAC(
        packetData, dataSize,
        packet->header.securityHeader.mac,
        MeshSecurityService::getConfig().networkKey);
}

MeshSecurityResult SecurePacketService::verifyPacketAuthentication(const SecureDataPacket* packet,
                                                                 size_t totalSize) {
    if (!packet || totalSize == 0) {
        return MESH_SEC_INVALID_KEY;
    }
    
    // Verify MAC for entire packet except MAC field itself
    const uint8_t* packetData = (const uint8_t*)packet;
    size_t dataSize = totalSize - MESH_MAC_SIZE;
    
    return MeshSecurityService::verifyMAC(
        packetData, dataSize,
        packet->header.securityHeader.mac,
        MeshSecurityService::getConfig().networkKey);
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