#include "ProvisioningService.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"
#include <esp_timer.h>
#include <esp_random.h>
#include <cstring>

// Static member initialization
bool ProvisioningService::initialized = false;
const char* ProvisioningService::TAG = "ProvSvc";
ProvisioningService::NodePacketCallback ProvisioningService::nodePacketCallback = nullptr;
ProvisioningSession ProvisioningService::sessions[MAX_PROVISIONING_SESSIONS];
uint8_t ProvisioningService::sessionCount = 0;
ProvisioningService::ProvisioningStats ProvisioningService::stats = {0};
uint8_t ProvisioningService::maxConcurrentSessions = MAX_PROVISIONING_SESSIONS;
uint32_t ProvisioningService::provisioningTimeoutSeconds = PROVISIONING_TIMEOUT_SECONDS;
AuthMethod ProvisioningService::authenticationMethod = AUTH_METHOD_CHALLENGE;
bool ProvisioningService::autoAuthorizationEnabled = true;

bool ProvisioningService::initialize() {
    if (initialized) {
        ESP_LOGI(TAG, "Provisioning Service already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Provisioning Service...");
    
    // Check dependencies
    if (!NVSStorageService::isInitialized()) {
        ESP_LOGE(TAG, "NVS Storage Service not initialized");
        return false;
    }
    
    if (!AddressManagementService::isInitialized()) {
        ESP_LOGE(TAG, "Address Management Service not initialized");
        return false;
    }
    
    // Initialize session array
    memset(sessions, 0, sizeof(sessions));
    sessionCount = 0;
    
    // Reset statistics
    memset(&stats, 0, sizeof(stats));
    
    initialized = true;
    
    ESP_LOGI(TAG, "Provisioning Service initialized successfully");
    ESP_LOGI(TAG, "Configuration: Max Sessions=%d, Timeout=%ds, Auth=%s, Auto-Auth=%s",
             maxConcurrentSessions, provisioningTimeoutSeconds,
             (authenticationMethod == AUTH_METHOD_OPEN) ? "OPEN" :
             (authenticationMethod == AUTH_METHOD_CHALLENGE) ? "CHALLENGE" : "PSK",
             autoAuthorizationEnabled ? "ENABLED" : "DISABLED");
    
    return true;
}

bool ProvisioningService::isInitialized() {
    return initialized;
}

void ProvisioningService::shutdown() {
    if (!initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Shutting down Provisioning Service...");
    
    clearAllSessions();
    initialized = false;
    
    ESP_LOGI(TAG, "Provisioning Service shutdown complete");
}

bool ProvisioningService::processProvisionRequest(const ProvisionRequestPacket& request, uint16_t senderAddress) {
    if (!initialized) {
        ESP_LOGE(TAG, "Service not initialized");
        return false;
    }
    
    logProvisioningEvent("PROVISION_REQUEST", request.deviceUUID, (char*)request.deviceName);
    stats.totalRequests++;
    
    // Validate request packet
    if (!validateProvisionRequest(request)) {
        ESP_LOGE(TAG, "Invalid provision request");
        updateStats("INVALID_REQUEST");
        sendProvisionReject(senderAddress, PROVISION_INVALID_REQUEST, 60);
        return false;
    }
    
    // // Check if bridge is in provisioning mode (must be explicitly started via UART)
    // if (!LoraMesher::getInstance().isProvisioningModeActive()) {
    //     ESP_LOGW(TAG, "Bridge not in provisioning mode - provision request ignored");
    //     ESP_LOGI(TAG, "Use UART_CMD_START_PROVISIONING to enable provisioning mode");
    //     updateStats("PROVISIONING_MODE_INACTIVE");
    //     sendProvisionReject(senderAddress, PROVISION_BRIDGE_BUSY, 300);
    //     return false;
    // }
    
    // Check if bridge is ready for provisioning (has network config)
    if (!isProvisioningReady()) {
        ESP_LOGW(TAG, "Bridge not configured for provisioning");
        updateStats("BRIDGE_NOT_READY");
        sendProvisionReject(senderAddress, PROVISION_INTERNAL_ERROR, 300);
        return false;
    }
    
    // Check if device is already provisioned
    if (isDeviceAlreadyProvisioned(request.deviceUUID)) {
        ESP_LOGI(TAG, "Device already provisioned, sending existing credentials");
        
        ProvisionedDevice existingDevice;
        if (AddressManagementService::findDeviceByUUID(request.deviceUUID, existingDevice)) {
            // Send credentials for already provisioned device
            ProvisioningSession* session = createSession(request.deviceUUID, existingDevice.address);
            if (session) {
                sendProvisionResponse(existingDevice.address, PROVISION_SUCCESS, 
                                    existingDevice.address, session);
                return true;
            }
        }
        
        sendProvisionReject(senderAddress, PROVISION_INVALID_REQUEST, 60);
        return false;
    }
    
    // Check session capacity
    if (getActiveSessionCount() >= maxConcurrentSessions) {
        ESP_LOGW(TAG, "Maximum concurrent sessions reached");
        updateStats("MAX_SESSIONS");
        sendProvisionReject(senderAddress, PROVISION_BRIDGE_BUSY, 120);
        return false;
    }
    
    // Create or find existing session
    ProvisioningSession* session = findSessionByUUID(request.deviceUUID);
    if (!session) {
        session = createSession(request.deviceUUID, senderAddress);
        if (!session) {
            ESP_LOGE(TAG, "Failed to create provisioning session");
            updateStats("SESSION_CREATION_FAILED");
            sendProvisionReject(senderAddress, PROVISION_INTERNAL_ERROR, 60);
            return false;
        }
    }
    
    // Authenticate request
    if (!authenticateRequest(request, session)) {
        ESP_LOGE(TAG, "Authentication failed for device");
        updateStats("AUTH_FAILED");
        stats.failedAuthentications++;
        
        session->attemptCount++;
        if (session->attemptCount >= MAX_AUTH_ATTEMPTS) {
            removeSession(session);
            sendProvisionReject(senderAddress, PROVISION_AUTH_FAILED, 3600); // 1 hour retry
        } else {
            sendProvisionReject(senderAddress, PROVISION_AUTH_FAILED, 30);
        }
        return false;
    }
    
    // Authorize device
    if (!authorizeDevice(request, request.deviceUUID)) {
        ESP_LOGW(TAG, "Device authorization failed");
        updateStats("AUTHORIZATION_FAILED");
        removeSession(session);
        sendProvisionReject(senderAddress, PROVISION_AUTH_FAILED, 3600);
        return false;
    }
    
    // Allocate address for device
    AddressManagementService::AllocationResult allocation = 
        AddressManagementService::allocateAddress(request.deviceUUID, request.deviceType, 
                                                 (char*)request.deviceName);
    
    if (!allocation.success) {
        ESP_LOGE(TAG, "Address allocation failed: %s", allocation.errorMessage);
        updateStats("ADDRESS_ALLOCATION_FAILED");
        removeSession(session);
        
        ProvisioningStatus rejectReason = PROVISION_INTERNAL_ERROR;
        if (strstr(allocation.errorMessage, "exhausted")) {
            rejectReason = PROVISION_NETWORK_FULL;
        }
        
        sendProvisionReject(senderAddress, rejectReason, 300);
        return false;
    }
    
    // Update session with allocated address
    session->nodeAddress = allocation.address;
    session->sessionState = SESSION_CREDENTIALS_SENT;
    session->lastActivity = esp_timer_get_time() / 1000000;
    
    // Send provisioning response with credentials
    bool responseSent = sendProvisionResponse(senderAddress, PROVISION_SUCCESS, 
                                            allocation.address, session);
    
    if (responseSent) {
        ESP_LOGI(TAG, "Provisioning response sent to device %s at address 0x%04X",
                 request.deviceName, allocation.address);
        updateStats("RESPONSE_SENT");
    } else {
        ESP_LOGE(TAG, "Failed to send provisioning response");
        updateStats("RESPONSE_SEND_FAILED");
        // Don't remove session yet, device might retry
    }
    
    return responseSent;
}

bool ProvisioningService::processProvisionComplete(const ProvisionCompletePacket& complete, uint16_t senderAddress) {
    if (!initialized) {
        return false;
    }
    
    logProvisioningEvent("PROVISION_COMPLETE", nullptr, "Address: 0x%04X");
    
    // Find session for this completion
    ProvisioningSession* session = findSessionByAddress(complete.nodeAddress);
    if (!session) {
        ESP_LOGW(TAG, "No active session for provision complete from 0x%04X", complete.nodeAddress);
        return false;
    }
    
    // Validate sender address matches session
    if (session->nodeAddress != complete.nodeAddress) {
        ESP_LOGW(TAG, "Address mismatch in provision complete");
        return false;
    }
    
    // Update device status to ACTIVE
    if (AddressManagementService::updateDeviceStatus(complete.nodeAddress, DEVICE_STATUS_ACTIVE)) {
        ESP_LOGI(TAG, "Device 0x%04X successfully joined network", complete.nodeAddress);
        session->sessionState = SESSION_COMPLETE;
        session->lastActivity = esp_timer_get_time() / 1000000;
        
        stats.successfulProvisions++;
        stats.lastProvisionTime = session->lastActivity;
        updateStats("PROVISION_SUCCESS");
        
        // Remove completed session after short delay
        // Could implement delayed cleanup instead of immediate removal
        removeSession(session);
        
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to update device status for 0x%04X", complete.nodeAddress);
        return false;
    }
}

void ProvisioningService::handleProvisioningTimeouts() {
    if (!initialized) {
        return;
    }
    
    uint32_t currentTime = esp_timer_get_time() / 1000000;
    
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        ProvisioningSession* session = &sessions[i];
        
        if (!session->isActive) {
            continue;
        }
        
        uint32_t sessionAge = currentTime - session->lastActivity;
        if (sessionAge > provisioningTimeoutSeconds) {
            ESP_LOGW(TAG, "Provisioning session timeout for device at 0x%04X", session->nodeAddress);
            
            // Release allocated address if session timed out after allocation
            if (session->sessionState >= SESSION_CREDENTIALS_SENT && session->nodeAddress != 0) {
                AddressManagementService::releaseAddress(session->nodeAddress);
            }
            
            removeSession(session);
            stats.timeoutSessions++;
            updateStats("SESSION_TIMEOUT");
        }
    }
}

uint8_t ProvisioningService::getActiveSessionCount() {
    if (!initialized) {
        return 0;
    }
    
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        if (sessions[i].isActive) {
            count++;
        }
    }
    return count;
}

