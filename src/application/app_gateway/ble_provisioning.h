#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>
#include <NimBLEDevice.h>

class BleProvisioning {
public:
    BleProvisioning();
    ~BleProvisioning();
    
    // Initialize BLE provisioning (call if not yet provisioned)
    bool begin();
    
    // Stop BLE and clean up
    void stop();
    
    // Check if BLE is currently active
    bool isActive() const { return _active; }
    
    // Provisioning data structure received from mobile app
    struct ProvisionData {
        String ssid;
        String password;
        String userUID;
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
    
    // Service and characteristic UUIDs (matching mobile app)
    static constexpr const char* SERVICE_UUID = "0000ffb0-0000-1000-8000-00805f9b34fb";
    static constexpr const char* COMMAND_CHAR_UUID = "0000ffb1-0000-1000-8000-00805f9b34fb";
    static constexpr const char* RESPONSE_CHAR_UUID = "0000ffb2-0000-1000-8000-00805f9b34fb";
    
    // Callback handlers
    class CommandCharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        CommandCharCallbacks(BleProvisioning* parent) : _parent(parent) {}
        void onWrite(NimBLECharacteristic* pCharacteristic) override;
    private:
        BleProvisioning* _parent;
    };
    
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        ServerCallbacks(BleProvisioning* parent) : _parent(parent) {}
        void onConnect(NimBLEServer* pServer) override;
        void onDisconnect(NimBLEServer* pServer) override;
    private:
        BleProvisioning* _parent;
    };
    
    friend class CommandCharCallbacks;
    friend class ServerCallbacks;
};

#endif // BLE_PROVISIONING_H
