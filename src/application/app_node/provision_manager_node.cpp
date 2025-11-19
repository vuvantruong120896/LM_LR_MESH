#include "provision_manager_node.h"
#include "esp_mac.h"
#include "led_control.h"
#include "../../components/lora_mesh_manager/src/services/NVSStorageService.h"

static const char* TAG = "ProvMgrNode";

// NVS namespace and keys (separate from Gateway)
static const char* NVS_NAMESPACE = "kagri_node";
static const char* NVS_KEY_PROVISIONED = "provisioned";
static const char* NVS_KEY_USER_UID = "user_uid";
static const char* NVS_KEY_NETKEY = "netkey";
static const char* NVS_KEY_NODE_ADDR = "node_addr";
// Note: Using NVS_KEY_GATEWAY_ADDR from NVSStorageService.h (defined as "gw_addr")

ProvisionManagerNode::ProvisionManagerNode() 
    : _provisioningActive(false), _needsRestart(false), _provisionSucceeded(false) {
}

bool ProvisionManagerNode::isProvisioned() {
    _prefs.begin(NVS_NAMESPACE, true); // read-only
    bool provisioned = _prefs.getBool(NVS_KEY_PROVISIONED, false);
    _prefs.end();
    
    // Don't log here - this function is called every loop cycle
    // ESP_LOGI(TAG, "Node provisioning status: %s", provisioned ? "YES" : "NO");
    return provisioned;
}

void ProvisionManagerNode::startProvisioningIfNeeded() {
    if (isProvisioned()) {
        ESP_LOGI(TAG, "Node already provisioned, skipping BLE");
        return;
    }
    
    ESP_LOGI(TAG, "Node not provisioned, starting BLE...");
    
    // Set provisioning callback
    _ble.setProvisionCallback([this](const BleProvisioningNode::ProvisionData& data) {
        this->handleProvisionData(data);
    });
    
    // Start BLE advertising
    if (_ble.begin()) {
        _provisioningActive = true;
        ESP_LOGI(TAG, "BLE provisioning active. Waiting for mobile app...");
    } else {
        ESP_LOGE(TAG, "Failed to start BLE provisioning");
    }
}

