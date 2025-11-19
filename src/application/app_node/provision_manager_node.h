#ifndef PROVISION_MANAGER_NODE_H
#define PROVISION_MANAGER_NODE_H

#include <Arduino.h>
#include <Preferences.h>
#include "ble_provisioning_node.h"
#include "../app_gateway/crypto_utils.h"

class ProvisionManagerNode {
public:
    ProvisionManagerNode();
    
    // Check if node is provisioned
    bool isProvisioned();
    
    // Check if node needs restart after provisioning
    bool needsRestart() const { return _needsRestart; }
    
    // Check if provisioning just succeeded (for LED feedback)
    bool provisionSucceeded() const { return _provisionSucceeded; }
    void clearProvisionSuccessFlag() { _provisionSucceeded = false; }
    
    // Start BLE provisioning if not provisioned
    void startProvisioningIfNeeded();
    
    // Get stored configuration
    bool getUserUID(String& userUID);
    bool getNetkey(uint8_t* netkey); // 16 bytes
    bool getNodeAddress(uint16_t& address);
    bool getGatewayAddress(uint16_t& address);  // Get gateway address (last 2 bytes of MAC)
    String getNodeMAC();
    
    // Clear provisioning data (for factory reset)
    void clearProvisionData();
    
private:
    Preferences _prefs;
    BleProvisioningNode _ble;
    bool _provisioningActive;
    bool _needsRestart;
    bool _provisionSucceeded;
    
    // Handle provisioning data from BLE
    void handleProvisionData(const BleProvisioningNode::ProvisionData& data);
    
    // Save provisioning data to NVS (includes gateway address for offline buffer sync)
    bool saveProvisionData(const String& userUID, const uint8_t* netkey, uint16_t nodeAddress, uint16_t gatewayAddress);
};

#endif // PROVISION_MANAGER_NODE_H
