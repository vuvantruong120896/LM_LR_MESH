#ifndef BLE_SENSOR_DATA_HANDHELD_H
#define BLE_SENSOR_DATA_HANDHELD_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>
#include "../../components/rs485_soil_sensor/include/sensor_data.h"

/**
 * @brief BLE Sensor Data Service for Handheld Device
 * 
 * Allows mobile app to connect and receive sensor data from handheld device
 * Different from WiFi provisioning BLE service
 * 
 * Device advertises as: KAGRI-HHT-{MAC_LAST_4_CHARS}
 * 
 * Service UUID: Different from provisioning service for differentiation
 */
class BleSensorData {
public:
    // Callback function type for disconnect events
    using DisconnectCallback = std::function<void()>;
    
    // Callback function type for subscription events
    using SubscriptionCallback = std::function<void()>;
    
    // Callback function type for data sent events
    using DataSentCallback = std::function<void()>;
    
    BleSensorData();
    ~BleSensorData();
    
    /**
     * @brief Start BLE advertising for sensor data
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
     * @brief Send sensor data to connected phone
     * @param data Sensor data to send
     * @return true if sent successfully
     */
    bool sendSensorData(const sensorData& data);
    
    /**
     * @brief Set callback for disconnect events
     * @param callback Function to call when client disconnects
     */
    void setDisconnectCallback(DisconnectCallback callback) { 
        _disconnectCallback = callback; 
    }
    
    /**
     * @brief Set callback for subscription events
     * @param callback Function to call when client subscribes to sensor data
     */
    void setSubscriptionCallback(SubscriptionCallback callback) { 
        _subscriptionCallback = callback; 
    }
    
    /**
     * @brief Set callback for data sent events
     * @param callback Function to call when sensor data is successfully sent
     */
    void setDataSentCallback(DataSentCallback callback) { 
        _dataSentCallback = callback; 
    }
    
    /**
     * @brief Check if any client is subscribed to sensor data notifications
     * @return true if at least one client is subscribed
     */
    bool isClientSubscribed() const { return _clientSubscribed; }
    
    /**
     * @brief Get subscription state change
     * @return true if subscription state just changed from false to true
     */
    bool getAndClearSubscriptionEvent() {
        if (_subscriptionStateChanged) {
            _subscriptionStateChanged = false;
            return true;
        }
        return false;
    }

private:
    bool _active;
    NimBLEServer* _server;
    NimBLECharacteristic* _sensorDataChar;   // For sending sensor data to app
    NimBLECharacteristic* _commandChar;      // For receiving commands from app
    
    // Disconnect callback
    DisconnectCallback _disconnectCallback;
    
    // Subscription callback
    SubscriptionCallback _subscriptionCallback;
    
    // Data sent callback
    DataSentCallback _dataSentCallback;
    
    // Subscription state tracking
    bool _clientSubscribed = false;
    bool _subscriptionStateChanged = false;
    bool _clientConnected = false;  // Track if client is currently connected
    
    // Service and characteristic UUIDs for sensor data service
    static constexpr const char* SERVICE_UUID = "0000ffe0-0000-1000-8000-00805f9b34fb";
    static constexpr const char* SENSOR_DATA_CHAR_UUID = "0000ffe1-0000-1000-8000-00805f9b34fb";
    static constexpr const char* COMMAND_CHAR_UUID = "0000ffe2-0000-1000-8000-00805f9b34fb";
    
    // Get MAC address last 4 characters for device name
    String getDeviceName();
    
    // Callback handlers for BLE characteristic writes
    class CommandCharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        CommandCharCallbacks(BleSensorData* parent) : _parent(parent) {}
        void onWrite(NimBLECharacteristic* pCharacteristic) override;
    private:
        BleSensorData* _parent;
    };
    
    // Callback handlers for sensor data characteristic subscription
    class SensorDataCharCallbacks : public NimBLECharacteristicCallbacks {
    public:
        SensorDataCharCallbacks(BleSensorData* parent) : _parent(parent) {}
        void onSubscribe(NimBLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc, uint16_t subValue) override;
    private:
        BleSensorData* _parent;
    };
    
    // Callback handlers for BLE server connection/disconnection
    class ServerCallbacks : public NimBLEServerCallbacks {
    public:
        ServerCallbacks(BleSensorData* parent) : _parent(parent) {}
        void onConnect(NimBLEServer* pServer) override;
        void onDisconnect(NimBLEServer* pServer) override;
    private:
        BleSensorData* _parent;
    };
    
    // Callback instances - declared after class definitions
    CommandCharCallbacks _commandCharCallbacks;
    SensorDataCharCallbacks _sensorDataCharCallbacks;
    ServerCallbacks _serverCallbacks;
};

#endif // BLE_SENSOR_DATA_HANDHELD_H
