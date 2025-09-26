#include "mesh_security_config.h"
#include "mesh_security.h"
#include "esp_log.h"

static const char* MESH_INIT_TAG = "MeshSecurityInit";

// Initialize security configuration based on device role
bool initializeMeshSecurity() {
    MeshSecurityConfig config;
    
    // Set up network keys (same for all devices in network)
    uint8_t networkKey[] = MESH_NETWORK_KEY;
    uint8_t authToken[] = MESH_AUTH_TOKEN;
    
    memcpy(config.networkKey, networkKey, MESH_NETKEY_SIZE);
    memcpy(config.authToken, authToken, MESH_AUTH_TOKEN_SIZE);
    
    // Configure security based on build settings
    #if ENABLE_MESH_SECURITY
        config.enableEncryption = true;
        config.enableAuthentication = true;
        config.enableReplayProtection = true;
        
        #ifdef DEVICE_MODE
            #if DEVICE_MODE == 1  // Node mode
                config.securityLevel = NODE_SECURITY_LEVEL;
                ESP_LOGI(MESH_INIT_TAG, "Initializing Node security - Level: %d", config.securityLevel);
            #elif DEVICE_MODE == 2  // Bridge mode
                config.securityLevel = BRIDGE_SECURITY_LEVEL;
                ESP_LOGI(MESH_INIT_TAG, "Initializing Bridge security - Level: %d", config.securityLevel);
            #endif
        #endif
    #else
        config.enableEncryption = false;
        config.enableAuthentication = false;
        config.enableReplayProtection = false;
        config.securityLevel = 0;
        ESP_LOGW(MESH_INIT_TAG, "Mesh security DISABLED");
    #endif
    
    // Set key rotation interval
    config.keyRotationInterval = 3600; // 1 hour
    
    // Initialize security service
    bool result = MeshSecurityService::initialize(config);
    
    if (result) {
        ESP_LOGI(MESH_INIT_TAG, "Mesh security initialized successfully");
        ESP_LOGI(MESH_INIT_TAG, "Security features:");
        ESP_LOGI(MESH_INIT_TAG, "  - Encryption: %s", config.enableEncryption ? "ENABLED" : "DISABLED");
        ESP_LOGI(MESH_INIT_TAG, "  - Authentication: %s", config.enableAuthentication ? "ENABLED" : "DISABLED");
        ESP_LOGI(MESH_INIT_TAG, "  - Replay Protection: %s", config.enableReplayProtection ? "ENABLED" : "DISABLED");
        ESP_LOGI(MESH_INIT_TAG, "  - Security Level: %d", config.securityLevel);
    } else {
        ESP_LOGE(MESH_INIT_TAG, "Failed to initialize mesh security");
    }
    
    return result;
}

// Get security configuration for current device
MeshSecurityConfig getCurrentSecurityConfig() {
    return MeshSecurityService::getConfig();
}

// Check if mesh security is enabled
bool isMeshSecurityEnabled() {
    #if ENABLE_MESH_SECURITY
        return true;
    #else
        return false;
    #endif
}

// Get security level for current device
uint8_t getCurrentSecurityLevel() {
    #if ENABLE_MESH_SECURITY
        #ifdef DEVICE_MODE
            #if DEVICE_MODE == 1
                return NODE_SECURITY_LEVEL;
            #elif DEVICE_MODE == 2
                return BRIDGE_SECURITY_LEVEL;
            #endif
        #endif
    #endif
    return 0;
}

// Log security status
void logSecurityStatus() {
    if (isMeshSecurityEnabled()) {
        const MeshSecurityConfig& config = getCurrentSecurityConfig();
        
        ESP_LOGI(MESH_INIT_TAG, "=== Mesh Security Status ===");
        ESP_LOGI(MESH_INIT_TAG, "Security Level: %d", config.securityLevel);
        ESP_LOGI(MESH_INIT_TAG, "Encryption: %s", config.enableEncryption ? "ON" : "OFF");
        ESP_LOGI(MESH_INIT_TAG, "Authentication: %s", config.enableAuthentication ? "ON" : "OFF");
        ESP_LOGI(MESH_INIT_TAG, "Replay Protection: %s", config.enableReplayProtection ? "ON" : "OFF");
        ESP_LOGI(MESH_INIT_TAG, "Key Rotation: %lu seconds", config.keyRotationInterval);
        
        // Log network key info (first few bytes only for security)
        ESP_LOGI(MESH_INIT_TAG, "Network Key: %02X%02X%02X%02X...", 
                 config.networkKey[0], config.networkKey[1], 
                 config.networkKey[2], config.networkKey[3]);
    } else {
        ESP_LOGW(MESH_INIT_TAG, "=== Mesh Security DISABLED ===");
        ESP_LOGW(MESH_INIT_TAG, "WARNING: Network traffic is NOT encrypted!");
        ESP_LOGW(MESH_INIT_TAG, "WARNING: No network access control!");
    }
}