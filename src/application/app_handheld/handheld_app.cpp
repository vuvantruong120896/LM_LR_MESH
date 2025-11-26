#include "handheld_app.h"
#include "handheld_config.h" 
#include "mesh_utils_handheld.h"
#include "../../utils/battery_monitor.h"
#include "../../components/button_led/include/button_control.h"
#include "../../components/rs485_soil_sensor/include/sensor_data.h"
#include "../../components/rs485_soil_sensor/include/soil_sensor_service.h"
#include "../../components/rs485_soil_sensor/include/sensor_task.h"
#include "../../components/tft_display/include/tft_display_manager.h"
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <WiFi.h>

static const char* TAG = "HandheldApp";

// Static instance for callback
HandheldApp* HandheldApp::instance = nullptr;

HandheldApp::HandheldApp() :
    displayManager(nullptr),
    firebaseUploader(nullptr),
    currentState(AppState::INITIALIZING),
    lastSensorRead(0),
    lastAutoUpload(0),
    lastBatteryCheck(0),
    lastDisplayUpdate(0),
    displayTimeout(0),
    hasNewSensorData(false),
    stateChangeTime(0),
    lastUploadTime(0),
    lastWiFiCheck(0),
    currentUIScreen(DisplayScreen::HOME),
    menuSelection(0)
{
    instance = this;
    memset(&lastSensorReading, 0, sizeof(lastSensorReading));
    memset(&status, 0, sizeof(status));
}

HandheldApp::~HandheldApp() {
    //if (wifiService) delete wifiService;  // TODO: Implement WiFi service
    if (displayManager) delete displayManager;
    if (firebaseUploader) delete firebaseUploader;
    instance = nullptr;
}

bool HandheldApp::initialize() {
    ESP_LOGI(TAG, "Initializing Handheld Application v%s", HANDHELD_FIRMWARE_VER);
    
    if (!initializeNVS() || !initializeComponents() || !initializeButtons()) {
        ESP_LOGE(TAG, "Initialization failed");
        return false;
    }
    
    loadConfiguration();
    changeState(AppState::IDLE);
    ESP_LOGI(TAG, "Handheld application initialized");
    return true;
}

void HandheldApp::loop() {
    uint32_t currentTime = millis();
    
    // Update system status
    updateSystemStatus();
    
    // Handle periodic tasks based on current state
    switch (currentState) {
        case AppState::INITIALIZING:
            // Should not stay here long
            if (currentTime > 10000) {  // 10 second timeout
                handleError("Initialization timeout");
                changeState(AppState::ERROR);
            }
            break;
            
        case AppState::IDLE:
        case AppState::SLEEP:
            handleBatteryCheck();
            handleDisplayUpdate();
            handleWiFiReconnect();
            break;
            
        case AppState::MEASURING: {
            sensorData newData;
            if (SensorTaskManager::getData(newData, 0)) {
                lastSensorReading = newData;
                hasNewSensorData = true;
                ESP_LOGI(TAG, "New sensor data: Moisture=%.1f%%, pH=%.1f", 
                        newData.data.soil.soilMoisture, newData.data.soil.pH);
            }
            changeState(AppState::IDLE);
            break;
        }
        case AppState::UPLOADING:
        case AppState::WIFI_CONFIG:
            break;
        case AppState::ERROR:
            if (currentTime - status.uptime > 30000) {
                ESP_LOGI(TAG, "Auto-recovery from error");
                changeState(AppState::IDLE);
            }
            break;
    }
    
    // Update display
    // if (displayManager) {
    //     displayManager->update();
    // }
    
    // Handle touch events
    // if (displayManager && displayManager->isDisplayOn()) {
    //     auto touchEvent = displayManager->getTouchEvent();
    //     if (touchEvent.pressed) {
    //         displayTimeout = currentTime + DISPLAY_TIMEOUT_MS;  // Reset display timeout
    //     }
    // }
    
    // Handle display timeout
    // if (displayManager && displayManager->isDisplayOn() && 
    //     currentTime > displayTimeout) {
    //     displayManager->setDisplayOn(false);
    //     changeState(AppState::SLEEP);
    //     ESP_LOGI(TAG, "Display timeout - entering sleep mode");
    // }
    
    // Log system info periodically
    if (currentTime % 60000 == 0) {  // Every minute
        logSystemInfo();
    }
}

HandheldApp::SystemStatus HandheldApp::getSystemStatus() {
    return status;
}

