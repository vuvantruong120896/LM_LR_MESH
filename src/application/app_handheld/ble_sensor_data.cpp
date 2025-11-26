#include "ble_sensor_data.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <ArduinoJson.h>

static const char* TAG = "BleSensorData";

BleSensorData::BleSensorData() 
    : _active(false), _server(nullptr), _sensorDataChar(nullptr), _commandChar(nullptr) {
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
        
        // Initialize BLE device
        NimBLEDevice::init(deviceName.c_str());
        
        // Set BLE power - correct order: powerLevel, powerType
        NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
        
        // Create BLE server
        _server = NimBLEDevice::createServer();
        if (!_server) {
            ESP_LOGE(TAG, "Failed to create BLE server");
            return false;
        }
        
        _server->setCallbacks(new ServerCallbacks(this));
        
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
        
        // Create command characteristic (receive from app)
        _commandChar = service->createCharacteristic(
            COMMAND_CHAR_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        if (!_commandChar) {
            ESP_LOGE(TAG, "Failed to create command characteristic");
            return false;
        }
        
        _commandChar->setCallbacks(new CommandCharCallbacks(this));
        
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
        if (_server) {
            NimBLEDevice::getAdvertising()->stop();
            NimBLEDevice::deinit(true);
            _server = nullptr;
        }
        
        _active = false;
        ESP_LOGI(TAG, "BLE Sensor Data service stopped");
        
    } catch (std::exception& e) {
        ESP_LOGE(TAG, "Exception in stop(): %s", e.what());
    }
}

bool BleSensorData::sendSensorData(const sensorData& data) {
    if (!_active || !_sensorDataChar) {
        ESP_LOGW(TAG, "BLE not active, cannot send sensor data");
        return false;
    }
    
    try {
        // Create JSON with sensor data
        JsonDocument doc;
        doc["temp"] = data.data.soil.soilTemperature;
        doc["moisture"] = data.data.soil.soilMoisture;
        doc["ec"] = data.data.soil.conductivity;
        doc["pH"] = data.data.soil.pH;
        doc["N"] = data.data.soil.nitrogen;
        doc["P"] = data.data.soil.phosphorus;
        doc["K"] = data.data.soil.potassium;
        doc["timestamp"] = millis();
        
        String jsonStr;
        serializeJson(doc, jsonStr);
        
        // Send via BLE notification
        _sensorDataChar->setValue((uint8_t*)jsonStr.c_str(), jsonStr.length());
        _sensorDataChar->notify();
        
        ESP_LOGI(TAG, "Sensor data sent: %s", jsonStr.c_str());
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

void BleSensorData::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Phone connected via BLE (Sensor Data)");
}

void BleSensorData::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Phone disconnected from BLE (Sensor Data)");
}
