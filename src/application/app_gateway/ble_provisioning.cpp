#include "ble_provisioning.h"
#include <ArduinoJson.h>

static const char* TAG = "BLEProv";

BleProvisioning::BleProvisioning() 
    : _active(false), _server(nullptr), _commandChar(nullptr), _responseChar(nullptr) {
}

BleProvisioning::~BleProvisioning() {
    stop();
}

bool BleProvisioning::begin() {
    if (_active) {
        ESP_LOGW(TAG, "BLE already active");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing BLE provisioning...");
    
    // Get device MAC for advertising name
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char deviceName[32];
    snprintf(deviceName, sizeof(deviceName), "KAGRI-GW-%02X%02X", mac[4], mac[5]);
    
    // Initialize NimBLE
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(512); // Increase MTU for larger JSON payloads
    
    // Create BLE Server
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(this));
    
    // Create Provisioning Service
    NimBLEService* service = _server->createService(SERVICE_UUID);
    
    // Create Command Characteristic (Write)
    _commandChar = service->createCharacteristic(
        COMMAND_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _commandChar->setCallbacks(new CommandCharCallbacks(this));
    
    // Create Response Characteristic (Notify - optional)
    _responseChar = service->createCharacteristic(
        RESPONSE_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );
    
    // Start service
    service->start();
    
    // Start advertising
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    advertising->setMaxPreferred(0x12);
    advertising->start();
    
    _active = true;
    ESP_LOGI(TAG, "BLE provisioning started. Device name: %s", deviceName);
    
    return true;
}

void BleProvisioning::stop() {
    if (!_active) return;
    
    ESP_LOGI(TAG, "Stopping BLE provisioning...");
    
    if (_server) {
        NimBLEDevice::getAdvertising()->stop();
        _server->disconnect(0); // Disconnect all clients
    }
    
    NimBLEDevice::deinit(true);
    
    _active = false;
    _server = nullptr;
    _commandChar = nullptr;
    _responseChar = nullptr;
    
    ESP_LOGI(TAG, "BLE provisioning stopped");
}

// Command Characteristic Write Callback
void BleProvisioning::CommandCharCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.empty()) {
        ESP_LOGW(TAG, "Received empty provisioning data");
        return;
    }
    
    ESP_LOGI(TAG, "Received provisioning data: %d bytes", value.length());
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, value);
    
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        
        // Send error response
        if (_parent->_responseChar) {
            JsonDocument responseDoc;
            responseDoc["status"] = "error";
            responseDoc["message"] = "Invalid JSON";
            String response;
            serializeJson(responseDoc, response);
            _parent->_responseChar->setValue(response.c_str());
            _parent->_responseChar->notify();
        }
        return;
    }
    
    // Extract provisioning data
    ProvisionData data;
    data.ssid = doc["ssid"] | "";
    data.password = doc["password"] | "";
    data.userUID = doc["userUID"] | "";
    
    // Validate data
    if (data.ssid.isEmpty() || data.userUID.isEmpty()) {
        ESP_LOGE(TAG, "Missing required fields (ssid or userUID)");
        
        if (_parent->_responseChar) {
            JsonDocument responseDoc;
            responseDoc["status"] = "error";
            responseDoc["message"] = "Missing required fields";
            String response;
            serializeJson(responseDoc, response);
            _parent->_responseChar->setValue(response.c_str());
            _parent->_responseChar->notify();
        }
        return;
    }
    
    ESP_LOGI(TAG, "Provisioning data validated:");
    ESP_LOGI(TAG, "  SSID: %s", data.ssid.c_str());
    ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
    ESP_LOGI(TAG, "  Password: %s", data.password.isEmpty() ? "<empty>" : "***");
    
    // Send success response
    if (_parent->_responseChar) {
        JsonDocument responseDoc;
        responseDoc["status"] = "success";
        responseDoc["message"] = "Provisioning received";
        String response;
        serializeJson(responseDoc, response);
        _parent->_responseChar->setValue(response.c_str());
        _parent->_responseChar->notify();
        ESP_LOGI(TAG, "Success response sent via notify");
    } else {
        ESP_LOGW(TAG, "Response characteristic not available");
    }
    
    // Call provisioning callback
    if (_parent->_provisionCallback) {
        ESP_LOGI(TAG, "Calling provisioning callback...");
        _parent->_provisionCallback(data);
    } else {
        ESP_LOGE(TAG, "ERROR: Provisioning callback not set!");
    }
}

// Server Connection Callbacks
void BleProvisioning::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client connected");
    pServer->updateConnParams(pServer->getPeerInfo(0).getConnHandle(), 24, 48, 0, 60);
}

void BleProvisioning::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client disconnected");
    // Restart advertising
    NimBLEDevice::startAdvertising();
}