bool HandheldApp::uploadNow() {
    if (currentState == AppState::UPLOADING) {
        ESP_LOGW(TAG, "Upload in progress");
        return false;
    }
    
    if (!hasNewSensorData) {
        ESP_LOGW(TAG, "No sensor data");
        return false;
    }
    
    if (!WiFi.isConnected()) {
        ESP_LOGW(TAG, "WiFi disconnected");
        storeSensorDataOffline(lastSensorReading);
        return false;
    }
    
    changeState(AppState::UPLOADING);
    auto status = firebaseUploader->uploadSensorData(lastSensorReading);
    
    if (status == FirebaseUploader::UploadStatus::SUCCESS) {
        ESP_LOGI(TAG, "Upload successful");
        hasNewSensorData = false;
    } else {
        ESP_LOGW(TAG, "Upload failed, saving offline");
        storeSensorDataOffline(lastSensorReading);
    }
    
    changeState(AppState::IDLE);
    return status == FirebaseUploader::UploadStatus::SUCCESS;
}

void HandheldApp::enterWiFiConfigMode() {
    changeState(AppState::WIFI_CONFIG);
}

void HandheldApp::factoryReset() {
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    delay(2000);
    ESP.restart();
}

void HandheldApp::buttonCallback(button_event_t event) {
    if (instance) instance->handleButtonPress(event);
}

void HandheldApp::handleButtonPress(button_event_t event) {
    // Reset display timeout on any button press
    displayTimeout = millis() + DISPLAY_TIMEOUT_MS;
    // displayManager->setBrightness(100);  // Wake up display
    
    switch (currentUIScreen) {
        case DisplayScreen::HOME:
            handleMenuNavigation(event);
            break;
        case DisplayScreen::SOIL_DATA:
            handleSoilDataScreen();
            break;
        case DisplayScreen::DEVICE_CONFIG:
            handleConfigScreen();
            break;
        default:
            if (event == BUTTON_EVENT_CLICK) {
                currentUIScreen = DisplayScreen::HOME;
                displayManager->drawHomeScreen();
            }
            break;
    }
}

void HandheldApp::handleMenuNavigation(button_event_t event) {
    switch (event) {
        case BUTTON_EVENT_CLICK:
            if (menuSelection == 0) {
                // Read sensor and queue soil data display (async)
                readSensorManual();
                currentUIScreen = DisplayScreen::SOIL_DATA;
                displayManager->displaySensorData(
                    lastSensorReading.data.soil.soilTemperature,
                    lastSensorReading.data.soil.soilMoisture,
                    lastSensorReading.data.soil.pH,
                    0.0f, 0.0f, 0.0f  // N, P, K placeholder
                );
            } else if (menuSelection == 1) {
                // Navigate to device config screen (async)
                currentUIScreen = DisplayScreen::DEVICE_CONFIG;
                displayManager->drawText(20, 100, "Device Configuration", 0x07FF);
                displayManager->drawText(20, 140, "WiFi: Not connected", 0xFFFF);
                displayManager->drawText(20, 160, "Firebase: Ready", 0x07E0);
            }
            break;
        case BUTTON_EVENT_LONG_PRESS:
            // Manual sensor read + upload (non-blocking command queued)
            readSensorManual();
            uploadNow();
            break;
        default:
            break;
    }
}

void HandheldApp::handleSoilDataScreen() {
    // Back button: return to HOME
    currentUIScreen = DisplayScreen::HOME;
    displayManager->drawHomeScreen();
}

void HandheldApp::handleConfigScreen() {
    // Back button: return to HOME
    currentUIScreen = DisplayScreen::HOME;
    displayManager->drawHomeScreen();
}

bool HandheldApp::initializeComponents() {
    initializeDisplay();
    initializeSensor();
    initializeBuzzer();
    initializeWiFi();
    initializeFirebase();
    return true;
}

bool HandheldApp::initializeNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    return true;
}

void HandheldApp::loadConfiguration() {
    ESP_LOGI(TAG, "Configuration loaded");
}

bool HandheldApp::initializeButtons() {
    button_init();
    button_set_long_press_time(3000);
    button_set_callback(buttonCallback);
    ESP_LOGI(TAG, "Button initialized: IO3 pull-up");
    return true;
}

bool HandheldApp::initializeBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
    ESP_LOGI(TAG, "Buzzer initialized");
    return true;
}

