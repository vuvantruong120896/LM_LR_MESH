#include "ProvisioningProtocol.h"
#include "../../include/mesh_security.h"
#include <esp_timer.h>
#include <esp_random.h>
#include <cstring>

const char* ProvisioningProtocol::TAG = "ProvProtocol";

bool ProvisioningProtocol::calculatePacketMAC(const uint8_t* packet, size_t packetSize,
                                            const uint8_t* key, uint8_t* mac) {
    if (!packet || !key || !mac || packetSize < PROVISIONING_MAC_SIZE) {
        return false;
    }
    
    // Use mesh security service for MAC calculation
    uint8_t fullMAC[16];
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        packet, packetSize - PROVISIONING_MAC_SIZE, fullMAC, key);
    
    if (result == MESH_SEC_OK) {
        // Use first 4 bytes of MAC for provisioning
        memcpy(mac, fullMAC, PROVISIONING_MAC_SIZE);
        return true;
    }
    
    return false;
}

bool ProvisioningProtocol::verifyPacketMAC(const uint8_t* packet, size_t packetSize,
                                         const uint8_t* key, const uint8_t* expectedMAC) {
    if (!packet || !key || !expectedMAC || packetSize < PROVISIONING_MAC_SIZE) {
        return false;
    }
    
    uint8_t calculatedMAC[PROVISIONING_MAC_SIZE];
    if (!calculatePacketMAC(packet, packetSize, key, calculatedMAC)) {
        return false;
    }
    
    return (memcmp(calculatedMAC, expectedMAC, PROVISIONING_MAC_SIZE) == 0);
}

void ProvisioningProtocol::generateChallenge(uint8_t* challenge) {
    if (!challenge) {
        return;
    }
    
    // Generate 16 bytes of random challenge using ESP32 hardware RNG
    for (int i = 0; i < PROVISIONING_CHALLENGE_SIZE; i += 4) {
        uint32_t random = esp_random();
        memcpy(&challenge[i], &random, 4);
    }
}

bool ProvisioningProtocol::generateChallengeResponse(const uint8_t* challenge,
                                                   const uint8_t* deviceUUID,
                                                   const uint8_t* authKey,
                                                   uint8_t* response) {
    if (!challenge || !deviceUUID || !authKey || !response) {
        return false;
    }
    
    // Create combined data: challenge + deviceUUID
    uint8_t combinedData[32];
    memcpy(combinedData, challenge, 16);
    memcpy(combinedData + 16, deviceUUID, 16);
    
    // Generate HMAC-based response
    uint8_t fullMAC[16];
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        combinedData, sizeof(combinedData), fullMAC, authKey);
    
    if (result == MESH_SEC_OK) {
        memcpy(response, fullMAC, 16);
        return true;
    }
    
    return false;
}

bool ProvisioningProtocol::verifyChallengeResponse(const uint8_t* challenge,
                                                 const uint8_t* deviceUUID,
                                                 const uint8_t* authKey,
                                                 const uint8_t* response) {
    if (!challenge || !deviceUUID || !authKey || !response) {
        return false;
    }
    
    uint8_t expectedResponse[16];
    if (!generateChallengeResponse(challenge, deviceUUID, authKey, expectedResponse)) {
        return false;
    }
    
    return (memcmp(expectedResponse, response, 16) == 0);
}

bool ProvisioningProtocol::createProvisionRequest(const uint8_t* deviceUUID,
                                                uint8_t deviceType,
                                                const char* deviceName,
                                                AuthMethod authMethod,
                                                ProvisionRequestPacket& packet) {
    if (!deviceUUID || !deviceName) {
        return false;
    }
    
    memset(&packet, 0, sizeof(ProvisionRequestPacket));
    
    packet.packetType = PROVISION_REQUEST_PACKET;
    memcpy(packet.deviceUUID, deviceUUID, 16);
    packet.deviceType = deviceType;
    packet.authMethod = authMethod;
    packet.requestedAddress = 0; // Any address
    packet.timestamp = esp_timer_get_time() / 1000000; // Current timestamp
    
    // Copy device name with bounds checking
    size_t nameLen = strlen(deviceName);
    if (nameLen >= sizeof(packet.deviceName)) {
        nameLen = sizeof(packet.deviceName) - 1;
    }
    memcpy(packet.deviceName, deviceName, nameLen);
    packet.deviceName[nameLen] = '\0';
    
    // Set device info (placeholder - could include firmware version, hardware type, etc.)
    snprintf((char*)packet.deviceInfo, sizeof(packet.deviceInfo), 
             "LM_Node_v1.0_%08X", (uint32_t)esp_random());
    
    // Calculate MAC (using device UUID as temporary key for request)
    if (!calculatePacketMAC((uint8_t*)&packet, sizeof(packet), deviceUUID, packet.mac)) {
        return false;
    }
    
    return true;
}