ProvisioningSession* ProvisioningService::findSessionByUUID(const uint8_t* deviceUUID) {
    if (!initialized || !deviceUUID) {
        return nullptr;
    }
    
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        if (sessions[i].isActive && 
            memcmp(sessions[i].deviceUUID, deviceUUID, 16) == 0) {
            return &sessions[i];
        }
    }
    return nullptr;
}

ProvisioningSession* ProvisioningService::findSessionByAddress(uint16_t nodeAddress) {
    if (!initialized) {
        return nullptr;
    }
    
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        if (sessions[i].isActive && sessions[i].nodeAddress == nodeAddress) {
            return &sessions[i];
        }
    }
    return nullptr;
}

ProvisioningSession* ProvisioningService::createSession(const uint8_t* deviceUUID, uint16_t tempAddress) {
    if (!initialized || !deviceUUID) {
        return nullptr;
    }
    
    ProvisioningSession* session = findAvailableSession();
    if (!session) {
        ESP_LOGE(TAG, "No available session slots");
        return nullptr;
    }
    
    // Initialize session
    memset(session, 0, sizeof(ProvisioningSession));
    memcpy(session->deviceUUID, deviceUUID, 16);
    session->nodeAddress = tempAddress;
    session->sessionState = SESSION_CHALLENGE_SENT;
    session->sessionStart = esp_timer_get_time() / 1000000;
    session->lastActivity = session->sessionStart;
    session->attemptCount = 0;
    session->isActive = true;
    
    // Generate challenge for authentication
    ProvisioningProtocol::generateChallenge(session->challenge);
    
    sessionCount++;
    stats.activeSessions = getActiveSessionCount();
    
    ESP_LOGI(TAG, "Created provisioning session for device at 0x%04X", tempAddress);
    return session;
}

