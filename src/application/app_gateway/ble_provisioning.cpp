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
    
    // Validate data based on mode
    #ifdef USE_CELLULAR
        // Cellular mode: Only userUID is required
        if (data.userUID.isEmpty()) {
            ESP_LOGE(TAG, "Missing required field: userUID (Cellular mode)");
            
            if (_parent->_responseChar) {
                JsonDocument responseDoc;
                responseDoc["status"] = "error";
                responseDoc["message"] = "Missing required field: userUID";
                String response;
                serializeJson(responseDoc, response);
                _parent->_responseChar->setValue(response.c_str());
                _parent->_responseChar->notify();
            }
            return;
        }
        
        ESP_LOGI(TAG, "Provisioning data validated (Cellular mode):");
        ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
        if (!data.ssid.isEmpty()) {
            ESP_LOGW(TAG, "  SSID provided but not used in Cellular mode");
        }
    #else
        // WiFi mode: ssid and userUID are required
        if (data.ssid.isEmpty() || data.userUID.isEmpty()) {
            ESP_LOGE(TAG, "Missing required fields (WiFi mode: ssid and userUID)");
            
            if (_parent->_responseChar) {
                JsonDocument responseDoc;
                responseDoc["status"] = "error";
                responseDoc["message"] = "Missing required fields: ssid and userUID";
                String response;
                serializeJson(responseDoc, response);
                _parent->_responseChar->setValue(response.c_str());
                _parent->_responseChar->notify();
            }
            return;
        }
        
        ESP_LOGI(TAG, "Provisioning data validated (WiFi mode):");
        ESP_LOGI(TAG, "  SSID: %s", data.ssid.c_str());
        ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
        ESP_LOGI(TAG, "  Password: %s", data.password.isEmpty() ? "<empty>" : "***");
    #endif
    
    // Send success response
    if (_parent->_responseChar) {
        // Get Gateway MAC to include in response (for Node provisioning)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        // Create JSON response with explicit size
        JsonDocument responseDoc;
        responseDoc["status"] = "success";
        responseDoc["gatewayMAC"] = macStr;  // IMPORTANT: Mobile App needs this for Node provisioning
        #ifdef USE_CELLULAR
            responseDoc["message"] = "Provisioning received (Cellular mode)";
            responseDoc["mode"] = "cellular";
        #else
            responseDoc["message"] = "Provisioning received (WiFi mode)";
            responseDoc["mode"] = "wifi";
        #endif
        
        // Serialize to String
        String response;
        serializeJson(responseDoc, response);
        
        // Debug: Log the actual JSON string being sent
        ESP_LOGI(TAG, "JSON response: %s", response.c_str());
        ESP_LOGI(TAG, "JSON response length: %d bytes", response.length());
        
        // Set value with explicit length to prevent truncation
        _parent->_responseChar->setValue((uint8_t*)response.c_str(), response.length());
        _parent->_responseChar->notify();
        ESP_LOGI(TAG, "Success response sent via notify (Gateway MAC: %s)", macStr);
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
