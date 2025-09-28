#include "NetkeyDistributionService.h"
#include "RoutingTableService.h"
#include "../../include/mesh_security.h"
#include "../../include/mesh_security_keys.h"
#include "mesh_security_config.h"
#include "../core/LoraMesher.h"

static const char* NETKEY_TAG = "NetkeyDistribution";

// Static member definitions
void (*NetkeyDistributionService::netkeyUpdateCallback)(const uint8_t* newKey, uint8_t version) = nullptr;
uint8_t NetkeyDistributionService::currentKeyVersion = 1;
uint32_t NetkeyDistributionService::lastUpdateTimestamp = 0;

void NetkeyDistributionService::initialize() {
    ESP_LOGI(NETKEY_TAG, "Netkey Distribution Service initialized");
    currentKeyVersion = MESH_KEY_VERSION;
    lastUpdateTimestamp = millis();
}

void NetkeyDistributionService::setNetkeyUpdateCallback(void (*callback)(const uint8_t* newKey, uint8_t version)) {
    netkeyUpdateCallback = callback;
    ESP_LOGI(NETKEY_TAG, "Netkey update callback registered");
}

bool NetkeyDistributionService::distributeNetworkKey(const uint8_t* networkKey, 
                                                    const uint8_t* authToken,
                                                    uint16_t networkId, 
                                                    uint8_t keyVersion) {
    if (!networkKey || !authToken) {
        ESP_LOGE(NETKEY_TAG, "Invalid parameters for network key distribution");
        return false;
    }
    
    ESP_LOGI(NETKEY_TAG, "Starting network key distribution - Version: %d, Network ID: 0x%04X", 
             keyVersion, networkId);
    
    // Log key for debugging (first 8 bytes)
    ESP_LOGD(NETKEY_TAG, "New key (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X...",
             networkKey[0], networkKey[1], networkKey[2], networkKey[3],
             networkKey[4], networkKey[5], networkKey[6], networkKey[7]);
    
    // First, update Bridge's own key
    if (!updateLocalNetworkKey(networkKey, authToken, networkId, keyVersion)) {
        ESP_LOGE(NETKEY_TAG, "Failed to update Bridge's own network key");
        return false;
    }
    
    // Get all nodes from routing table
    size_t nodeCount = RoutingTableService::routingTableSize();
    ESP_LOGI(NETKEY_TAG, "Distributing key to %zu nodes in routing table", nodeCount);
    
    if (nodeCount == 0) {
        ESP_LOGW(NETKEY_TAG, "No nodes in routing table - key updated only for Bridge");
        return true;
    }
    
    // Send key update to all nodes in routing table
    bool allSent = true;
    if (RoutingTableService::routingTableList->moveToStart()) {
        do {
            RouteNode* node = RoutingTableService::routingTableList->getCurrent();
            if (node && node->networkNode.address != 0x01) { // Skip Bridge itself
                ESP_LOGD(NETKEY_TAG, "Sending key update to node 0x%04X", node->networkNode.address);
                
                if (!sendNetkeyUpdate(node->networkNode.address, networkKey, authToken, 
                                    networkId, keyVersion)) {
                    ESP_LOGW(NETKEY_TAG, "Failed to send key update to node 0x%04X", 
                             node->networkNode.address);
                    allSent = false;
                }
            }
        } while (RoutingTableService::routingTableList->next());
    }
    
    // Update version and timestamp
    currentKeyVersion = keyVersion;
    lastUpdateTimestamp = millis();
    
    ESP_LOGI(NETKEY_TAG, "Network key distribution completed - Success: %s", 
             allSent ? "Full" : "Partial");
    
    return allSent;
}

