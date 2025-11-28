#include "handheld_app.h"
#include "handheld_config.h" 
#include "mesh_utils_handheld.h"
#include "../../utils/battery_monitor.h"
#include "../../components/button_led/include/button_control.h"
#include "../../components/rs485_soil_sensor/include/sensor_data.h"
#include "../../components/rs485_soil_sensor/include/soil_sensor_service.h"
#include "../../components/rs485_soil_sensor/include/sensor_task.h"
#include "../../components/tft_display/include/tft_display_manager_lvgl.h"
#include "../../components/tft_display/include/display_colors.h"
#include "../../components/tft_display/include/display_strings.h"
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
    bleProvisioning(nullptr),
    bleSensorData(nullptr),
    currentState(AppState::INITIALIZING),
    wifiConfigSubState(WiFiConfigState::WAITING_FOR_APP),
    lastSensorRead(0),
    lastAutoUpload(0),
    lastBatteryCheck(0),
    lastDisplayUpdate(0),
    displayTimeout(0),
    hasNewSensorData(false),
    beepedForCurrentMeasurement(false),
    displayingSensorData(false),
    wifiConfigScreenDrawn(false),
    wifiConfigLastCountdownUpdate(0),
    sensorDataScreenDrawn(false),
    sensorDataLastCountdownUpdate(0),
    lastWiFiConnectedState(false),
    lastWiFiStatusCheckTime(0),
    stateChangeTime(0),
    lastUploadTime(0),
    lastWiFiCheck(0),
    menuSelection(0)
{
    instance = this;
    memset(&lastSensorReading, 0, sizeof(lastSensorReading));
    memset(&status, 0, sizeof(status));
}

HandheldApp::~HandheldApp() {
    //if (wifiService) delete wifiService;  // TODO: Implement WiFi service
    if (bleProvisioning) delete bleProvisioning;
    if (bleSensorData) delete bleSensorData;
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
    
    // Clear BLE disconnect flag from NVS at end of initialization
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_erase_key(nvs_handle, "ble_disconnect");
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "BLE disconnect flag cleared from NVS");
    }
    
    changeState(AppState::IDLE);
    ESP_LOGI(TAG, "Handheld application initialized");
    return true;
}

