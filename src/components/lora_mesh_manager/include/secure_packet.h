#ifndef _SECURE_PACKET_H
#define _SECURE_PACKET_H

#include "mesh_security.h"
#include "../src/network/entities/packets/PacketHeader.h"
#include "../src/network/entities/packets/DataPacket.h"

// Secure packet types
#define SECURE_DATA_PACKET      0x80
#define SECURE_HELLO_PACKET     0x81
#define SECURE_ROUTE_PACKET     0x82
#define SECURE_CONTROL_PACKET   0x83

#pragma pack(1)

// Secure packet header that wraps existing packets
struct SecurePacketHeader {
    // Original packet header
    PacketHeader originalHeader;
    
    // Security extensions
    SecurityPacketHeader securityHeader;
    
    // Security metadata
    uint8_t securityLevel;              // 0=none, 1=auth, 2=encrypt+auth
    uint16_t originalPayloadSize;       // Size before encryption/padding
    uint8_t encryptionFlags;            // Encryption algorithm and mode flags
    
    SecurePacketHeader() : securityLevel(0), originalPayloadSize(0), encryptionFlags(0) {
        memset(&securityHeader, 0, sizeof(securityHeader));
    }
};

// Secure data packet that enhances existing DataPacket
class SecureDataPacket {
public:
    SecurePacketHeader header;
    uint8_t payload[];                  // Encrypted payload + MAC (if enabled)
    
    // Get size of security overhead
    static size_t getSecurityOverhead(uint8_t securityLevel) {
        size_t overhead = sizeof(SecurityPacketHeader);
        
        if (securityLevel >= 1) {
            overhead += MESH_MAC_SIZE;  // Authentication
        }
        
        if (securityLevel >= 2) {
            // Encryption padding (worst case)
            overhead += 16;  // AES block size
        }
        
        return overhead;
    }
    
    // Calculate total packet size including security
    static size_t calculateSecurePacketSize(size_t originalPayloadSize, uint8_t securityLevel) {
        size_t baseSize = sizeof(SecurePacketHeader);
        size_t payloadSize = originalPayloadSize;
        
        // Add encryption padding if needed
        if (securityLevel >= 2) {
            payloadSize = ((originalPayloadSize + 15) / 16) * 16; // Round to AES block size
        }
        
        // Add MAC size if authentication enabled
        if (securityLevel >= 1) {
            payloadSize += MESH_MAC_SIZE;
        }
        
        return baseSize + payloadSize;
    }
    
    void operator delete(void* p) {
        ESP_LOGV("SecurePacket", "Deleting Secure packet");
        vPortFree(p);
    }
};

#pragma pack()

// Secure packet service for wrapping/unwrapping packets
class SecurePacketService {
public:
    // Wrap a regular packet with security
    static SecureDataPacket* wrapPacket(const DataPacket* originalPacket, 
                                       size_t originalSize,
                                       uint8_t securityLevel = 2);
    
    // Unwrap a secure packet to get original data
    static DataPacket* unwrapPacket(const SecureDataPacket* securePacket,
                                   size_t* originalSize);
    
    // Encrypt packet payload in-place
    static MeshSecurityResult encryptPacketPayload(SecureDataPacket* packet,
                                                  const uint8_t* originalPayload,
                                                  size_t originalSize);
    
    // Decrypt packet payload
    static MeshSecurityResult decryptPacketPayload(const SecureDataPacket* packet,
                                                  uint8_t* decryptedPayload,
                                                  size_t* decryptedSize);
    
    // Add authentication to packet
    static MeshSecurityResult authenticatePacket(SecureDataPacket* packet,
                                                size_t totalSize);
    
    // Verify packet authentication
    static MeshSecurityResult verifyPacketAuthentication(const SecureDataPacket* packet,
                                                        size_t totalSize);
    
    // Check if packet is secure
    static bool isSecurePacket(uint8_t packetType) {
        return (packetType & 0x80) != 0;
    }
    
    // Convert regular packet type to secure
    static uint8_t makeSecureType(uint8_t originalType) {
        return originalType | 0x80;
    }
    
    // Extract original packet type from secure type
    static uint8_t getOriginalType(uint8_t secureType) {
        return secureType & 0x7F;
    }
    
    // Validate secure packet structure
    static bool validateSecurePacket(const SecureDataPacket* packet, size_t packetSize);
    
    // Get security level from packet
    static uint8_t getSecurityLevel(const SecureDataPacket* packet) {
        return packet ? packet->header.securityLevel : 0;
    }
    
private:
    // Internal helper methods
    static void copyPacketHeader(const PacketHeader* src, PacketHeader* dst);
    static size_t getEncryptedSize(size_t originalSize);
    static void generatePacketNonce(uint8_t* nonce, uint16_t src, uint16_t dst, uint32_t sequence);
};

// Security packet factory for creating secure packets
class SecurePacketFactory {
public:
    // Create secure data packet
    template<typename T>
    static SecureDataPacket* createSecurePacket(uint16_t src, uint16_t dst,
                                               const T* payload, size_t payloadSize,
                                               uint8_t securityLevel = 2,
                                               uint8_t priority = DEFAULT_PRIORITY);
    
    // Create secure hello packet
    static SecureDataPacket* createSecureHelloPacket(uint16_t src, 
                                                    const uint8_t* helloData,
                                                    size_t helloSize);
    
    // Create secure route packet  
    static SecureDataPacket* createSecureRoutePacket(uint16_t src, uint16_t dst,
                                                    const uint8_t* routeData,
                                                    size_t routeSize);
    
    // Allocate secure packet with proper size
    static SecureDataPacket* allocateSecurePacket(size_t totalSize);
    
private:
    static void initializeSecureHeader(SecurePacketHeader* header,
                                      uint16_t src, uint16_t dst,
                                      uint8_t type, uint8_t securityLevel,
                                      size_t originalSize);
};

// Macros for security level checking
#define IS_PACKET_ENCRYPTED(level) ((level) >= 2)
#define IS_PACKET_AUTHENTICATED(level) ((level) >= 1)
#define IS_SECURE_PACKET(type) SecurePacketService::isSecurePacket(type)

// Security configuration flags
#define ENCRYPTION_AES128_CBC   0x01
#define ENCRYPTION_AES128_CTR   0x02
#define AUTH_HMAC_SHA256        0x10
#define REPLAY_PROTECTION       0x20

#endif // _SECURE_PACKET_H