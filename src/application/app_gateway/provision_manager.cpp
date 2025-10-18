#include "provision_manager.h"
#include "esp_mac.h"
#include "led_control.h"  // For LED feedback

static const char* TAG = "ProvisionMgr";

// NVS namespace and keys
static const char* NVS_NAMESPACE = "kagri_prov";
static const char* NVS_KEY_PROVISIONED = "provisioned";
static const char* NVS_KEY_WIFI_SSID = "wifi_ssid";
static const char* NVS_KEY_WIFI_PASS = "wifi_pass";
static const char* NVS_KEY_USER_UID = "user_uid";
static const char* NVS_KEY_NETKEY = "netkey";

ProvisionManager::ProvisionManager() : _provisioningActive(false), _needsRestart(false), _provisionSucceeded(false) {
}

bool ProvisionManager::isProvisioned() {
    _prefs.begin(NVS_NAMESPACE, true); // read-only
    bool provisioned = _prefs.getBool(NVS_KEY_PROVISIONED, false);
    _prefs.end();
    
    ESP_LOGI(TAG, "Provisioning status: %s", provisioned ? "YES" : "NO");
    return provisioned;
}

void ProvisionManager::startProvisioningIfNeeded() {
    if (isProvisioned()) {
        ESP_LOGI(TAG, "Device already provisioned, skipping BLE");
        return;
    }
    
    ESP_LOGI(TAG, "Device not provisioned, starting BLE...");
    
    // Set provisioning callback
    _ble.setProvisionCallback([this](const BleProvisioning::ProvisionData& data) {
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

void ProvisionManager::handleProvisionData(const BleProvisioning::ProvisionData& data) {
    ESP_LOGI(TAG, "Processing provisioning data...");
    
    // Get device MAC
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // Derive netkey from userUID + MAC
    uint8_t netkey[16];
    if (!CryptoUtils::deriveNetkey(data.userUID, mac, netkey)) {
        ESP_LOGE(TAG, "Failed to derive netkey");
        return;
    }
    
    // Save to NVS
    if (saveProvisionData(data.ssid, data.password, data.userUID, netkey)) {
        ESP_LOGI(TAG, "✓ Provisioning data saved successfully");
        ESP_LOGI(TAG, "  WiFi: %s", data.ssid.c_str());
        ESP_LOGI(TAG, "  User: %s", data.userUID.c_str());
        ESP_LOGI(TAG, "  MAC: %s", CryptoUtils::getGatewayMAC().c_str());
        ESP_LOGI(TAG, "  Netkey: %s", CryptoUtils::toHexString(netkey, 16).c_str());
        
        ESP_LOGI(TAG, "🎉 Provisioning successful!");
        
        // Set flags for main loop to handle (don't block in callback!)
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

bool ProvisionManager::saveProvisionData(const String& ssid, const String& password, 
                                        const String& userUID, const uint8_t* netkey) {
    _prefs.begin(NVS_NAMESPACE, false); // read-write
    
    bool success = true;
    success &= _prefs.putString(NVS_KEY_WIFI_SSID, ssid) > 0;
    success &= _prefs.putString(NVS_KEY_WIFI_PASS, password) > 0;
    success &= _prefs.putString(NVS_KEY_USER_UID, userUID) > 0;
    success &= _prefs.putBytes(NVS_KEY_NETKEY, netkey, 16) == 16;
    success &= _prefs.putBool(NVS_KEY_PROVISIONED, true);
    
    _prefs.end();
    
    return success;
}

bool ProvisionManager::getWiFiConfig(String& ssid, String& password) {
    _prefs.begin(NVS_NAMESPACE, true);
    ssid = _prefs.getString(NVS_KEY_WIFI_SSID, "");
    password = _prefs.getString(NVS_KEY_WIFI_PASS, "");
    _prefs.end();
    
    return !ssid.isEmpty();
}

bool ProvisionManager::getWiFiCredentials(String& ssid, String& password) {
    return getWiFiConfig(ssid, password);
}

bool ProvisionManager::getUserUID(String& userUID) {
    _prefs.begin(NVS_NAMESPACE, true);
    userUID = _prefs.getString(NVS_KEY_USER_UID, "");
    _prefs.end();
    
    return !userUID.isEmpty();
}

bool ProvisionManager::getNetkey(uint8_t* netkey) {
    _prefs.begin(NVS_NAMESPACE, true);
    size_t len = _prefs.getBytes(NVS_KEY_NETKEY, netkey, 16);
    _prefs.end();
    
    return len == 16;
}

String ProvisionManager::getGatewayMAC() {
    return CryptoUtils::getGatewayMAC();
}

void ProvisionManager::clearProvisionData() {
    ESP_LOGW(TAG, "Clearing provisioning data (factory reset)");
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.clear();
    _prefs.end();
}
