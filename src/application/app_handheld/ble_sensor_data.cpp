#include "ble_sensor_data.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <ArduinoJson.h>

static const char* TAG = "BleSensorData";

BleSensorData::BleSensorData() 
    : _active(false), _server(nullptr), _sensorDataChar(nullptr), _commandChar(nullptr),
      _commandCharCallbacks(this), _sensorDataCharCallbacks(this), _serverCallbacks(this) {
    ESP_LOGI(TAG, "BleSensorData created");
}

BleSensorData::~BleSensorData() {
    stop();
    ESP_LOGI(TAG, "BleSensorData destroyed");
}

String BleSensorData::getDeviceName() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    
    // Format: KAGRI-HHT-XXYY (last 2 bytes of MAC)
    char name[20];
    snprintf(name, sizeof(name), "KAGRI-HHT-%02X%02X", mac[4], mac[5]);
    return String(name);
}

bool BleSensorData::begin() {
    if (_active) {
        ESP_LOGW(TAG, "BLE Sensor Data already active");
        return true;
    }
    
    try {
        String deviceName = getDeviceName();
        ESP_LOGI(TAG, "Starting BLE Sensor Data service as: %s", deviceName.c_str());
        
        // If NimBLE already initialized, reuse it
        if (NimBLEDevice::getInitialized()) {
            ESP_LOGI(TAG, "NimBLE already initialized, reusing");
            _server = NimBLEDevice::getServer();
        } else {
            // First time: Initialize NimBLE
            ESP_LOGI(TAG, "First time init: initializing NimBLE device");
            NimBLEDevice::init(deviceName.c_str());
            NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
            _server = NimBLEDevice::createServer();
        }
        
        if (!_server) {
            ESP_LOGE(TAG, "Failed to get/create BLE server");
            return false;
        }
        
        _server->setCallbacks(&_serverCallbacks);
        
        // Create service
        NimBLEService* service = _server->createService(SERVICE_UUID);
        if (!service) {
            ESP_LOGE(TAG, "Failed to create service");
            return false;
        }
        
        // Create sensor data characteristic (notify to app)
        _sensorDataChar = service->createCharacteristic(
            SENSOR_DATA_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
        );
        if (!_sensorDataChar) {
            ESP_LOGE(TAG, "Failed to create sensor data characteristic");
            return false;
        }
        
        // Set callback to detect subscription
        _sensorDataChar->setCallbacks(&_sensorDataCharCallbacks);
        
        // Create command characteristic (receive from app)
        _commandChar = service->createCharacteristic(
            COMMAND_CHAR_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        if (!_commandChar) {
            ESP_LOGE(TAG, "Failed to create command characteristic");
            return false;
        }
        
        _commandChar->setCallbacks(&_commandCharCallbacks);
        
        // Start service
        service->start();
        
        // Start advertising
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinInterval(160);  // 100ms
        pAdvertising->setMaxInterval(240);  // 150ms
        pAdvertising->start();
        
        _active = true;
        ESP_LOGI(TAG, "BLE Sensor Data service started successfully");
        return true;
        
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in begin(): %s", e.what());
        return false;
    }
}

void BleSensorData::stop() {
    if (!_active) {
        return;
    }
    
    try {
        ESP_LOGI(TAG, "Stopping BLE Sensor Data service...");
        
        if (_server) {
            // Stop advertising first
            NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
            if (pAdvertising) {
                pAdvertising->stop();
                ESP_LOGI(TAG, "Advertising stopped");
            }
            
            // Wait for client to disconnect (max 2 seconds)
            int waitCount = 0;
            while (_clientConnected && waitCount < 20) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                waitCount++;
                ESP_LOGD(TAG, "Waiting for client disconnect... (%d/20)", waitCount);
            }
            
            if (_clientConnected) {
                ESP_LOGW(TAG, "⚠️ Client still connected after timeout, force disconnecting");
                std::vector<uint16_t> connIds = _server->getPeerDevices();
                for (uint16_t connId : connIds) {
                    _server->disconnect(connId);
                    ESP_LOGI(TAG, "Force disconnected client: %d", connId);
                }
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
            
            _server = nullptr;
            _sensorDataChar = nullptr;
            _commandChar = nullptr;
        }
        
        // IMPORTANT: Do NOT call deinit() from main loop - causes mutex issues
        // Just stop advertising and let BLE persist
        // Next begin() will reuse the same BLE device
        
        _active = false;
        _clientConnected = false;
        _clientSubscribed = false;
        ESP_LOGI(TAG, "✅ BLE Sensor Data service stopped successfully");
        
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in stop(): %s", e.what());
        _active = false;
    }
}

bool BleSensorData::sendSensorData(const sensorData& data) {
    if (!_active || !_sensorDataChar) {
        ESP_LOGW(TAG, "BLE not active, cannot send sensor data");
        return false;
    }
    
    try {
        // Create JSON with sensor data (match Firebase validation)
        JsonDocument doc;
        doc["temperature"] = data.data.soil.soilTemperature;  // Match Firebase: temperature
        doc["humidity"] = data.data.soil.soilMoisture;        // Match Firebase: humidity (moisture as humidity)
        doc["timestamp"] = millis();
        
        // Also include other soil sensor data for display
        doc["ec"] = data.data.soil.conductivity;
        doc["pH"] = data.data.soil.pH;
        doc["N"] = data.data.soil.nitrogen;
        doc["P"] = data.data.soil.phosphorus;
        doc["K"] = data.data.soil.potassium;
        doc["saltContent"] = data.data.soil.saltContent;  // NEW: Salt content
        
        String jsonStr;
        serializeJson(doc, jsonStr);
        
        // Set characteristic value (always, even without subscriber)
        _sensorDataChar->setValue((uint8_t*)jsonStr.c_str(), jsonStr.length());
        
        // Check if any client is subscribed to notifications
        bool hasSubscriber = _sensorDataChar->getSubscribedCount() > 0;
        
        if (hasSubscriber) {
            // Client subscribed - send notification
            _sensorDataChar->notify();
            ESP_LOGI(TAG, "✅ Sensor data sent to subscribed client: %s", jsonStr.c_str());
            
            // Trigger data sent callback
            if (_dataSentCallback) {
                _dataSentCallback();
                ESP_LOGI(TAG, "Data sent callback triggered");
            }
        } else {
            // No subscriber yet - just set value (characteristic readable via READ)
            ESP_LOGW(TAG, "⚠️ No client subscribed - data set for READ, waiting for subscription: %s", jsonStr.c_str());
        }
        
        return true;
        
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in sendSensorData(): %s", e.what());
        return false;
    }
}

void BleSensorData::CommandCharCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    try {
        std::string value = pCharacteristic->getValue();
        ESP_LOGI(TAG, "Command received from app: %s", value.c_str());
        
        // Parse JSON command
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, value.c_str());
        
        if (error) {
            ESP_LOGW(TAG, "Failed to parse command JSON: %s", error.c_str());
            return;
        }
        
        if (doc["cmd"].is<const char*>()) {
            const char* cmd = doc["cmd"];
            if (strcmp(cmd, "get_data") == 0) {
                ESP_LOGI(TAG, "App requested sensor data");
                // App will call getSensorData separately
            }
        }
        
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in onWrite(): %s", e.what());
    }
}