bool NetkeyDistributionService::distributeNetkeyToAllNodes(const uint8_t* networkKey, 
                                                          const uint8_t* authToken,
                                                          uint16_t networkId, 
                                                          uint8_t keyVersion,
                                                          const NetworkNode* nodes,
                                                          size_t nodeCount) {
    if (!networkKey || !authToken || !nodes || nodeCount == 0) {
        ESP_LOGE(NETKEY_TAG, "Invalid parameters for netkey distribution to all nodes");
        return false;
    }
    
    ESP_LOGI(NETKEY_TAG, "*** SIMPLIFIED NETKEY DISTRIBUTION TO ALL NODES ***");
    ESP_LOGI(NETKEY_TAG, "Distributing key to %zu nodes - Version: %d, Network ID: 0x%04X", 
             nodeCount, keyVersion, networkId);
    
    // First, update Bridge's own key
    if (!updateLocalNetworkKey(networkKey, authToken, networkId, keyVersion)) {
        ESP_LOGE(NETKEY_TAG, "Failed to update Bridge's own network key");
        return false;
    }
    
    // Send key update to all provided nodes
    bool allSent = true;
    for (size_t i = 0; i < nodeCount; i++) {
        const NetworkNode* node = &nodes[i];
        if (node->address != 0x01 && node->address != 0x00) { // Skip Bridge itself and invalid addresses
            ESP_LOGI(NETKEY_TAG, "Sending netkey to node 0x%04X (hop %d)", 
                     node->address, node->metric);
            
            if (!sendNetkeyUpdate(node->address, networkKey, authToken, networkId, keyVersion)) {
                ESP_LOGW(NETKEY_TAG, "Failed to send netkey update to node 0x%04X", node->address);
                allSent = false;
            } else {
                ESP_LOGD(NETKEY_TAG, "Successfully sent netkey to node 0x%04X", node->address);
            }
        }
    }
    
    // Update version and timestamp
    currentKeyVersion = keyVersion;
    lastUpdateTimestamp = millis();
    
    ESP_LOGI(NETKEY_TAG, "Simplified netkey distribution completed - Success rate: %s", 
             allSent ? "100%" : "PARTIAL");
    
    return allSent;
}

bool NetkeyDistributionService::processNetkeyPacket(const uint8_t* packet, 
                                                   size_t packetSize,
                                                   uint16_t senderAddress) {
    if (!packet || packetSize < sizeof(PacketHeader) + 1) {
        return false;
    }
    
    const PacketHeader* header = (const PacketHeader*)packet;
    uint8_t netkeyType = packet[sizeof(PacketHeader)];
    
    ESP_LOGD(NETKEY_TAG, "Processing netkey packet type 0x%02X from 0x%04X", 
             netkeyType, senderAddress);
    
    switch (netkeyType) {
        case NETKEY_UPDATE_REQUEST: {
            if (packetSize < sizeof(NetkeyUpdatePacket)) {
                ESP_LOGW(NETKEY_TAG, "Netkey update packet too small");
                return false;
            }
            
            const NetkeyUpdatePacket* netkeyPacket = (const NetkeyUpdatePacket*)packet;
            
            // Validate packet
            if (!validateNetkeyPacket(netkeyPacket, senderAddress)) {
                ESP_LOGW(NETKEY_TAG, "Invalid netkey update packet from 0x%04X", senderAddress);
                sendNetkeyResponse(senderAddress, netkeyPacket->keyVersion, NETKEY_UPDATE_AUTH_FAILED);
                return false;
            }
            
            ESP_LOGI(NETKEY_TAG, "Received valid netkey update from 0x%04X - Version: %d", 
                     senderAddress, netkeyPacket->keyVersion);
            
            // Update local network key
            bool updateSuccess = updateLocalNetworkKey(netkeyPacket->networkKey,
                                                     netkeyPacket->authToken,
                                                     netkeyPacket->networkId,
                                                     netkeyPacket->keyVersion);
            
            // Send response
            NetkeyUpdateStatus status = updateSuccess ? NETKEY_UPDATE_SUCCESS : NETKEY_UPDATE_INTERNAL_ERROR;
            sendNetkeyResponse(senderAddress, netkeyPacket->keyVersion, status);
            
            // Call callback if registered
            if (updateSuccess && netkeyUpdateCallback) {
                netkeyUpdateCallback(netkeyPacket->networkKey, netkeyPacket->keyVersion);
            }
            
            return true;
        }
        
        case NETKEY_UPDATE_RESPONSE: {
            if (packetSize < sizeof(NetkeyResponsePacket)) {
                ESP_LOGW(NETKEY_TAG, "Netkey response packet too small");
                return false;
            }
            
            const NetkeyResponsePacket* responsePacket = (const NetkeyResponsePacket*)packet;
            
            ESP_LOGI(NETKEY_TAG, "Received netkey update response from 0x%04X - Version: %d, Status: %d", 
                     senderAddress, responsePacket->keyVersion, responsePacket->status);
            
            if (responsePacket->status == NETKEY_UPDATE_SUCCESS) {
                ESP_LOGD(NETKEY_TAG, "Node 0x%04X successfully updated to key version %d", 
                         senderAddress, responsePacket->keyVersion);
            } else {
                ESP_LOGW(NETKEY_TAG, "Node 0x%04X failed to update key - Status: %d", 
                         senderAddress, responsePacket->status);
            }
            
            return true;
        }
        
        default:
            ESP_LOGW(NETKEY_TAG, "Unknown netkey packet type: 0x%02X", netkeyType);
            return false;
    }
}

