#ifndef BLE_PROVISIONING_NODE_H
#define BLE_PROVISIONING_NODE_H

#include <Arduino.h>
#include <NimBLEDevice.h>

class BleProvisioningNode {
public:
    BleProvisioningNode();
    ~BleProvisioningNode();
    
    // Initialize BLE provisioning (call if not yet provisioned)
    bool begin();
    
    // Stop BLE and clean up
    void stop();
    
    // Check if BLE is currently active
    bool isActive() const { return _active; }
    
    // Provisioning data structure received from mobile app
    struct ProvisionData {
        String userUID;
        String gatewayMAC;     // Gateway MAC address (for netkey derivation)
        uint16_t nodeAddress;  // Optional: Mobile app can specify address, 0 = auto-generate from MAC
    };
    
    // Callback when provisioning data received
    typedef std::function<void(const ProvisionData&)> ProvisionCallback;
    void setProvisionCallback(ProvisionCallback cb) { _provisionCallback = cb; }

private:
    bool _active;
    NimBLEServer* _server;
    NimBLECharacteristic* _commandChar;
    NimBLECharacteristic* _responseChar;
    ProvisionCallback _provisionCallback;
    
    // Service and characteristic UUIDs (DIFFERENT from Gateway to avoid conflicts)
    // Gateway uses 0000ffb0-xxxx, Node uses 0000ffc0-xxxx
    static constexpr const char* SERVICE_UUID = "0000ffc0-0000-1000-8000-00805f9b34fb";
    static constexpr const char* COMMAND_CHAR_UUID = "0000ffc1-0000-1000-8000-00805f9b34fb";
    static constexpr const char* RESPONSE_CHAR_UUID = "0000ffc2-0000-1000-8000-00805f9b34fb";
    
    // Callback handlers
    class CommandCharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        CommandCharCallbacks(BleProvisioningNode* parent) : _parent(parent) {}
        void onWrite(NimBLECharacteristic* pCharacteristic) override;
    private:
        BleProvisioningNode* _parent;
    };
    
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        ServerCallbacks(BleProvisioningNode* parent) : _parent(parent) {}
        void onConnect(NimBLEServer* pServer) override;
        void onDisconnect(NimBLEServer* pServer) override;
    private:
        BleProvisioningNode* _parent;
    };
    
    friend class CommandCharCallbacks;
    friend class ServerCallbacks;
};

#endif // BLE_PROVISIONING_NODE_H