void ProvisionManagerNode::handleProvisionData(const BleProvisioningNode::ProvisionData& data) {
    ESP_LOGI(TAG, "Processing provisioning data...");
    
    // Parse Gateway MAC from string (format: "AA:BB:CC:DD:EE:FF")
    uint8_t gatewayMac[6];
    if (sscanf(data.gatewayMAC.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &gatewayMac[0], &gatewayMac[1], &gatewayMac[2],
               &gatewayMac[3], &gatewayMac[4], &gatewayMac[5]) != 6) {
        ESP_LOGE(TAG, "Failed to parse Gateway MAC: %s", data.gatewayMAC.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "Parsed Gateway MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             gatewayMac[0], gatewayMac[1], gatewayMac[2],
             gatewayMac[3], gatewayMac[4], gatewayMac[5]);
    
    // Get Node MAC (for address generation only)
    uint8_t nodeMac[6];
    esp_read_mac(nodeMac, ESP_MAC_WIFI_STA);
    
    // CRITICAL: Derive netkey from userUID + GATEWAY MAC (not Node MAC)
    // This ensures Gateway and all Nodes have the SAME network key
    uint8_t netkey[16];
    if (!CryptoUtils::deriveNetkey(data.userUID, gatewayMac, netkey)) {
        ESP_LOGE(TAG, "Failed to derive netkey");
        return;
    }
    
    ESP_LOGI(TAG, "✓ Netkey derived using Gateway MAC (ensures consistency with Gateway)");
    
    // Extract gateway LoRa address from last 2 bytes of MAC
    uint16_t gatewayAddress = ((uint16_t)gatewayMac[4] << 8) | (uint16_t)gatewayMac[5];
    ESP_LOGI(TAG, "Gateway LoRa Address (from MAC): 0x%04X", gatewayAddress);
    
    // Determine node address: use provided address or auto-generate from Node MAC
    uint16_t nodeAddress;
    if (data.nodeAddress != 0) {
        nodeAddress = data.nodeAddress;
        ESP_LOGI(TAG, "Using address provided by mobile app: 0x%04X", nodeAddress);
    } else {
        // Auto-generate from last 2 bytes of Node MAC
        nodeAddress = ((uint16_t)nodeMac[4] << 8) | (uint16_t)nodeMac[5];
        ESP_LOGI(TAG, "Auto-generated address from Node MAC: 0x%04X", nodeAddress);
    }
    
    // Save to NVS (kagri_node namespace) - include gateway address
    if (saveProvisionData(data.userUID, netkey, nodeAddress, gatewayAddress)) {
        ESP_LOGI(TAG, "✓ Node provisioning data saved successfully");
        ESP_LOGI(TAG, "  User UID: %s", data.userUID.c_str());
        ESP_LOGI(TAG, "  Node MAC: %s", CryptoUtils::getGatewayMAC().c_str());
        ESP_LOGI(TAG, "  Node Address: 0x%04X", nodeAddress);
        ESP_LOGI(TAG, "  Gateway Address (for LoRa): 0x%04X", gatewayAddress);
        ESP_LOGI(TAG, "  Network Key: %s", CryptoUtils::toHexString(netkey, 16).c_str());
        
        // CRITICAL: Sync netkey to mesh_config namespace for mesh operations
        ESP_LOGI(TAG, "🔄 Syncing netkey to mesh_config namespace...");
        NetworkConfig meshCfg;
        memset(&meshCfg, 0, sizeof(meshCfg));
        memcpy(meshCfg.networkKey, netkey, 16);
        // Generate dummy authToken (not used in current impl, but struct requires it)
        memset(meshCfg.authToken, 0xAB, 8); // Placeholder
        meshCfg.networkId = 0x0001; // Default network ID
        meshCfg.keyVersion = 1;
        meshCfg.timestamp = millis() / 1000;
        meshCfg.initialized = true;
        
        if (!NVSStorageService::isInitialized()) {
            NVSStorageService::initialize();
        }
        
        if (NVSStorageService::saveNetworkConfig(meshCfg)) {
            ESP_LOGI(TAG, "✅ Netkey synced to mesh_config namespace");
        } else {
            ESP_LOGW(TAG, "⚠️  Failed to sync netkey to mesh_config");
        }
        
        ESP_LOGI(TAG, "🎉 Node provisioning successful!");
        
        // Set flags for main loop to handle
        _provisionSucceeded = true;  // Signal for LED pattern
        _needsRestart = true;  // Signal for restart
        
        // Stop BLE immediately to free connection
        ESP_LOGI(TAG, "Stopping BLE provisioning...");
        _ble.stop();
        _provisioningActive = false;
        
        ESP_LOGI(TAG, "✅ Provisioning complete! Main loop will handle LED and restart.");
    } else {
        ESP_LOGE(TAG, "Failed to save provisioning data");
    }
}

bool ProvisionManagerNode::saveProvisionData(const String& userUID, const uint8_t* netkey, uint16_t nodeAddress, uint16_t gatewayAddress) {
    _prefs.begin(NVS_NAMESPACE, false); // read-write
    
    bool success = true;
    success &= _prefs.putString(NVS_KEY_USER_UID, userUID) > 0;
    success &= _prefs.putBytes(NVS_KEY_NETKEY, netkey, 16) == 16;
    success &= _prefs.putUShort(NVS_KEY_NODE_ADDR, nodeAddress) == sizeof(uint16_t);
    success &= _prefs.putUShort(NVS_KEY_GATEWAY_ADDR, gatewayAddress) == sizeof(uint16_t);
    success &= _prefs.putBool(NVS_KEY_PROVISIONED, true);
    
    _prefs.end();
    
    return success;
}

bool ProvisionManagerNode::getUserUID(String& userUID) {
    _prefs.begin(NVS_NAMESPACE, true);
    userUID = _prefs.getString(NVS_KEY_USER_UID, "");
    _prefs.end();
    
    return !userUID.isEmpty();
}

bool ProvisionManagerNode::getNetkey(uint8_t* netkey) {
    _prefs.begin(NVS_NAMESPACE, true);
    size_t len = _prefs.getBytes(NVS_KEY_NETKEY, netkey, 16);
    _prefs.end();
    
    return len == 16;
}

bool ProvisionManagerNode::getNodeAddress(uint16_t& address) {
    _prefs.begin(NVS_NAMESPACE, true);
    address = _prefs.getUShort(NVS_KEY_NODE_ADDR, 0);
    _prefs.end();
    
    return address != 0;
}

bool ProvisionManagerNode::getGatewayAddress(uint16_t& address) {
    _prefs.begin(NVS_NAMESPACE, true);
    address = _prefs.getUShort(NVS_KEY_GATEWAY_ADDR, 0);
    _prefs.end();
    
    return address != 0;
}

String ProvisionManagerNode::getNodeMAC() {
    return CryptoUtils::getGatewayMAC();
}

void ProvisionManagerNode::clearProvisionData() {
    ESP_LOGW(TAG, "Clearing node provisioning data (factory reset)");
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear();
    _prefs.end();
}