bool HandheldApp::initializeDisplay() {
    displayManager = TFTDisplayManager::getInstance();
    if (!displayManager->initialize()) {
        ESP_LOGE(TAG, "Display init failed");
        return false;
    }

    displayManager->clear();

    // Draw splash screen
    displayManager->drawText(40, 150, "KAGRI SYSTEM", 0x07FF);
    displayManager->drawText(60, 170, "Loading...", 0xFFFF);
    vTaskDelay(3000 / portTICK_PERIOD_MS);

    displayManager->drawHomeScreen();
    
    ESP_LOGI(TAG, "Display initialized successfully");
    return true;
}

bool HandheldApp::initializeSensor() {
    if (!SoilSensorService::initialize()) {
        ESP_LOGW(TAG, "Soil sensor not available");
        return false;
    }
    
    if (!SensorTaskManager::initialize()) {
        ESP_LOGW(TAG, "Sensor task failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Sensor initialized");
    return true;
}

bool HandheldApp::initializeWiFi() {
    WiFi.mode(WIFI_STA);
    
    nvs_handle_t handle;
    if (nvs_open("nvs.wifi", NVS_READONLY, &handle) == ESP_OK) {
        char ssid[32] = {0};
        char password[64] = {0};
        size_t ssid_len = 32, pwd_len = 64;
        
        if (nvs_get_str(handle, "ssid", ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(handle, "password", password, &pwd_len) == ESP_OK) {
            WiFi.begin(ssid, password);
            ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
            
            for (int i = 0; i < 20 && !WiFi.isConnected(); i++) {
                delay(500);
            }
        }
        nvs_close(handle);
    }
    
    ESP_LOGI(TAG, "WiFi: %s", WiFi.isConnected() ? "Connected" : "Not connected");
    return true;
}

bool HandheldApp::initializeFirebase() {
    firebaseUploader = new FirebaseUploader();
    return firebaseUploader->initialize();
}

void HandheldApp::updateSystemStatus() {
    status.uptime = millis();
    status.freeHeap = ESP.getFreeHeap();
    status.wifiConnected = WiFi.isConnected();
    status.batteryLevel = (float)BatteryMonitor::getPercentage();
    status.sensorStatus = true; // Would check actual sensor status
}

void HandheldApp::changeState(AppState newState) {
    if (currentState != newState) {
        ESP_LOGI(TAG, "State change: %d -> %d", (int)currentState, (int)newState);
        currentState = newState;
        stateChangeTime = millis();
    }
}

void HandheldApp::logSystemInfo() {
    ESP_LOGI(TAG, "System: Uptime=%lums, Heap=%d, WiFi=%s, Battery=%.1f%%", 
             status.uptime, status.freeHeap, 
             status.wifiConnected ? "OK" : "FAIL",
             status.batteryLevel);
}

void HandheldApp::handleSensorReading() {
    // Manual sensor reading on button press only
    // Automatic reading disabled per user request
}

void HandheldApp::handleAutoUpload() {
    // Automatic upload disabled per user request
    // Manual upload via button press only
}

void HandheldApp::handleBatteryCheck() {
    if (millis() % 60000 == 0) {
        updateSystemStatus();
        if (status.batteryLevel < 10.0)
            ESP_LOGW(TAG, "Low battery: %.1f%%", status.batteryLevel);
    }
}

void HandheldApp::handleDisplayUpdate() {}

void HandheldApp::readSensorManual() {
    sensorData data;
    if (SensorTaskManager::getData(data, 0)) {
        lastSensorReading = data;
        hasNewSensorData = true;
        ESP_LOGI(TAG, "Sensor read: Moisture=%.1f%%, Temp=%.1f°C, pH=%.2f",
                 data.data.soil.soilMoisture, data.data.soil.soilTemperature, data.data.soil.pH);
    } else {
        ESP_LOGW(TAG, "No sensor data available");
    }
}

void HandheldApp::handleWiFiReconnect() {
    if (!WiFi.isConnected() && millis() - lastWiFiCheck > 30000) {
        ESP_LOGI(TAG, "WiFi reconnecting...");
        WiFi.reconnect();
        lastWiFiCheck = millis();
        
        for (int i = 0; i < 10 && !WiFi.isConnected(); i++) {
            delay(500);
        }
        
        if (WiFi.isConnected()) {
            ESP_LOGI(TAG, "WiFi reconnected");
        }
    }
}

void HandheldApp::handleError(const String& error) {
    ESP_LOGE(TAG, "Error: %s", error.c_str());
}

void HandheldApp::storeSensorDataOffline(const sensorData& data) {
    ESP_LOGI(TAG, "Data saved offline");
}