bool ProvisioningProtocol::createProvisionResponse(ProvisioningStatus status,
                                                 uint16_t assignedAddress,
                                                 const uint8_t* networkKey,
                                                 const uint8_t* authToken,
                                                 uint16_t networkId,
                                                 ProvisionResponsePacket& packet) {
    if (!networkKey || !authToken) {
        return false;
    }
    
    memset(&packet, 0, sizeof(ProvisionResponsePacket));
    
    packet.packetType = PROVISION_RESPONSE_PACKET;
    packet.status = status;
    packet.assignedAddress = assignedAddress;
    memcpy(packet.networkKey, networkKey, 16);
    memcpy(packet.authToken, authToken, 8);
    packet.networkId = networkId;
    packet.keyVersion = 1; // Default key version
    packet.provisionTime = esp_timer_get_time() / 1000000;
    packet.bridgeAddress = 0x0001; // Bridge address constant
    
    // Generate challenge for future authentication
    generateChallenge(packet.challenge);
    
    // Set network configuration (placeholder for additional settings)
    memset(packet.networkConfig, 0, sizeof(packet.networkConfig));
    packet.networkConfig[0] = 0x01; // Configuration version
    packet.networkConfig[1] = 0x02; // Security level
    
    // Calculate MAC using network key
    if (!calculatePacketMAC((uint8_t*)&packet, sizeof(packet), networkKey, packet.mac)) {
        return false;
    }
    
    return true;
}

bool ProvisioningProtocol::createProvisionComplete(uint16_t nodeAddress,
                                                  ProvisioningStatus status,
                                                  ProvisionCompletePacket& packet) {
    memset(&packet, 0, sizeof(ProvisionCompletePacket));
    
    packet.packetType = PROVISION_COMPLETE_PACKET;
    packet.nodeAddress = nodeAddress;
    packet.status = status;
    packet.timestamp = esp_timer_get_time() / 1000000;
    
    // Set device status information
    snprintf((char*)packet.deviceStatus, sizeof(packet.deviceStatus), 
             "JOINED_%08X", (uint32_t)esp_timer_get_time());
    
    // For complete packet, we'll use a simple checksum instead of full MAC
    // since the node might not have all keys established yet
    uint32_t checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - PROVISIONING_MAC_SIZE; i++) {
        checksum += data[i];
    }
    
    // Store checksum in MAC field (simple integrity check)
    memcpy(packet.mac, &checksum, PROVISIONING_MAC_SIZE);
    
    return true;
}

bool ProvisioningProtocol::createProvisionReject(ProvisioningStatus rejectReason,
                                                uint32_t retryAfter,
                                                ProvisionRejectPacket& packet) {
    memset(&packet, 0, sizeof(ProvisionRejectPacket));
    
    packet.packetType = PROVISION_REJECT_PACKET;
    packet.rejectReason = rejectReason;
    packet.retryAfter = retryAfter;
    
    // Set rejection information based on reason
    const char* rejectInfo = getStatusString(rejectReason);
    size_t infoLen = strlen(rejectInfo);
    if (infoLen >= sizeof(packet.additionalInfo)) {
        infoLen = sizeof(packet.additionalInfo) - 1;
    }
    memcpy(packet.additionalInfo, rejectInfo, infoLen);
    packet.additionalInfo[infoLen] = '\0';
    
    // Simple MAC for reject packet
    uint32_t checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - PROVISIONING_MAC_SIZE; i++) {
        checksum += data[i];
    }
    memcpy(packet.mac, &checksum, PROVISIONING_MAC_SIZE);
    
    return true;
}

const char* ProvisioningProtocol::getStatusString(ProvisioningStatus status) {
    switch (status) {
        case PROVISION_SUCCESS:
            return "Success";
        case PROVISION_NETWORK_FULL:
            return "Network full";
        case PROVISION_INVALID_REQUEST:
            return "Invalid request";
        case PROVISION_AUTH_FAILED:
            return "Authentication failed";
        case PROVISION_ADDRESS_CONFLICT:
            return "Address conflict";
        case PROVISION_BRIDGE_BUSY:
            return "Bridge busy";
        case PROVISION_TIMEOUT:
            return "Timeout";
        case PROVISION_INTERNAL_ERROR:
            return "Internal error";
        default:
            return "Unknown status";
    }
}

const char* ProvisioningProtocol::getPacketTypeName(uint8_t packetType) {
    switch (packetType) {
        case PROVISION_REQUEST_PACKET:
            return "PROVISION_REQUEST";
        case PROVISION_RESPONSE_PACKET:
            return "PROVISION_RESPONSE";
        case PROVISION_COMPLETE_PACKET:
            return "PROVISION_COMPLETE";
        case PROVISION_REJECT_PACKET:
            return "PROVISION_REJECT";
        default:
            return "UNKNOWN_PACKET";
    }
}