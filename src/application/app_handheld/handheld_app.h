#ifndef HANDHELD_APP_H
#define HANDHELD_APP_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "handheld_config.h"
#include "mesh_utils_handheld.h"
#include "application/common/device_type.h"
#include "components/rs485_soil_sensor/include/sensor_task.h"
#include "components/button_led/include/button_control.h"
#include "components/tft_display/include/tft_display_manager.h"
#include "components/tft_display/include/display_task.h"  // For DisplayScreen enum
//#include "components/lora_mesh_manager/include/wifi_connection_service.h"  // TODO: Create simplified WiFi service
#include "firebase_uploader.h"

/**
 * @brief Handheld Device Application
 * 
 * Features:
 * - RS485 soil sensor reading  
 * - TFT LCD 2.4" display with touch
 * - WiFi Firebase data upload
 * - Manual upload on button press (IO3)
 * - Battery monitoring
 * - Offline data storage
 * 
 * Hardware:
 * - ESP32-S3 with 16MB Flash
 * - ILI9341 240x320 TFT with touch
 * - RS485 soil sensor interface
 * - Push buttons for user interaction
 */
class HandheldApp {
public:
    /**
     * @brief Application state
     */
    enum class AppState {
        INITIALIZING,       ///< Starting up, initializing components
        IDLE,              ///< Normal operation, display on
        MEASURING,         ///< Reading sensor data
        UPLOADING,         ///< Uploading data to Firebase
        SLEEP,             ///< Display off, low power mode
        ERROR,             ///< Error state
        WIFI_CONFIG        ///< WiFi configuration mode
    };

    /**
     * @brief System status information
     */
    struct SystemStatus {
        AppState state;
        bool wifiConnected;
        bool sensorReady;
        bool sensorStatus;  // Added for compatibility
        bool displayActive;
        int batteryPercent;
        float batteryLevel; // Added for compatibility
        uint32_t freeHeap;
        uint32_t uptime;
        int rssi;
        String lastError;
    };

    /**
     * @brief Constructor
     */
    HandheldApp();

    /**
     * @brief Destructor  
     */
    ~HandheldApp();

    /**
     * @brief Initialize the handheld application
     * @return true if initialization successful
     */
    bool initialize();
    
    /**
     * @brief Setup method (alias for initialize for compatibility)
     * @return true if setup successful
     */
    bool setup() { return initialize(); }

    /**
     * @brief Main application loop
     * Call this repeatedly in Arduino loop()
     */
    void loop();

    /**
     * @brief Get current system status
     * @return SystemStatus structure
     */
    SystemStatus getSystemStatus();

    /**
     * @brief Force upload current sensor data
     * @return true if upload initiated successfully
     */
    bool uploadNow();

    /**
     * @brief Enter WiFi configuration mode
     */
    void enterWiFiConfigMode();

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset();

private:
    // Core components
    //WiFiConnectionService* wifiService;  // TODO: Implement simplified WiFi service
    TFTDisplayManager* displayManager;
    FirebaseUploader* firebaseUploader;
    
    // State management
    AppState currentState;
    uint32_t lastSensorRead;
    uint32_t lastAutoUpload;
    uint32_t lastBatteryCheck;
    uint32_t lastDisplayUpdate;
    uint32_t displayTimeout;
    
    // Data management
    sensorData lastSensorReading;
    std::vector<sensorData> offlineReadings;
    bool hasNewSensorData;
    
    // System status
    SystemStatus status;
    String deviceId;
    
    // Private methods
    bool initializeComponents();
    bool initializeNVS();
    bool initializeWiFi();
    bool initializeFirebase();
    bool initializeDisplay();
    bool initializeSensor();
    bool initializeButtons();
    bool initializeBuzzer();
    
    void handleButtonPress(button_event_t event);
    void handleSensorReading();
    void handleAutoUpload();
    void handleDisplayUpdate();
    void handleBatteryCheck();
    void handleWiFiReconnect();
    void readSensorManual();  // Manual sensor read on button press
    
    void updateSystemStatus();
    void changeState(AppState newState);
    void handleStateTransition();
    
    bool readSensorData();
    bool uploadSensorData(const sensorData& data);
    void storeSensorDataOffline(const sensorData& data);
    bool uploadOfflineData();
    
    void showSensorData();
    void showSystemInfo();
    void showWiFiConfig();
    void showError(const String& error);
    
    String generateDeviceId();
    void loadConfiguration();
    bool saveConfiguration();
    
    void logSystemInfo();
    void handleError(const String& error);
    
    // Static callback wrapper for button events
    static void buttonCallback(button_event_t event);
    static HandheldApp* instance;  // For static callback
    
    // Missing private members
    unsigned long stateChangeTime;
    unsigned long lastUploadTime;
    unsigned long lastWiFiCheck;
    
    // UI navigation
    DisplayScreen currentUIScreen;
    int menuSelection;
    void handleMenuNavigation(button_event_t event);
    void handleSoilDataScreen();
    void handleConfigScreen();
};

#endif // HANDHELD_APP_H