bool ProvisioningService::removeSession(ProvisioningSession* session) {
    if (!initialized || !session || !session->isActive) {
        return false;
    }
    
    ESP_LOGI(TAG, "Removing provisioning session for device at 0x%04X", session->nodeAddress);
    
    session->isActive = false;
    memset(session, 0, sizeof(ProvisioningSession));
    
    if (sessionCount > 0) {
        sessionCount--;
    }
    stats.activeSessions = getActiveSessionCount();
    
    return true;
}

void ProvisioningService::clearAllSessions() {
    ESP_LOGI(TAG, "Clearing all provisioning sessions");
    
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        if (sessions[i].isActive) {
            // Release allocated addresses
            if (sessions[i].nodeAddress != 0 && 
                sessions[i].sessionState >= SESSION_CREDENTIALS_SENT) {
                AddressManagementService::releaseAddress(sessions[i].nodeAddress);
            }
        }
    }
    
    memset(sessions, 0, sizeof(sessions));
    sessionCount = 0;
    stats.activeSessions = 0;
}

bool ProvisioningService::authenticateRequest(const ProvisionRequestPacket& request, ProvisioningSession* session) {
    if (!session) {
        return false;
    }
    
    switch (authenticationMethod) {
        case AUTH_METHOD_OPEN:
            // Open authentication - always pass
            return true;
            
        case AUTH_METHOD_CHALLENGE:
            // For first request, generate and send challenge
            if (session->sessionState == SESSION_IDLE) {
                session->sessionState = SESSION_CHALLENGE_SENT;
                return true;
            }
            
            // For subsequent requests, verify challenge response
            if (session->sessionState == SESSION_CHALLENGE_SENT) {
                // Get network credentials for challenge verification
                uint8_t networkKey[16], authToken[8];
                uint16_t networkId;
                uint8_t keyVersion;
                
                if (!getNetworkCredentials(networkKey, authToken, &networkId, &keyVersion)) {
                    return false;
                }
                
                // Verify challenge response using auth token as key
                return ProvisioningProtocol::verifyChallengeResponse(
                    session->challenge, request.deviceUUID, authToken, request.challengeResponse);
            }
            break;
            
        case AUTH_METHOD_PSK:
            // Pre-shared key authentication - would need device-specific keys
            // For now, fall back to open
            return true;
            
        default:
            return false;
    }
    
    return false;
}

