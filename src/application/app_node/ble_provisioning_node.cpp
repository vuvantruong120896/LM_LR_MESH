#include "ble_provisioning_node.h"
#include <ArduinoJson.h>

static const char* TAG = "BLEProvNode";

BleProvisioningNode::BleProvisioningNode() 
    : _active(false), _server(nullptr), _commandChar(nullptr), _responseChar(nullptr) {
}

BleProvisioningNode::~BleProvisioningNode() {
    stop();
}

bool BleProvisioningNode::begin() {
    if (_active) {
        ESP_LOGW(TAG, "BLE already active");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing BLE provisioning for Node...");
    
    // Get device MAC for advertising name
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char deviceName[32];
    snprintf(deviceName, sizeof(deviceName), "KAGRI-NODE-%02X%02X", mac[4], mac[5]);
    
    // Initialize NimBLE
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setMTU(512); // Increase MTU for larger JSON payloads
    
    // Create BLE Server
    _server = NimBLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(this));
    
    // Create Provisioning Service (different UUID from Gateway)
    NimBLEService* service = _server->createService(SERVICE_UUID);
    
    // Create Command Characteristic (Write)
    _commandChar = service->createCharacteristic(
        COMMAND_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    _commandChar->setCallbacks(new CommandCharCallbacks(this));
    
    // Create Response Characteristic (Notify)
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
    ESP_LOGI(TAG, "Service UUID: %s (different from Gateway)", SERVICE_UUID);
    
    return true;
}

void BleProvisioningNode::stop() {
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
void BleProvisioningNode::CommandCharCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
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
    data.userUID = doc["userUID"] | "";
    data.gatewayMAC = doc["gatewayMAC"] | "";
    data.nodeAddress = doc["nodeAddress"] | 0;  // 0 = auto-generate from MAC
    
    // Validate data: userUID and gatewayMAC are required for Node
    if (data.userUID.isEmpty() || data.gatewayMAC.isEmpty()) {
        ESP_LOGE(TAG, "Missing required fields: userUID and gatewayMAC");
        
        if (_parent->_responseChar) {
            JsonDocument responseDoc;
            responseDoc["status"] = "error";
            responseDoc["message"] = "Missing required fields: userUID and gatewayMAC";
            String response;
            serializeJson(responseDoc, response);
            _parent->_responseChar->setValue(response.c_str());
            _parent->_responseChar->notify();
        }
        return;
    }
    
    ESP_LOGI(TAG, "Provisioning data validated:");
    ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
    ESP_LOGI(TAG, "  Gateway MAC: %s", data.gatewayMAC.c_str());
    if (data.nodeAddress != 0) {
        ESP_LOGI(TAG, "  Node Address: 0x%04X (specified by app)", data.nodeAddress);
    } else {
        ESP_LOGI(TAG, "  Node Address: 0x0000 (will auto-generate from MAC)");
    }
    
    // Generate Node address from MAC if not specified
    uint16_t finalNodeAddress = data.nodeAddress;
    if (finalNodeAddress == 0) {
        // Auto-generate from last 2 bytes of MAC
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        finalNodeAddress = (mac[4] << 8) | mac[5];
        ESP_LOGI(TAG, "Auto-generated Node Address: 0x%04X from MAC", finalNodeAddress);
    }
    
    // Send success response with nodeAddress
    if (_parent->_responseChar) {
        StaticJsonDocument<256> responseDoc;
        responseDoc["status"] = "success";
        responseDoc["message"] = "Node provisioning received";
        responseDoc["deviceType"] = "node";
        responseDoc["nodeAddress"] = finalNodeAddress;  // IMPORTANT: Mobile App needs this
        
        String response;
        serializeJson(responseDoc, response);
        
        // Debug: Log the actual JSON string being sent
        ESP_LOGI(TAG, "JSON response: %s", response.c_str());
        ESP_LOGI(TAG, "JSON response length: %d bytes", response.length());
        
        // Set value with explicit length to prevent truncation
        _parent->_responseChar->setValue((uint8_t*)response.c_str(), response.length());
        _parent->_responseChar->notify();
        ESP_LOGI(TAG, "Success response sent via notify (Node Address: 0x%04X)", finalNodeAddress);
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
void BleProvisioningNode::ServerCallbacks::onConnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client connected to Node");
    pServer->updateConnParams(pServer->getPeerInfo(0).getConnHandle(), 24, 48, 0, 60);
}

void BleProvisioningNode::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    ESP_LOGI(TAG, "Client disconnected from Node");
    // Restart advertising
    NimBLEDevice::startAdvertising();
}