void BleSensorData::SensorDataCharCallbacks::onSubscribe(NimBLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc, uint16_t subValue) {
    try {
        if (subValue > 0) {
            _parent->_clientSubscribed = true;
            _parent->_subscriptionStateChanged = true;
            ESP_LOGI(TAG, "📱 App SUBSCRIBED to sensor data notifications (ffe1) - ready to receive data!");
            
            // Call subscription callback immediately
            if (_parent->_subscriptionCallback) {
                _parent->_subscriptionCallback();
            }
        } else {
            _parent->_clientSubscribed = false;
            ESP_LOGI(TAG, "📱 App UNSUBSCRIBED from sensor data notifications (ffe1)");
        }
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in onSubscribe(): %s", e.what());
    }
}

void BleSensorData::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Phone connected via BLE (Sensor Data)");
    _parent->_clientConnected = true;
}

void BleSensorData::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "🔌 Phone disconnected from BLE (Sensor Data)");
    _parent->_clientConnected = false;
    _parent->_clientSubscribed = false;
    
    // Trigger disconnect callback if set
    if (_parent && _parent->_disconnectCallback) {
        ESP_LOGI(TAG, "Calling disconnect callback...");
        _parent->_disconnectCallback();
    } else {
        ESP_LOGW(TAG, "⚠️ No disconnect callback set or parent is null");
    }
}