bool ProvisioningService::authorizeDevice(const ProvisionRequestPacket& request, const uint8_t* deviceUUID) {
    if (autoAuthorizationEnabled) {
        // Auto-authorization enabled - check basic criteria
        
        // Verify device type is supported
        if (request.deviceType == 0 || request.deviceType > 0x1F) {
            ESP_LOGW(TAG, "Invalid device type: 0x%02X", request.deviceType);
            return false;
        }
        
        // Check if device name is valid
        if (strlen((char*)request.deviceName) == 0) {
            ESP_LOGW(TAG, "Empty device name");
            return false;
        }
        
        return true;
    } else {
        // Manual authorization required - would integrate with external system
        // For now, reject all manual authorizations
        ESP_LOGW(TAG, "Manual authorization not implemented");
        return false;
    }
}

// Implementation continues with remaining methods...
// [Content truncated for length - would include remaining methods like getNetworkCredentials, 
// isProvisioningReady, sendProvisionResponse, etc.]

bool ProvisioningService::getNetworkCredentials(uint8_t* networkKey, uint8_t* authToken,
                                              uint16_t* networkId, uint8_t* keyVersion) {
    if (!networkKey || !authToken || !networkId || !keyVersion) {
        return false;
    }
    
    NetworkConfig config;
    if (!NVSStorageService::loadNetworkConfig(config)) {
        return false;
    }
    
    memcpy(networkKey, config.networkKey, 16);
    memcpy(authToken, config.authToken, 8);
    *networkId = config.networkId;
    *keyVersion = config.keyVersion;
    
    return true;
}