bool NetkeyDistributionService::isNetkeyPacket(uint8_t packetType) {
    return (packetType == NETKEY_UPDATE_REQUEST || 
            packetType == NETKEY_UPDATE_RESPONSE ||
            packetType == NETKEY_UPDATE_BROADCAST);
}

uint8_t NetkeyDistributionService::getCurrentKeyVersion() {
    return currentKeyVersion;
}

bool NetkeyDistributionService::updateLocalNetworkKey(const uint8_t* networkKey,
                                                     const uint8_t* authToken,
                                                     uint16_t networkId,
                                                     uint8_t keyVersion) {
    if (!networkKey || !authToken) {
        ESP_LOGE(NETKEY_TAG, "Invalid parameters for local key update");
        return false;
    }
    
    ESP_LOGI(NETKEY_TAG, "Updating local network key - Version: %d, Network ID: 0x%04X", 
             keyVersion, networkId);
    
    // Get current security config and create updated version
    MeshSecurityConfig newConfig = MeshSecurityService::getConfig();
    
    // Update network key and auth token
    memcpy(newConfig.networkKey, networkKey, 16);
    memcpy(newConfig.authToken, authToken, 8);
    // Note: networkId and keyVersion would need to be added to MeshSecurityConfig
    // For now, we'll store them in static variables in this service
    
    // Update the security service with new config
    if (!MeshSecurityService::updateConfig(newConfig)) {
        ESP_LOGE(NETKEY_TAG, "Failed to update security configuration");
        return false;
    }
    
    // Log updated key (first 8 bytes)
    ESP_LOGD(NETKEY_TAG, "Updated key (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X...",
             networkKey[0], networkKey[1], networkKey[2], networkKey[3],
             networkKey[4], networkKey[5], networkKey[6], networkKey[7]);
    
    // Update version tracking
    currentKeyVersion = keyVersion;
    lastUpdateTimestamp = millis();
    
    ESP_LOGI(NETKEY_TAG, "Local network key updated successfully");
    return true;
}

bool NetkeyDistributionService::sendNetkeyUpdate(uint16_t nodeAddress,
                                                const uint8_t* networkKey,
                                                const uint8_t* authToken,
                                                uint16_t networkId,
                                                uint8_t keyVersion) {
    NetkeyUpdatePacket packet;
    memset(&packet, 0, sizeof(packet));
    
    // Set header
    packet.header.dst = nodeAddress;
    packet.header.src = 0x01; // Bridge address
    packet.header.type = NETKEY_UPDATE_REQUEST;
    packet.header.id = 0; // Will be set by LoraMesher
    packet.header.packetSize = sizeof(NetkeyUpdatePacket);
    
    // Set netkey data
    packet.netkeyType = NETKEY_UPDATE_REQUEST;
    packet.keyVersion = keyVersion;
    packet.networkId = networkId;
    memcpy(packet.networkKey, networkKey, 16);
    memcpy(packet.authToken, authToken, 8);
    packet.timestamp = millis();
    
    // Generate MAC using Bootstrap Key for secure distribution
    if (!generateBootstrapMAC((uint8_t*)&packet, sizeof(packet) - 4, packet.mac)) {
        ESP_LOGE(NETKEY_TAG, "Failed to generate bootstrap MAC for netkey packet");
        return false;
    }
    
    // Send packet via LoraMesher
    LoraMesher& radio = LoraMesher::getInstance();
    
    ESP_LOGD(NETKEY_TAG, "Sending netkey update to 0x%04X", nodeAddress);
    return radio.sendPacket((uint8_t*)&packet, sizeof(packet)) == 0; // 0 = success
}