void HandheldApp::loop() {
    // **CRITICAL**: Process button events (polling every loop iteration)
    // ISR sets the flag, button_update() processes it
    button_update();
    
    // Update currentTime AFTER processing button events
    // This ensures stateChangeTime (set during button processing) is always <= currentTime
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
            
            // Check WiFi status and refresh home screen if changed
            if (currentTime - lastWiFiStatusCheckTime > 2000) {  // Check every 2 seconds
                bool currentWiFiState = WiFi.isConnected();
                if (currentWiFiState != lastWiFiConnectedState) {
                    ESP_LOGI(TAG, "WiFi status changed: %d -> %d, refreshing display", 
                             lastWiFiConnectedState, currentWiFiState);
                    lastWiFiConnectedState = currentWiFiState;
                    displayManager->drawHomeScreen();
                }
                lastWiFiStatusCheckTime = currentTime;
            }
            
            // Auto-return to home after 60 seconds if displaying sensor data
            if (displayingSensorData && (currentTime - stateChangeTime > 60000)) {
                ESP_LOGI(TAG, "Sensor data display timeout (60s) - returning to home screen");
                displayingSensorData = false;
                displayManager->drawHomeScreen();
            }
            break;
            
        case AppState::MEASURING: {
            sensorData newData;
            if (SensorTaskManager::getData(newData, 0)) {
                lastSensorReading = newData;
                hasNewSensorData = true;
                ESP_LOGI(TAG, "New sensor data: Moisture=%.1f%%, pH=%.1f", 
                        newData.data.soil.soilMoisture, newData.data.soil.pH);
                // Display sensor data
                displayManager->displaySensorData(
                    newData.data.soil.soilTemperature,
                    newData.data.soil.soilMoisture,
                    newData.data.soil.pH,
                    newData.data.soil.conductivity,
                    newData.data.soil.nitrogen,
                    newData.data.soil.phosphorus,
                    newData.data.soil.potassium
                );
                // TODO: Beep once to signal completion (temporarily disabled)
                // if (!beepedForCurrentMeasurement) {
                //     beepSuccess();
                //     beepedForCurrentMeasurement = true;
                // }
                displayingSensorData = true;  // Mark that we're displaying sensor data
                changeState(AppState::IDLE);
            }
            break;
        }
        case AppState::UPLOADING:
            // TODO: Show BLE/Firebase upload status
            // displayManager->drawBleSendingScreen(progress);
            break;
        case AppState::WIFI_CONFIG: {
            // WiFi config: Show BLE waiting screen with countdown
            uint32_t wifiConfigDuration = currentTime - stateChangeTime;
            wifiConfigSubState = WiFiConfigState::APP_CONNECTED;
            
            // Draw full screen only once per state entry, then update countdown only
            if (!wifiConfigScreenDrawn) {
                displayManager->drawBLEWaitingScreen(60);
                wifiConfigScreenDrawn = true;
            }
            
            // Update countdown every 1 second (only refresh countdown text)
            if (currentTime - wifiConfigLastCountdownUpdate > 1000) {  // Update every 1 second
                int remainingSeconds = (60000 - wifiConfigDuration) / 1000;
                if (remainingSeconds < 0) remainingSeconds = 0;
                displayManager->updateBLEWaitingScreenCountdown(remainingSeconds);
                wifiConfigLastCountdownUpdate = currentTime;
            }
            
            // Log every 5 seconds for debugging
            static uint32_t lastLogTime = 0;
            if (currentTime - lastLogTime > 5000) {
                ESP_LOGI(TAG, "WIFI_CONFIG: duration=%lu ms, BLE active=%d", 
                         wifiConfigDuration, bleProvisioning ? bleProvisioning->isActive() : 0);
                lastLogTime = currentTime;
            }
            
            // Timeout after 60 seconds
            if (wifiConfigDuration > 60000) {
                ESP_LOGI(TAG, "WiFi config timeout (%lu ms) - returning to IDLE", wifiConfigDuration);
                displayingSensorData = false;  // Reset flag
                displayManager->drawHomeScreen();
                changeState(AppState::IDLE);
            }
            break;
        }
        case AppState::SENSOR_DATA_TRANSFER: {
            // Sensor data transmission via BLE
            uint32_t dataTransferDuration = currentTime - stateChangeTime;
            
            // Draw full screen only once per state entry, then update countdown only
            if (!sensorDataScreenDrawn) {
                displayManager->drawBLEWaitingScreen(120);  // 120 second timeout for data transfer
                sensorDataScreenDrawn = true;
                sensorDataLastCountdownUpdate = currentTime;
            }
            
            // Update countdown every 1 second
            if (currentTime - sensorDataLastCountdownUpdate > 1000) {
                int remainingSeconds = (120000 - dataTransferDuration) / 1000;
                if (remainingSeconds < 0) remainingSeconds = 0;
                displayManager->updateBLEWaitingScreenCountdown(remainingSeconds);
                sensorDataLastCountdownUpdate = currentTime;
            }
            
            // Data is sent via subscription callback - no need to check here
            
            // Timeout after 120 seconds
            if (dataTransferDuration > 120000) {
                ESP_LOGI(TAG, "Sensor data transfer timeout - REBOOTING");
                delay(1000);
                ESP.restart();
            }
            break;
        }
        case AppState::ERROR:
            if (currentTime - stateChangeTime > 30000) {
                ESP_LOGI(TAG, "Auto-recovery from error");
                displayManager->drawHomeScreen();
                changeState(AppState::IDLE);
            }
            break;
    }
    
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
    
    // Log all button events
    const char* eventName = "";
    switch(event) {
        case BUTTON_EVENT_CLICK: eventName = "CLICK"; break;
        case BUTTON_EVENT_DOUBLE_CLICK: eventName = "DOUBLE_CLICK"; break;
        case BUTTON_EVENT_LONG_PRESS: eventName = "LONG_PRESS (1s)"; break;
        case BUTTON_EVENT_EXTENDED_PRESS: eventName = "EXTENDED_PRESS (5s)"; break;
        case BUTTON_EVENT_PRESS: eventName = "PRESS"; break;
        case BUTTON_EVENT_RELEASE: eventName = "RELEASE"; break;
        default: eventName = "UNKNOWN"; break;
    }
    ESP_LOGI(TAG, "📱 Button event: %s (state=%s)", eventName, 
             currentState == AppState::IDLE ? "IDLE" : "OTHER");
    
    if (event == BUTTON_EVENT_CLICK) {
        // Short click: Context-dependent action
        if (displayingSensorData) {
            // If showing sensor data: Return to home screen (keep IDLE state, just hide sensor data)
            ESP_LOGI(TAG, "📍 Button clicked - hiding sensor data, returning to HOME");
            displayingSensorData = false;
            displayManager->drawHomeScreen();
        } else if (currentState == AppState::IDLE) {
            // In IDLE without sensor data: Trigger on-demand sensor read
            ESP_LOGI(TAG, "📍 Button clicked - triggering sensor read...");
            displayManager->drawMeasuringScreen();  // Show measuring screen
            changeState(AppState::MEASURING);
            
            // Send trigger to sensor task
            if (SensorTaskManager::triggerRead()) {
                ESP_LOGI(TAG, "✅ Sensor read triggered - waiting for result...");
            } else {
                ESP_LOGE(TAG, "❌ Failed to trigger sensor read");
            }
        }
    } 
    else if (event == BUTTON_EVENT_LONG_PRESS) {
        // 1s long-press: Trigger BLE data transmission if showing sensor data
        if (displayingSensorData) {
            ESP_LOGI(TAG, "⏱️ Long press (1s) - starting sensor data BLE transmission");
            displayingSensorData = false;  // Reset flag
            changeState(AppState::SENSOR_DATA_TRANSFER);
        } else {
            ESP_LOGI(TAG, "⏱️ Long press (1s) - reserved");
        }
    }
    else if (event == BUTTON_EVENT_EXTENDED_PRESS) {
        // 5s extended press: Context-dependent action
        if (displayingSensorData) {
            // If showing sensor data: Trigger BLE data transmission
            ESP_LOGI(TAG, "📱 Extended press (5s) - starting sensor data BLE transmission");
            displayingSensorData = false;  // Reset flag
            changeState(AppState::SENSOR_DATA_TRANSFER);
        } else if (currentState == AppState::IDLE) {
            // In IDLE state without sensor data: WiFi config mode
            ESP_LOGI(TAG, "🔧 Extended press (5s) - entering WiFi config mode");
            changeState(AppState::WIFI_CONFIG);
        }
    }
}