bool ProvisioningService::isProvisioningReady() {
    return NVSStorageService::isNetworkConfigured();
}

bool ProvisioningService::sendProvisionResponse(uint16_t targetAddress, ProvisioningStatus status,
                                               uint16_t assignedAddress, ProvisioningSession* session) {
    if (!session) {
        return false;
    }
    
    ProvisionResponsePacket response;
    uint8_t networkKey[16], authToken[8];
    uint16_t networkId;
    uint8_t keyVersion;
    
    if (!getNetworkCredentials(networkKey, authToken, &networkId, &keyVersion)) {
        return false;
    }
    
    if (!ProvisioningProtocol::createProvisionResponse(status, assignedAddress, 
                                                       networkKey, authToken, networkId, response)) {
        return false;
    }
    
    return sendPacket(targetAddress, (uint8_t*)&response, sizeof(response));
}

bool ProvisioningService::sendProvisionReject(uint16_t targetAddress, ProvisioningStatus rejectReason,
                                             uint32_t retryAfter) {
    ProvisionRejectPacket reject;
    if (!ProvisioningProtocol::createProvisionReject(rejectReason, retryAfter, reject)) {
        return false;
    }
    
    return sendPacket(targetAddress, (uint8_t*)&reject, sizeof(reject));
}

bool ProvisioningService::isProvisioningPacket(uint8_t packetType) {
    return (packetType >= PROVISION_REQUEST_PACKET && packetType <= PROVISION_REJECT_PACKET);
}

bool ProvisioningService::processProvisioningPacket(const uint8_t* packet, size_t packetSize, uint16_t senderAddress) {
    if (!initialized || !packet || packetSize == 0) {
        return false;
    }
    
    uint8_t packetType = packet[0]; // First byte is packet type
    
    ESP_LOGI(TAG, "Processing provisioning packet type 0x%02X from 0x%04X", packetType, senderAddress);
    
    switch (packetType) {
        case PROVISION_REQUEST_PACKET: {
            if (packetSize < sizeof(ProvisionRequestPacket)) {
                ESP_LOGW(TAG, "Provision request packet too small");
                return false;
            }
            
            const ProvisionRequestPacket* request = reinterpret_cast<const ProvisionRequestPacket*>(packet);
            return processProvisionRequest(*request, senderAddress);
        }
        
        case PROVISION_COMPLETE_PACKET: {
            if (packetSize < sizeof(ProvisionCompletePacket)) {
                ESP_LOGW(TAG, "Provision complete packet too small");
                return false;
            }
            
            const ProvisionCompletePacket* complete = reinterpret_cast<const ProvisionCompletePacket*>(packet);
            return processProvisionComplete(*complete, senderAddress);
        }
        
        case PROVISION_RESPONSE_PACKET:
        case PROVISION_REJECT_PACKET:
            // These are sent by bridge, intended for node applications
            ESP_LOGD(TAG, "Forwarding provisioning packet type 0x%02X to node application", packetType);
            if (nodePacketCallback != nullptr) {
                nodePacketCallback(packetType, packet, packetSize, senderAddress);
                return true;
            } else {
                ESP_LOGW(TAG, "No node callback registered for provisioning packet type 0x%02X", packetType);
                return false;
            }
            
        default:
            ESP_LOGW(TAG, "Unknown provisioning packet type: 0x%02X", packetType);
            return false;
    }
}

