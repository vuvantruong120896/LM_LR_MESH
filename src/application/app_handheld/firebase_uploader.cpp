#include "firebase_uploader.h"
#include "../../components/rs485_soil_sensor/include/sensor_data.h"
#include "esp_log.h"
#include <WiFi.h>

static const char* TAG = "FirebaseUploader";

FirebaseUploader::FirebaseUploader() : 
    isInitialized(false),
    lastError("") {
}

FirebaseUploader::~FirebaseUploader() {
    // Cleanup Firebase connection
    ESP_LOGI(TAG, "Firebase uploader destroyed");
}

bool FirebaseUploader::initialize() {
    if (isInitialized) {
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Firebase connection...");
    
    // Firebase configuration
    FirebaseConfig config;
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;
    
    // Firebase auth
    FirebaseAuth auth;
    auth.user.email = FIREBASE_USER_EMAIL;
    auth.user.password = FIREBASE_USER_PASSWORD;
    
    // Initialize Firebase
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    // Wait for token ready
    ESP_LOGI(TAG, "Getting User UID");
    while ((auth.token.uid) == "") {
        ESP_LOGI(TAG, ".");
        delay(1000);
    }
    
    uid = auth.token.uid.c_str();
    ESP_LOGI(TAG, "User UID: %s", uid.c_str());
    
    isInitialized = true;
    lastError = "";
    
    ESP_LOGI(TAG, "Firebase initialization completed");
    return true;
}

FirebaseUploader::UploadStatus FirebaseUploader::uploadSensorData(const sensorData& data) {
    if (!isInitialized) {
        lastError = "Firebase not initialized";
        return UploadStatus::FAILED;
    }
    
    if (!WiFi.isConnected()) {
        lastError = "WiFi not connected";
        return UploadStatus::NO_WIFI;
    }
    
    ESP_LOGI(TAG, "Uploading sensor data...");
    
    // Create data path with timestamp
    String timestamp = String(millis());
    String dataPath = "/users/" + uid + "/sensor_data/" + timestamp;
    
    // Create JSON document
    FirebaseJson json;
    json.add("timestamp", timestamp);
    json.add("deviceType", (int)data.deviceType);
    json.add("nodeId", data.nodeId);
    json.add("batteryLevel", data.battery);
    json.add("errorCode", data.error);
    
    // Add soil sensor data
    if (data.deviceType == DeviceType::SOIL_SENSOR) {  // Soil sensor
        FirebaseJson soilData;
        soilData.add("temperature", data.data.soil.soilTemperature);
        soilData.add("moisture", data.data.soil.soilMoisture);
        soilData.add("conductivity", data.data.soil.conductivity);
        soilData.add("pH", data.data.soil.pH);
        soilData.add("nitrogen", data.data.soil.nitrogen);
        soilData.add("phosphorus", data.data.soil.phosphorus);
        soilData.add("potassium", data.data.soil.potassium);
        soilData.add("saltContent", data.data.soil.saltContent);
        
        json.add("soilData", soilData);
    }
    
    // Upload to Firebase
    if (Firebase.setJSON(fbdo, dataPath.c_str(), json)) {
        ESP_LOGI(TAG, "Sensor data uploaded successfully");
        lastError = "";
        return UploadStatus::SUCCESS;
    } else {
        lastError = "Upload failed: " + fbdo.errorReason();
        ESP_LOGE(TAG, "Upload failed: %s", lastError.c_str());
        return UploadStatus::FAILED;
    }
}

FirebaseUploader::UploadStatus FirebaseUploader::uploadDeviceStatus(const String& status) {
    if (!isInitialized) {
        lastError = "Firebase not initialized";
        return UploadStatus::FAILED;
    }
    
    if (!WiFi.isConnected()) {
        lastError = "WiFi not connected";
        return UploadStatus::NO_WIFI;
    }
    
    String statusPath = "/users/" + uid + "/device_status";
    
    FirebaseJson json;
    json.add("timestamp", String(millis()));
    json.add("status", status);
    json.add("uptime", millis());
    json.add("freeHeap", ESP.getFreeHeap());
    json.add("wifiRSSI", WiFi.RSSI());
    
    if (Firebase.setJSON(fbdo, statusPath.c_str(), json)) {
        ESP_LOGI(TAG, "Device status uploaded successfully");
        return UploadStatus::SUCCESS;
    } else {
        lastError = "Status upload failed: " + fbdo.errorReason();
        ESP_LOGE(TAG, "Status upload failed: %s", lastError.c_str());
        return UploadStatus::FAILED;
    }
}

String FirebaseUploader::getLastError() const {
    return lastError;
}

bool FirebaseUploader::isConnected() const {
    return isInitialized && WiFi.isConnected() && Firebase.ready();
}

// Static callback for token status
void FirebaseUploader::tokenStatusCallback(TokenInfo info) {
    ESP_LOGI(TAG, "Token info: %s", info.status == token_status_ready ? "ready" : 
             info.status == token_status_on_signing ? "signing" : 
             info.status == token_status_on_request ? "request" : 
             info.status == token_status_on_refresh ? "refresh" : 
             info.status == token_status_on_initialize ? "init" : "unknown");
}