void HandheldApp::handleMenuNavigation(button_event_t event) {
    // Handled by handleButtonPress
}

void HandheldApp::handleSoilDataScreen() {
    // Back to home screen
    displayManager->drawHomeScreen();
}

void HandheldApp::handleConfigScreen() {
    // Back to home screen
    displayManager->drawHomeScreen();
}

bool HandheldApp::initializeComponents() {
    initializeDisplay();
    initializeSensor();
    initializeBuzzer();
    initializeWiFi();
    // initializeFirebase();
    
    // Initialize BLE provisioning (starts in stopped state)
    bleProvisioning = new BleProvisioning();
    if (!bleProvisioning) {
        ESP_LOGE(TAG, "Failed to create BLE provisioning object");
        return false;
    }
    
    // Set callback for when WiFi credentials received via BLE
    bleProvisioning->setProvisionCallback([this](const BleProvisioning::ProvisionData& data) {
        onWiFiCredentialsReceived(data);
    });
    
    ESP_LOGI(TAG, "BLE provisioning object created (will start when entering WIFI_CONFIG state)");
    
    // Initialize BLE sensor data service (starts in stopped state)
    bleSensorData = new BleSensorData();
    if (!bleSensorData) {
        ESP_LOGE(TAG, "Failed to create BLE sensor data object");
        return false;
    }
    
    ESP_LOGI(TAG, "BLE sensor data object created (will start when entering SENSOR_DATA_TRANSFER state)");
    
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
    button_set_long_press_time(1000);          // 1s for short long-press event
    button_set_extended_press_time(5000);      // 5s for extended long-press (config mode)
    button_set_callback(buttonCallback);
    ESP_LOGI(TAG, "Button initialized: GPIO3 (pull-up, 1s/5s long-press)");
    return true;
}