bool NetkeyDistributionService::sendNetkeyResponse(uint16_t bridgeAddress,
                                                  uint8_t keyVersion,
                                                  NetkeyUpdateStatus status) {
    NetkeyResponsePacket packet;
    memset(&packet, 0, sizeof(packet));
    
    // Set header
    packet.header.dst = bridgeAddress;
    packet.header.src = LoraMesher::getInstance().getLocalAddress();
    packet.header.type = NETKEY_UPDATE_RESPONSE;
    packet.header.id = 0; // Will be set by LoraMesher
    packet.header.packetSize = sizeof(NetkeyResponsePacket);
    
    // Set response data
    packet.netkeyType = NETKEY_UPDATE_RESPONSE;
    packet.keyVersion = keyVersion;
    packet.status = status;
    packet.timestamp = millis();
    
    // Generate MAC
    if (!generateNetkeyMAC((uint8_t*)&packet, sizeof(packet) - 4, packet.mac, true)) {
        ESP_LOGE(NETKEY_TAG, "Failed to generate MAC for response packet");
        return false;
    }
    
    // Send packet
    LoraMesher& radio = LoraMesher::getInstance();
    
    ESP_LOGD(NETKEY_TAG, "Sending netkey response to 0x%04X - Status: %d", bridgeAddress, status);
    return radio.sendPacket((uint8_t*)&packet, sizeof(packet)) == 0;
}

bool NetkeyDistributionService::validateNetkeyPacket(const NetkeyUpdatePacket* packet, 
                                                    uint16_t senderAddress) {
    if (!packet) return false;
    
    // Basic validation
    if (packet->netkeyType != NETKEY_UPDATE_REQUEST) {
        ESP_LOGW(NETKEY_TAG, "Invalid netkey packet type: 0x%02X", packet->netkeyType);
        return false;
    }
    
    // Check sender is Bridge (for nodes)
    if (senderAddress != 0x01) {
        ESP_LOGW(NETKEY_TAG, "Netkey update from non-bridge address: 0x%04X", senderAddress);
        return false;
    }
    
    // **SECURE BOOTSTRAP**: Validate MAC using pre-shared bootstrap key
    // This ensures only authorized devices can distribute network keys
    uint8_t expectedMac[4];
    if (!generateBootstrapMAC((uint8_t*)packet, sizeof(*packet) - 4, expectedMac)) {
        ESP_LOGE(NETKEY_TAG, "Failed to generate bootstrap MAC for validation");
        return false;
    }
    
    if (memcmp(packet->mac, expectedMac, 4) != 0) {
        ESP_LOGW(NETKEY_TAG, "Bootstrap MAC validation failed - unauthorized netkey distribution attempt");
        ESP_LOGD(NETKEY_TAG, "Expected MAC: %02X%02X%02X%02X", 
                 expectedMac[0], expectedMac[1], expectedMac[2], expectedMac[3]);
        ESP_LOGD(NETKEY_TAG, "Received MAC: %02X%02X%02X%02X", 
                 packet->mac[0], packet->mac[1], packet->mac[2], packet->mac[3]);
        return false;
    }
    
    ESP_LOGI(NETKEY_TAG, "Bootstrap MAC validation successful - netkey distribution authorized");
    
    ESP_LOGD(NETKEY_TAG, "Netkey packet validation successful");
    return true;
}

bool NetkeyDistributionService::generateNetkeyMAC(const uint8_t* data, size_t dataSize, 
                                                  uint8_t* mac, bool useCurrentKey) {
    if (!data || !mac || dataSize == 0) {
        return false;
    }
    
    const MeshSecurityConfig& config = MeshSecurityService::getConfig();
    
    // Generate MAC using current network key
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        data, dataSize, mac, config.networkKey);
    
    return (result == MESH_SEC_OK);
}

/**
 * @brief Generate MAC using Bootstrap Key for secure netkey distribution
 * @param data Data to generate MAC for
 * @param dataSize Size of data
 * @param mac Output MAC (4 bytes)
 * @return true if MAC generated successfully
 */
bool NetkeyDistributionService::generateBootstrapMAC(const uint8_t* data, size_t dataSize, uint8_t* mac) {
    if (!data || !mac || dataSize == 0) {
        return false;
    }
    
    // Use pre-shared bootstrap key for MAC generation
    static const uint8_t bootstrapKey[] = MESH_BOOTSTRAP_KEY;
    
    // Generate MAC using bootstrap key
    MeshSecurityResult result = MeshSecurityService::generateMAC(
        data, dataSize, mac, bootstrapKey);
    
    return (result == MESH_SEC_OK);
}