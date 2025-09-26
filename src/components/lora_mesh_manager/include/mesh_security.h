#ifndef _MESH_SECURITY_H
#define _MESH_SECURITY_H

#include <stdint.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_random.h"

// Security configuration constants
#define MESH_NETKEY_SIZE            16      // Network key size (AES-128)
#define MESH_AUTH_TOKEN_SIZE        8       // Authentication token size
#define MESH_CHALLENGE_SIZE         8       // Challenge size for authentication
#define MESH_MAC_SIZE               4       // Message Authentication Code size
#define MESH_NONCE_SIZE             12      // Nonce size for encryption
#define MESH_MAX_REPLAY_WINDOW      100     // Anti-replay window size

// Security packet types
#define SECURITY_JOIN_REQUEST       0xA1    // Join network request
#define SECURITY_JOIN_RESPONSE      0xA2    // Join network response  
#define SECURITY_AUTH_CHALLENGE     0xA3    // Authentication challenge
#define SECURITY_AUTH_RESPONSE      0xA4    // Authentication response
#define SECURITY_KEY_UPDATE         0xA5    // Network key update

// Security error codes
enum MeshSecurityResult {
    MESH_SEC_OK = 0,
    MESH_SEC_INVALID_KEY = -1,
    MESH_SEC_DECRYPT_FAILED = -2,
    MESH_SEC_AUTH_FAILED = -3,
    MESH_SEC_REPLAY_ATTACK = -4,
    MESH_SEC_INVALID_MAC = -5,
    MESH_SEC_NOT_AUTHENTICATED = -6
};

// Security configuration structure
struct MeshSecurityConfig {
    uint8_t networkKey[MESH_NETKEY_SIZE];           // Network encryption key
    uint8_t authToken[MESH_AUTH_TOKEN_SIZE];        // Network authentication token
    bool enableEncryption;                          // Enable payload encryption
    bool enableAuthentication;                      // Enable network authentication
    bool enableReplayProtection;                    // Enable anti-replay protection
    uint32_t keyRotationInterval;                   // Key rotation interval (seconds)
    uint16_t securityLevel;                         // Security level (0=none, 1=basic, 2=high)
    
    MeshSecurityConfig() : 
        enableEncryption(true),
        enableAuthentication(true), 
        enableReplayProtection(true),
        keyRotationInterval(3600),  // 1 hour
        securityLevel(2) {
        // Generate default network key and auth token
        esp_fill_random(networkKey, MESH_NETKEY_SIZE);
        esp_fill_random(authToken, MESH_AUTH_TOKEN_SIZE);
    }
};

// Security packet header
struct SecurityPacketHeader {
    uint8_t securityType;                           // Security packet type
    uint8_t flags;                                  // Security flags
    uint32_t sequenceNumber;                        // Sequence number for replay protection
    uint8_t nonce[MESH_NONCE_SIZE];                 // Nonce for encryption
    uint8_t mac[MESH_MAC_SIZE];                     // Message Authentication Code
};

// Join request packet
struct JoinRequestPacket {
    SecurityPacketHeader header;
    uint16_t nodeId;                                // Requesting node ID
    uint8_t challenge[MESH_CHALLENGE_SIZE];         // Random challenge
    uint8_t capabilities;                           // Node capabilities
    uint32_t timestamp;                             // Request timestamp
};

// Join response packet  
struct JoinResponsePacket {
    SecurityPacketHeader header;
    uint16_t nodeId;                                // Target node ID
    uint8_t response[MESH_CHALLENGE_SIZE];          // Challenge response
    uint8_t networkAuth[MESH_AUTH_TOKEN_SIZE];      // Network authentication
    bool accepted;                                  // Join accepted/rejected
    uint32_t sessionKey;                            // Temporary session key
};

// Security service interface
class MeshSecurityService {
public:
    // Initialize security service with configuration
    static bool initialize(const MeshSecurityConfig& config);
    
    // Encrypt payload data
    static MeshSecurityResult encryptPayload(const uint8_t* plaintext, size_t plaintextLen,
                                            uint8_t* ciphertext, size_t* ciphertextLen,
                                            const uint8_t* nonce);
    
    // Decrypt payload data
    static MeshSecurityResult decryptPayload(const uint8_t* ciphertext, size_t ciphertextLen,
                                            uint8_t* plaintext, size_t* plaintextLen,
                                            const uint8_t* nonce);
    
    // Generate Message Authentication Code
    static MeshSecurityResult generateMAC(const uint8_t* data, size_t dataLen,
                                         uint8_t* mac, const uint8_t* key);
    
    // Verify Message Authentication Code
    static MeshSecurityResult verifyMAC(const uint8_t* data, size_t dataLen,
                                       const uint8_t* mac, const uint8_t* key);
    
    // Handle join network request
    static bool handleJoinRequest(const JoinRequestPacket* request, JoinResponsePacket* response);
    
    // Process join response
    static bool processJoinResponse(const JoinResponsePacket* response);
    
    // Check if node is authenticated
    static bool isNodeAuthenticated(uint16_t nodeId);
    
    // Update network key
    static bool updateNetworkKey(const uint8_t* newKey);
    
    // Generate secure random nonce
    static void generateNonce(uint8_t* nonce);
    
    // Check for replay attacks
    static bool isValidSequenceNumber(uint16_t nodeId, uint32_t sequenceNumber);
    
    // Get current security configuration
    static const MeshSecurityConfig& getConfig();

private:
    static MeshSecurityConfig config;
    static mbedtls_aes_context aes_ctx;
    static bool initialized;
    static uint32_t sequenceCounter;
    
    // Authenticated nodes tracking
    static uint16_t authenticatedNodes[32];
    static uint8_t authenticatedCount;
    
    // Replay protection
    static uint32_t lastSequenceNumbers[32];
    
    // Internal helper methods
    static void deriveEncryptionKey(uint8_t* derivedKey, const uint8_t* masterKey, const uint8_t* nonce);
    static bool authenticateNode(uint16_t nodeId, const uint8_t* challenge, const uint8_t* response);
    static uint32_t getNextSequenceNumber();
};

// Security utility macros
#define MESH_SECURITY_ENABLED() (MeshSecurityService::getConfig().enableEncryption)
#define MESH_AUTH_ENABLED() (MeshSecurityService::getConfig().enableAuthentication)
#define MESH_REPLAY_PROTECTION_ENABLED() (MeshSecurityService::getConfig().enableReplayProtection)

// Security packet flags
#define SEC_FLAG_ENCRYPTED      0x01
#define SEC_FLAG_AUTHENTICATED  0x02
#define SEC_FLAG_KEY_ROTATION   0x04
#define SEC_FLAG_HIGH_PRIORITY  0x08

#endif // _MESH_SECURITY_H