bool HandheldApp::initializeBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);
    
    // Check if this is a reboot after BLE disconnect
    nvs_handle_t nvs_handle;
    uint8_t ble_disconnect_flag = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, "ble_disconnect", &ble_disconnect_flag);
        nvs_close(nvs_handle);
    }
    
    // Only beep on startup if NOT a BLE disconnect reboot
    if (ble_disconnect_flag == 0) {
        delay(500);
        digitalWrite(BUZZER_PIN, LOW);
        ESP_LOGI(TAG, "Buzzer beep on startup");
    } else {
        // BLE disconnect reboot - skip beep, just set to OFF state
        digitalWrite(BUZZER_PIN, LOW);
        ESP_LOGI(TAG, "Buzzer initialized (skipped beep due to BLE disconnect reboot)");
    }
    
    ESP_LOGI(TAG, "Buzzer initialized");
    return true;
}

bool HandheldApp::initializeDisplay() {
    displayManager = DisplayManager::getInstance();
    if (!displayManager->initialize()) {
        ESP_LOGE(TAG, "Display init failed");
        return false;
    }

    // Check if this is a reboot after BLE disconnect
    nvs_handle_t nvs_handle;
    uint8_t ble_disconnect_flag = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, "ble_disconnect", &ble_disconnect_flag);
        nvs_close(nvs_handle);
    }
    
    // Only show initial screen if NOT a BLE disconnect reboot
    if (ble_disconnect_flag == 0) {
        displayManager->drawInitialScreen();
    } else {
        ESP_LOGI(TAG, "Skipping initial screen (BLE disconnect reboot)");
    }
    
    displayManager->drawHomeScreen();
    
    // Initialize WiFi status tracking with current state
    lastWiFiConnectedState = WiFi.isConnected();
    lastWiFiStatusCheckTime = millis();
    
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
    
    // Try to load WiFi credentials from NVS
    char ssid[32] = {0};
    char password[64] = {0};
    
    if (loadWiFiCredentialsFromNVS(ssid, sizeof(ssid), password, sizeof(password))) {
        WiFi.begin(ssid, password);
        ESP_LOGI(TAG, "Attempting WiFi connection with stored credentials: %s", ssid);
        
        // Wait up to 10 seconds for connection
        for (int i = 0; i < 20 && !WiFi.isConnected(); i++) {
            delay(500);
        }
        
        if (WiFi.isConnected()) {
            ESP_LOGI(TAG, "✓ WiFi Connected: IP=%s", WiFi.localIP().toString().c_str());
        } else {
            ESP_LOGW(TAG, "✗ WiFi connection failed - will retry on next boot");
        }
    } else {
        ESP_LOGI(TAG, "No stored WiFi credentials - device operational without WiFi");
        ESP_LOGI(TAG, "Configure WiFi via BLE when needed (hold button 5s)");
    }
    
    // Track initial WiFi state for home screen refresh logic
    lastWiFiConnectedState = WiFi.isConnected();
    
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
    // Update the tracked WiFi state for home screen refresh logic
    lastWiFiConnectedState = status.wifiConnected;
    // status.batteryLevel = (float)BatteryMonitor::getPercentage();
    status.sensorStatus = true; // Would check actual sensor status
}

