#ifndef BLE_PROVISIONING_HANDHELD_H
#define BLE_PROVISIONING_HANDHELD_H

#include <Arduino.h>
#include <NimBLEDevice.h>

/**
 * @brief BLE Provisioning for Handheld Device
 * 
 * Allows mobile app to scan and connect to handheld device via BLE
 * to send WiFi credentials (SSID, Password, UserUID)
 * 
 * Device advertises as: KAGRI-HHC-{MAC_LAST_4_CHARS}
 * 
 * UUIDs match Gateway BLE for app compatibility
 */
class BleProvisioning {
public:
    BleProvisioning();
    ~BleProvisioning();
    
    /**
     * @brief Start BLE advertising
     * @return true if successful
     */
    bool begin();
    
    /**
     * @brief Stop BLE and clean up
     */
    void stop();
    
    /**
     * @brief Check if BLE is currently active/advertising
     * @return true if BLE is running
     */
    bool isActive() const { return _active; }
    
    /**
     * @brief WiFi Provisioning data structure
     */
    struct ProvisionData {
        String ssid;
        String password;
        String userUID;
    };
    
    /**
     * @brief Callback when provisioning data received from mobile app
     */
    typedef std::function<void(const ProvisionData&)> ProvisionCallback;
    void setProvisionCallback(ProvisionCallback cb) { _provisionCallback = cb; }

private:
    bool _active;
    NimBLEServer* _server;
    NimBLECharacteristic* _commandChar;      // For receiving credentials from app
    NimBLECharacteristic* _responseChar;     // For sending status back to app
    ProvisionCallback _provisionCallback;
    
    // Service and characteristic UUIDs (must match mobile app expectations)
    // These are the same UUIDs as Gateway for compatibility
    static constexpr const char* SERVICE_UUID = "0000ffb0-0000-1000-8000-00805f9b34fb";
    static constexpr const char* COMMAND_CHAR_UUID = "0000ffb1-0000-1000-8000-00805f9b34fb";
    static constexpr const char* RESPONSE_CHAR_UUID = "0000ffb2-0000-1000-8000-00805f9b34fb";
    
    // Callback handlers for BLE characteristic writes
    class CommandCharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        CommandCharCallbacks(BleProvisioning* parent) : _parent(parent) {}
        void onWrite(NimBLECharacteristic* pCharacteristic) override;
    private:
        BleProvisioning* _parent;
    };
    
    // Callback handlers for BLE server connection/disconnection
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

#endif // BLE_PROVISIONING_HANDHELD_H
