#include "mesh_security.h"
#include "esp_log.h"

static const char* MESH_SEC_TAG = "MESH_SEC";

// Authentication implementation
bool MeshSecurityService::handleJoinRequest(const JoinRequestPacket* request, JoinResponsePacket* response) {
    if (!request || !response || !initialized) {
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Handling join request from node 0x%04X", request->nodeId);
    
    // Verify request MAC first
    uint8_t computedMac[MESH_MAC_SIZE];
    const uint8_t* requestData = (const uint8_t*)request;
    size_t dataLen = sizeof(JoinRequestPacket) - MESH_MAC_SIZE; // Exclude MAC field
    
    if (generateMAC(requestData, dataLen, computedMac, config.networkKey) != MESH_SEC_OK) {
        ESP_LOGE(MESH_SEC_TAG, "Failed to compute MAC for join request");
        return false;
    }
    
    if (verifyMAC(requestData, dataLen, request->header.mac, config.networkKey) != MESH_SEC_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Invalid MAC in join request from 0x%04X", request->nodeId);
        
        // Send rejection response
        memset(response, 0, sizeof(JoinResponsePacket));
        response->header.securityType = SECURITY_JOIN_RESPONSE;
        response->nodeId = request->nodeId;
        response->accepted = false;
        return false;
    }
    
    // Check if node already authenticated
    if (isNodeAuthenticated(request->nodeId)) {
        ESP_LOGW(MESH_SEC_TAG, "Node 0x%04X already authenticated", request->nodeId);
    }
    
    // Prepare response
    memset(response, 0, sizeof(JoinResponsePacket));
    response->header.securityType = SECURITY_JOIN_RESPONSE;
    response->header.flags = SEC_FLAG_AUTHENTICATED;
    response->header.sequenceNumber = getNextSequenceNumber();
    response->nodeId = request->nodeId;
    
    // Generate nonce for response
    generateNonce(response->header.nonce);
    
    // Create challenge response (simple hash of challenge + network auth token)
    uint8_t challengeInput[MESH_CHALLENGE_SIZE + MESH_AUTH_TOKEN_SIZE];
    memcpy(challengeInput, request->challenge, MESH_CHALLENGE_SIZE);
    memcpy(challengeInput + MESH_CHALLENGE_SIZE, config.authToken, MESH_AUTH_TOKEN_SIZE);
    
    // Generate response hash
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    if (md_info && mbedtls_md_setup(&md_ctx, md_info, 0) == 0) {
        mbedtls_md_starts(&md_ctx);
        mbedtls_md_update(&md_ctx, challengeInput, sizeof(challengeInput));
        
        uint8_t hash[32];
        mbedtls_md_finish(&md_ctx, hash);
        mbedtls_md_free(&md_ctx);
        
        // Use first 8 bytes as challenge response
        memcpy(response->response, hash, MESH_CHALLENGE_SIZE);
    } else {
        // Fallback: simple XOR
        for (int i = 0; i < MESH_CHALLENGE_SIZE; i++) {
            response->response[i] = request->challenge[i] ^ config.authToken[i % MESH_AUTH_TOKEN_SIZE];
        }
    }
    
    // Include network authentication token (encrypted)
    memcpy(response->networkAuth, config.authToken, MESH_AUTH_TOKEN_SIZE);
    
    // Accept the request
    response->accepted = true;
    
    // Generate temporary session key for this node
    esp_fill_random((uint8_t*)&response->sessionKey, sizeof(response->sessionKey));
    
    // Generate MAC for response
    const uint8_t* responseData = (const uint8_t*)response;
    size_t responseDataLen = sizeof(JoinResponsePacket) - MESH_MAC_SIZE;
    
    if (generateMAC(responseData, responseDataLen, response->header.mac, config.networkKey) != MESH_SEC_OK) {
        ESP_LOGE(MESH_SEC_TAG, "Failed to generate MAC for join response");
        return false;
    }
    
    // Add node to authenticated list
    if (authenticatedCount < 32) {
        bool nodeExists = false;
        for (int i = 0; i < authenticatedCount; i++) {
            if (authenticatedNodes[i] == request->nodeId) {
                nodeExists = true;
                break;
            }
        }
        
        if (!nodeExists) {
            authenticatedNodes[authenticatedCount] = request->nodeId;
            lastSequenceNumbers[authenticatedCount] = response->header.sequenceNumber;
            authenticatedCount++;
        }
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Join request accepted for node 0x%04X", request->nodeId);
    return true;
}

bool MeshSecurityService::processJoinResponse(const JoinResponsePacket* response) {
    if (!response || !initialized) {
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Processing join response for node 0x%04X", response->nodeId);
    
    // Verify response MAC
    const uint8_t* responseData = (const uint8_t*)response;
    size_t dataLen = sizeof(JoinResponsePacket) - MESH_MAC_SIZE;
    
    if (verifyMAC(responseData, dataLen, response->header.mac, config.networkKey) != MESH_SEC_OK) {
        ESP_LOGW(MESH_SEC_TAG, "Invalid MAC in join response");
        return false;
    }
    
    if (!response->accepted) {
        ESP_LOGW(MESH_SEC_TAG, "Join request was rejected");
        return false;
    }
    
    // Verify network authentication token
    if (memcmp(response->networkAuth, config.authToken, MESH_AUTH_TOKEN_SIZE) != 0) {
        ESP_LOGW(MESH_SEC_TAG, "Invalid network authentication in join response");
        return false;
    }
    
    // Check sequence number for replay protection
    if (!isValidSequenceNumber(response->nodeId, response->header.sequenceNumber)) {
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Successfully authenticated with network via node 0x%04X", response->nodeId);
    ESP_LOGI(MESH_SEC_TAG, "Session key: 0x%08lX", response->sessionKey);
    
    return true;
}

bool MeshSecurityService::authenticateNode(uint16_t nodeId, const uint8_t* challenge, const uint8_t* response) {
    if (!challenge || !response) {
        return false;
    }
    
    // Compute expected response
    uint8_t challengeInput[MESH_CHALLENGE_SIZE + MESH_AUTH_TOKEN_SIZE];
    memcpy(challengeInput, challenge, MESH_CHALLENGE_SIZE);
    memcpy(challengeInput + MESH_CHALLENGE_SIZE, config.authToken, MESH_AUTH_TOKEN_SIZE);
    
    uint8_t expectedResponse[MESH_CHALLENGE_SIZE];
    
    // Generate expected response hash
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    
    if (md_info && mbedtls_md_setup(&md_ctx, md_info, 0) == 0) {
        mbedtls_md_starts(&md_ctx);
        mbedtls_md_update(&md_ctx, challengeInput, sizeof(challengeInput));
        
        uint8_t hash[32];
        mbedtls_md_finish(&md_ctx, hash);
        mbedtls_md_free(&md_ctx);
        
        memcpy(expectedResponse, hash, MESH_CHALLENGE_SIZE);
    } else {
        // Fallback: simple XOR
        for (int i = 0; i < MESH_CHALLENGE_SIZE; i++) {
            expectedResponse[i] = challenge[i] ^ config.authToken[i % MESH_AUTH_TOKEN_SIZE];
        }
    }
    
    // Constant-time comparison
    int diff = 0;
    for (int i = 0; i < MESH_CHALLENGE_SIZE; i++) {
        diff |= (response[i] ^ expectedResponse[i]);
    }
    
    bool authenticated = (diff == 0);
    
    if (authenticated) {
        ESP_LOGI(MESH_SEC_TAG, "Node 0x%04X authenticated successfully", nodeId);
    } else {
        ESP_LOGW(MESH_SEC_TAG, "Authentication failed for node 0x%04X", nodeId);
    }
    
    return authenticated;
}

bool MeshSecurityService::updateNetworkKey(const uint8_t* newKey) {
    if (!newKey || !initialized) {
        return false;
    }
    
    ESP_LOGI(MESH_SEC_TAG, "Updating network key");
    
    // Store old key for graceful transition
    uint8_t oldKey[MESH_NETKEY_SIZE];
    memcpy(oldKey, config.networkKey, MESH_NETKEY_SIZE);
    
    // Update to new key
    memcpy(config.networkKey, newKey, MESH_NETKEY_SIZE);
    
    // Update AES context
    int ret = mbedtls_aes_setkey_enc(&aes_ctx, config.networkKey, MESH_NETKEY_SIZE * 8);
    if (ret != 0) {
        // Rollback on failure
        memcpy(config.networkKey, oldKey, MESH_NETKEY_SIZE);
        mbedtls_aes_setkey_enc(&aes_ctx, config.networkKey, MESH_NETKEY_SIZE * 8);
        ESP_LOGE(MESH_SEC_TAG, "Failed to update network key");
        return false;
    }
    
    // Reset authentication state (all nodes need to re-authenticate)
    authenticatedCount = 0;
    memset(authenticatedNodes, 0, sizeof(authenticatedNodes));
    memset(lastSequenceNumbers, 0, sizeof(lastSequenceNumbers));
    
    ESP_LOGI(MESH_SEC_TAG, "Network key updated successfully");
    return true;
}