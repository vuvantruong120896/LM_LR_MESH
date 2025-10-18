#ifndef PROVISION_MANAGER_H
#define PROVISION_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "ble_provisioning.h"
#include "crypto_utils.h"

class ProvisionManager {
public:
    ProvisionManager();
    
    // Check if device is provisioned
    bool isProvisioned();
    
    // Check if device needs restart after provisioning
    bool needsRestart() const { return _needsRestart; }
    
    // Check if provisioning just succeeded (for LED feedback)
    bool provisionSucceeded() const { return _provisionSucceeded; }
    void clearProvisionSuccessFlag() { _provisionSucceeded = false; }
    
    // Start BLE provisioning if not provisioned
    void startProvisioningIfNeeded();
    
    // Get stored configuration
    bool getWiFiConfig(String& ssid, String& password);
    bool getWiFiCredentials(String& ssid, String& password); // Alias for getWiFiConfig
    bool getUserUID(String& userUID);
    bool getNetkey(uint8_t* netkey); // 16 bytes
    String getGatewayMAC();
    
    // Clear provisioning data (for factory reset)
    void clearProvisionData();
    
private:
    Preferences _prefs;
    BleProvisioning _ble;
    bool _provisioningActive;
    bool _needsRestart;
    bool _provisionSucceeded;
    
    // Handle provisioning data from BLE
    void handleProvisionData(const BleProvisioning::ProvisionData& data);
    
    // Save provisioning data to NVS
    bool saveProvisionData(const String& ssid, const String& password, 
                          const String& userUID, const uint8_t* netkey);
};

#endif // PROVISION_MANAGER_H