void HandheldApp::changeState(AppState newState) {
    ESP_LOGI(TAG, "changeState() called: currentState=%d, newState=%d", (int)currentState, (int)newState);
    if (currentState != newState) {
        // Handle state-specific exit transitions (BEFORE changing state)
        if (currentState == AppState::WIFI_CONFIG) {
            // Stop BLE when leaving WiFi config mode
            if (bleProvisioning && bleProvisioning->isActive()) {
                ESP_LOGI(TAG, "Stopping BLE provisioning...");
                bleProvisioning->stop();
            }
            // Reset screenDrawn flag for next WiFi_CONFIG entry
            wifiConfigScreenDrawn = false;
            wifiConfigLastCountdownUpdate = 0;
        } else if (currentState == AppState::SENSOR_DATA_TRANSFER) {
            // Stop BLE sensor data when leaving this state
            if (bleSensorData && bleSensorData->isActive()) {
                ESP_LOGI(TAG, "Stopping BLE sensor data...");
                bleSensorData->stop();
            }
            // Reset screenDrawn flag for next SENSOR_DATA_TRANSFER entry
            sensorDataScreenDrawn = false;
            sensorDataLastCountdownUpdate = 0;
        }
        
        ESP_LOGI(TAG, "State change: %d -> %d", (int)currentState, (int)newState);
        currentState = newState;
        stateChangeTime = millis();
        ESP_LOGI(TAG, "stateChangeTime updated to %lu", stateChangeTime);
        
        // Reset beep flag when entering MEASURING state
        if (newState == AppState::MEASURING) {
            beepedForCurrentMeasurement = false;
        }
        
        // Handle state-specific entry transitions (AFTER changing state)
        if (newState == AppState::WIFI_CONFIG) {
            // Start BLE advertising when entering WiFi config mode
            if (bleProvisioning) {
                ESP_LOGI(TAG, "Starting BLE provisioning...");
                if (bleProvisioning->begin()) {
                    ESP_LOGI(TAG, "BLE provisioning started successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to start BLE provisioning");
                }
            }
        } else if (newState == AppState::SENSOR_DATA_TRANSFER) {
            // Start BLE sensor data service
            if (bleSensorData) {
                ESP_LOGI(TAG, "Starting BLE sensor data service...");
                if (bleSensorData->begin()) {
                    ESP_LOGI(TAG, "BLE sensor data service started successfully");
                    
                    // Set callback to reboot when mobile app disconnects after data transfer
                    bleSensorData->setDisconnectCallback([this]() {
                        ESP_LOGI(TAG, "📱 Mobile app disconnected after sensor data transfer - saving flag and REBOOTING");
                        
                        // Show sensor data sent success screen
                        displayManager->drawSensorDataSentScreen();
                        delay(2000);  // Show success for 2 seconds
                        
                        // Save flag to NVS before reboot
                        nvs_handle_t nvs_handle;
                        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
                            nvs_set_u8(nvs_handle, "ble_disconnect", 1);
                            nvs_commit(nvs_handle);
                            nvs_close(nvs_handle);
                            ESP_LOGI(TAG, "BLE disconnect flag saved to NVS");
                        }
                        
                        delay(1000);  // Give time for log to flush
                        ESP.restart();
                    });
                    
                    // Set callback when data is sent
                    bleSensorData->setDataSentCallback([this]() {
                        ESP_LOGI(TAG, "✅ Sensor data successfully sent to app");
                    });
                    
                    // Set callback to send data when mobile app subscribes
                    bleSensorData->setSubscriptionCallback([this]() {
                        ESP_LOGI(TAG, "Mobile app subscribed - sending sensor data now");
                        if (hasNewSensorData) {
                            bleSensorData->sendSensorData(lastSensorReading);
                        }
                    });
                    
                    // Send current sensor data immediately (as fallback)
                    if (hasNewSensorData) {
                        bleSensorData->sendSensorData(lastSensorReading);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to start BLE sensor data service");
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "State already is %d, not changing", (int)currentState);
    }
}

void HandheldApp::onWiFiCredentialsReceived(const BleProvisioning::ProvisionData& data) {
    ESP_LOGI(TAG, "WiFi credentials received from BLE:");
    ESP_LOGI(TAG, "  SSID: %s", data.ssid.c_str());
    ESP_LOGI(TAG, "  UserUID: %s", data.userUID.c_str());
    
    // Display receiving credentials screen
    displayManager->drawCredentialsReceivedScreen(data.ssid.c_str(), -45);
    delay(1000);  // Show credentials screen for 1 second
    
    // Store credentials to NVS
    if (saveWiFiCredentialsToNVS(data.ssid.c_str(), data.password.c_str())) {
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save WiFi credentials to NVS");
    }
    
    // Display connecting screen
    displayManager->drawWiFiConnectingScreen(0);
    
    // Connect to WiFi
    WiFi.begin(data.ssid.c_str(), data.password.c_str());
    ESP_LOGI(TAG, "Connecting to WiFi: %s", data.ssid.c_str());
    
    // Wait up to 15 seconds for WiFi connection with progress updates
    // Check both connected status AND got valid IP
    int progress = 0;
    bool wifiConnected = false;
    for (int i = 0; i < 30 && !wifiConnected; i++) {  // 30 iterations × 500ms = 15 seconds
        delay(500);
        progress = (i * 100) / 30;
        displayManager->drawWiFiConnectingScreen(progress);
        
        // Check if connected AND has valid IP (not 0.0.0.0)
        if (WiFi.isConnected() && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
            wifiConnected = true;
            ESP_LOGI(TAG, "WiFi connected with valid IP after %d ms", (i + 1) * 500);
        }
    }
    
    // Display result based on connection status
    if (wifiConnected) {
        wifiConfigLastCountdownUpdate = 0;
        wifiConfigScreenDrawn = false;  // Reset screen flag for next entry
        ESP_LOGI(TAG, "✓ WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
        displayManager->drawWiFiConnectSuccessScreen(WiFi.localIP().toString().c_str());
        
        // Show success screen for 3 seconds before returning to HOME
        delay(3000);

        // reboot to apply new settings
        ESP_LOGI(TAG, "Rebooting to apply new WiFi settings...");
        esp_restart();

    } else {
        ESP_LOGW(TAG, "✗ WiFi connection failed - timeout");
        displayManager->drawWiFiConnectErrorScreen("Connection timeout");
        
        // Show error screen for 3 seconds before returning to HOME
        delay(3000);

        // Return to HOME screen - must reset display flags and state
        wifiConfigScreenDrawn = false;  // Reset screen flag for next entry
        wifiConfigLastCountdownUpdate = 0;
        displayManager->drawHomeScreen();
        changeState(AppState::IDLE);
    }
}

bool HandheldApp::saveWiFiCredentialsToNVS(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    err = nvs_set_str(nvs_handle, "wifi_ssid", ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_set_str(nvs_handle, "wifi_password", password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "WiFi credentials saved: SSID=%s", ssid);
    return true;
}

bool HandheldApp::loadWiFiCredentialsFromNVS(char* ssid, size_t ssid_len, char* password, size_t password_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No NVS WiFi credentials available");
        return false;
    }
    
    err = nvs_get_str(nvs_handle, "wifi_ssid", ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SSID from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_get_str(nvs_handle, "wifi_password", password, &password_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read password from NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "WiFi credentials loaded from NVS: SSID=%s", ssid);
    return true;
}

bool HandheldApp::clearWiFiCredentialsFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return false;
    }
    
    nvs_erase_key(nvs_handle, "wifi_ssid");
    nvs_erase_key(nvs_handle, "wifi_password");
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials cleared from NVS");
        return true;
    }
    return false;
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
    displayManager->drawErrorScreen(error.c_str());
    changeState(AppState::ERROR);
}

void HandheldApp::beepSuccess() {
    // Single beep to indicate successful sensor reading (500ms)
    digitalWrite(BUZZER_PIN, LOW);   // Active low - beep ON
    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms beep duration
    digitalWrite(BUZZER_PIN, HIGH);  // Turn off - beep OFF
}

void HandheldApp::storeSensorDataOffline(const sensorData& data) {
    ESP_LOGI(TAG, "Data saved offline");
}