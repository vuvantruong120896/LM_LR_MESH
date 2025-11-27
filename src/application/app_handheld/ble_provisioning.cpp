#include "ble_provisioning.h"
#include <ArduinoJson.h>

static const char* TAG = "BLEProvHHC";

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
    
    ESP_LOGI(TAG, "Initializing BLE provisioning for Handheld...");
    
    // Get device MAC for advertising name
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char deviceName[32];
    // Device advertises as KAGRI-HHC-{LAST_4_CHARS} for mobile app to identify
    snprintf(deviceName, sizeof(deviceName), "KAGRI-HHC-%02X%02X", mac[4], mac[5]);
    
    // Initialize NimBLE
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(512);  // Increase MTU for larger JSON payloads
    
    // Create BLE Server
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(this));
    
    // Create Provisioning Service
    NimBLEService* service = _server->createService(SERVICE_UUID);
    
    // Create Command Characteristic (Write) - receives WiFi credentials from app
    _commandChar = service->createCharacteristic(
        COMMAND_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _commandChar->setCallbacks(new CommandCharCallbacks(this));
    
    // Create Response Characteristic (Notify) - sends status to app
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
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);
    advertising->start();
    
    _active = true;
    ESP_LOGI(TAG, "BLE provisioning started. Device name: %s", deviceName);
    ESP_LOGI(TAG, "Mobile app should scan for device named: %s", deviceName);
    
    return true;
}

void BleProvisioning::stop() {
    if (!_active) return;
    
    ESP_LOGI(TAG, "Stopping BLE provisioning...");
    
    try {
        // Stop advertising first
        NimBLEDevice::getAdvertising()->stop();
        ESP_LOGI(TAG, "Advertising stopped");
        
        // Disconnect all clients
        if (_server) {
            // Get all connected devices and disconnect them
            std::vector<uint16_t> connHandles = _server->getPeerDevices();
            ESP_LOGI(TAG, "Found %d connected clients", connHandles.size());
            for (auto connHandle : connHandles) {
                ESP_LOGI(TAG, "Disconnecting client with handle: %d", connHandle);
                _server->disconnect(connHandle);
            }
        }
        
        // Small delay to allow disconnect to complete
        delay(100);
        
        // Deinit NimBLE safely
        NimBLEDevice::deinit(false);  // false = don't force, allow graceful shutdown
        ESP_LOGI(TAG, "NimBLE deinitialized");
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception during BLE stop: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception during BLE stop");
    }
    
    _active = false;
    _server = nullptr;
    _commandChar = nullptr;
    _responseChar = nullptr;
    
    ESP_LOGI(TAG, "BLE provisioning stopped");
}

// ==================== Characteristic Callbacks ====================

void BleProvisioning::CommandCharCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.empty()) {
        ESP_LOGW(TAG, "Received empty provisioning data");
        return;
    }
    
    ESP_LOGI(TAG, "Received provisioning data: %d bytes", value.length());
    
    // Parse JSON from mobile app
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
            _parent->_responseChar->setValue((uint8_t*)response.c_str(), response.length());
            _parent->_responseChar->notify();
        }
        return;
    }
    
    // Extract WiFi provisioning data from mobile app
    ProvisionData data;
    data.ssid = doc["ssid"] | "";
    data.password = doc["password"] | "";
    data.userUID = doc["userUID"] | "";
    
    // Validate required fields
    if (data.ssid.isEmpty() || data.userUID.isEmpty()) {
        ESP_LOGE(TAG, "Missing required fields (ssid and userUID)");
        ESP_LOGW(TAG, "  ssid: %s", data.ssid.isEmpty() ? "<empty>" : data.ssid.c_str());
        ESP_LOGW(TAG, "  userUID: %s", data.userUID.isEmpty() ? "<empty>" : data.userUID.c_str());
        ESP_LOGW(TAG, "  password: %s", data.password.isEmpty() ? "<empty>" : "***");
        
        if (_parent->_responseChar) {
            JsonDocument responseDoc;
            responseDoc["status"] = "error";
            responseDoc["message"] = "Missing required fields: ssid and userUID";
            String response;
            serializeJson(responseDoc, response);
            _parent->_responseChar->setValue((uint8_t*)response.c_str(), response.length());
            _parent->_responseChar->notify();
        }
        return;
    }
    
    ESP_LOGI(TAG, "Provisioning data received:");
    ESP_LOGI(TAG, "  SSID: %s", data.ssid.c_str());
    ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
    ESP_LOGI(TAG, "  Password: %s", data.password.isEmpty() ? "<empty>" : "***");
    
    // Send success response
    if (_parent->_responseChar) {
        JsonDocument responseDoc;
        responseDoc["status"] = "success";
        responseDoc["message"] = "WiFi credentials received";
        responseDoc["device"] = "KAGRI-HHC";
        
        String response;
        serializeJson(responseDoc, response);
        
        ESP_LOGI(TAG, "Sending success response: %s", response.c_str());
        _parent->_responseChar->setValue((uint8_t*)response.c_str(), response.length());
        _parent->_responseChar->notify();
    }
    
    // Call provisioning callback to process credentials in main app
    if (_parent->_provisionCallback) {
        ESP_LOGI(TAG, "Calling provisioning callback...");
        _parent->_provisionCallback(data);
    } else {
        ESP_LOGE(TAG, "ERROR: Provisioning callback not set!");
    }
}

// ==================== Server Callbacks ====================

void BleProvisioning::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client connected via BLE");
    // Update connection parameters for better throughput
    pServer->updateConnParams(pServer->getPeerInfo(0).getConnHandle(), 24, 48, 0, 60);
}

void BleProvisioning::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client disconnected from BLE");
    // Only restart advertising if provisioning is still active
    if (_parent && _parent->isActive()) {
        NimBLEDevice::startAdvertising();
    }
}