bool ProvisioningService::sendPacket(uint16_t targetAddress, const uint8_t* packet, size_t packetSize) {
    // Get LoraMesher instance and send packet
    LoraMesher& radio = LoraMesher::getInstance();
    radio.sendPacket(targetAddress, packet, packetSize);
    return true; // Assume success since sendPacket is void
}

// Implement remaining private helper methods...
ProvisioningSession* ProvisioningService::findAvailableSession() {
    for (uint8_t i = 0; i < MAX_PROVISIONING_SESSIONS; i++) {
        if (!sessions[i].isActive) {
            return &sessions[i];
        }
    }
    return nullptr;
}

bool ProvisioningService::validateProvisionRequest(const ProvisionRequestPacket& request) {
    // Basic validation checks
    if (request.packetType != PROVISION_REQUEST_PACKET) {
        return false;
    }
    
    // Check timestamp (basic replay protection)
    uint32_t currentTime = esp_timer_get_time() / 1000000;
    if (abs((int32_t)(currentTime - request.timestamp)) > 300) { // 5 minute window
        ESP_LOGW(TAG, "Request timestamp out of acceptable range");
        return false;
    }
    
    return true;
}

bool ProvisioningService::isDeviceAlreadyProvisioned(const uint8_t* deviceUUID) {
    ProvisionedDevice device;
    return AddressManagementService::findDeviceByUUID(deviceUUID, device);
}

void ProvisioningService::logProvisioningEvent(const char* event, const uint8_t* deviceUUID, const char* details) {
    if (deviceUUID) {
        ESP_LOGI(TAG, "[%s] Device: %02X%02X%02X%02X... %s", 
                 event, deviceUUID[0], deviceUUID[1], deviceUUID[2], deviceUUID[3],
                 details ? details : "");
    } else {
        ESP_LOGI(TAG, "[%s] %s", event, details ? details : "");
    }
}

void ProvisioningService::updateStats(const char* event) {
    // Update relevant statistics based on event
    if (strcmp(event, "INVALID_REQUEST") == 0) {
        stats.rejectedRequests++;
    }
    // Add more stat updates as needed
}

// Configuration Management Methods

void ProvisioningService::setMaxConcurrentSessions(uint8_t maxSessions) {
    maxConcurrentSessions = maxSessions;
    ESP_LOGI(TAG, "Max concurrent sessions set to: %d", maxSessions);
}

void ProvisioningService::setProvisioningTimeout(uint32_t timeoutSeconds) {
    provisioningTimeoutSeconds = timeoutSeconds;
    ESP_LOGI(TAG, "Provisioning timeout set to: %d seconds", timeoutSeconds);
}

void ProvisioningService::setAuthenticationMethod(AuthMethod authMethod) {
    authenticationMethod = authMethod;
    const char* methodStr = (authMethod == AUTH_METHOD_OPEN) ? "OPEN" :
                           (authMethod == AUTH_METHOD_CHALLENGE) ? "CHALLENGE" : "PSK";
    ESP_LOGI(TAG, "Authentication method set to: %s", methodStr);
}

void ProvisioningService::setAutoAuthorization(bool enabled) {
    autoAuthorizationEnabled = enabled;
    ESP_LOGI(TAG, "Auto-authorization %s", enabled ? "enabled" : "disabled");
}

bool ProvisioningService::getProvisioningStats(ProvisioningStats& stats_out) {
    if (!initialized) {
        return false;
    }
    
    // Copy current statistics
    stats_out = stats;
    stats_out.activeSessions = getActiveSessionCount();
    
    return true;
}

void ProvisioningService::setNodePacketCallback(NodePacketCallback callback) {
    nodePacketCallback = callback;
    ESP_LOGI(TAG, "Node packet callback %s", callback ? "registered" : "cleared");
}

void ProvisioningService::clearNodePacketCallback() {
    nodePacketCallback = nullptr;
    ESP_LOGI(TAG, "Node packet callback cleared");
}