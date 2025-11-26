#ifndef FIREBASE_UPLOADER_H
#define FIREBASE_UPLOADER_H

#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include "components/rs485_soil_sensor/include/sensor_data.h"

/**
 * @brief Firebase Data Uploader for Handheld Device
 * 
 * Simplified Firebase client specifically for handheld device:
 * - Upload sensor data directly to Firebase
 * - Device status reporting
 * - Offline data queue management
 * - Authentication handling
 * 
 * @note This is a simplified version of the gateway's FirebaseClient,
 *       optimized for single-device operation without mesh networking
 */
class FirebaseUploader {
public:
    /**
     * @brief Upload status
     */
    enum class UploadStatus {
        SUCCESS,            ///< Upload completed successfully
        FAILED,            ///< Upload failed
        NO_WIFI,           ///< No WiFi connection
        AUTHENTICATION_FAILED, ///< Firebase auth failed
        TIMEOUT,           ///< Upload timeout
        QUEUE_FULL         ///< Offline queue is full
    };

    /**
     * @brief Upload statistics
     */
    struct UploadStats {
        uint32_t totalAttempts;     ///< Total upload attempts
        uint32_t successful;        ///< Successful uploads
        uint32_t failed;            ///< Failed uploads
        uint32_t lastUploadTime;    ///< Last successful upload (epoch)
        float avgUploadTime;        ///< Average upload time (ms)
    };

    /**
     * @brief Default constructor
     */
    FirebaseUploader();

    /**
     * @brief Destructor
     */
    ~FirebaseUploader();

    /**
     * @brief Initialize Firebase uploader
     * @return true if initialization successful
     */
    bool initialize();

    /**
     * @brief Upload sensor data to Firebase
     * @param data Sensor data to upload
     * @return Upload status
     */
    UploadStatus uploadSensorData(const sensorData& data);

    /**
     * @brief Upload device status
     * @param status Status message string
     * @return Upload status
     */
    UploadStatus uploadDeviceStatus(const String& status);

    /**
     * @brief Queue sensor data for offline upload
     * @param data Sensor data to queue
     * @return true if data queued successfully
     */
    bool queueOfflineData(const sensorData& data);

    /**
     * @brief Upload all queued offline data
     * @return Number of records successfully uploaded
     */
    int uploadOfflineQueue();

    /**
     * @brief Get upload statistics
     * @return UploadStats structure
     */
    UploadStats getStats() const;

    /**
     * @brief Check if Firebase is connected
     * @return true if connected
     */
    bool isConnected() const;

    /**
     * @brief Get last error message
     * @return Error message string
     */
    String getLastError() const;

    /**
     * @brief Clear offline data queue
     */
    void clearOfflineQueue();

    /**
     * @brief Get offline queue size
     * @return Number of queued records
     */
    size_t getOfflineQueueSize() const;

private:
    // Firebase objects
    FirebaseData fbdo;
    String uid;
    
    // State management
    bool isInitialized;
    String lastError;
    
    // Firebase configuration constants
    static constexpr const char* FIREBASE_API_KEY = "your-api-key-here";
    static constexpr const char* FIREBASE_DATABASE_URL = "https://your-project.firebaseio.com/";
    static constexpr const char* FIREBASE_USER_EMAIL = "test@example.com";
    static constexpr const char* FIREBASE_USER_PASSWORD = "testpassword";
    
    // Static callback
    static void tokenStatusCallback(TokenInfo info);
};

#endif // FIREBASE_UPLOADER_H