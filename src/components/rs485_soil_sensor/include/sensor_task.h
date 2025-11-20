#ifndef RS485_SENSOR_TASK_H
#define RS485_SENSOR_TASK_H

#include "sensor_data.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstdint>

/**
 * @file sensor_task.h
 * @brief FreeRTOS task management for RS485 soil sensor reading
 * 
 * Provides non-blocking, periodic sensor reading on dedicated core 0.
 * Uses queue-based communication to pass sensor data to main application.
 * 
 * **Architecture:**
 * - Runs on FreeRTOS core 0 (dedicated CPU core)
 * - Reads sensor every 10 minutes (configurable via SENSOR_READ_INTERVAL_MS)
 * - Puts data in queue for main application to consume
 * - Main loop never blocked by sensor read operations (~125ms reads)
 * - Data timestamp tracks when measurement was taken
 * 
 * **Usage Example:**
 * ```cpp
 * // In app setup (once):
 * if (SensorTaskManager::initialize()) {
 *     ESP_LOGI(TAG, "Sensor task started");
 * }
 * 
 * // In app loop (every iteration):
 * sensorData latest;
 * if (SensorTaskManager::getData(latest, 0)) {  // 0 = non-blocking
 *     // Process new sensor data
 *     ESP_LOGI(TAG, "Moisture: %.1f%%", latest.data.soil.soilMoisture);
 * }
 * // else: No new data available yet
 * 
 * // On shutdown (if needed):
 * SensorTaskManager::shutdown();
 * ```
 * 
 * **Characteristics:**
 * - Thread-safe queue communication
 * - Core 0 pinning prevents interference with WiFi/BLE (core 1)
 * - Low priority task - doesn't starve other operations
 * - Data only sent when new reading available (not every cycle)
 * - Each queue read consumes one sensor sample
 * 
 * **Failure Modes:**
 * - Task creation fails: initialize() returns false
 * - Sensor offline: Queue will contain error-filled sensorData struct
 * - Task crashed: Check runtime logs via ESP_ERROR_CHECK
 */

class SensorTaskManager {
public:
    /**
     * @brief Initialize and start sensor reading task on core 0
     * 
     * Creates FreeRTOS task pinned to core 0 that:
     * - Initializes SoilSensorService if not already done
     * - Reads sensor every SENSOR_READ_INTERVAL_MS (10 minutes)
     * - Sends readings to queue for main app to consume
     * - Logs timing information for monitoring
     * 
     * @return true if task created successfully, false if already running or creation failed
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** Yes, for ~10-50ms during RTOS internals
     * **May Fail:** If FreeRTOS cannot allocate stack memory or core 0 unavailable
     * 
     * @note Should be called once during application setup
     * @note SoilSensorService::initialize() called internally if needed
     * @note Task starts immediately after creation
     * 
     * Example:
     * ```cpp
     * if (!SensorTaskManager::initialize()) {
     *     ESP_LOGE(TAG, "Failed to create sensor task - proceed without sensor");
     * }
     * ```
     */
    static bool initialize();

    /**
     * @brief Stop sensor reading task and cleanup resources
     * 
     * Stops the FreeRTOS task and frees queue memory.
     * Safe to call even if task not running.
     * 
     * @return true if task stopped, false if no task was running
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** Yes, waits for task to stop (typically <100ms)
     * 
     * @note SoilSensorService remains initialized for manual use if needed
     * @note Queue handle becomes invalid after shutdown
     * 
     * Example:
     * ```cpp
     * SensorTaskManager::shutdown();
     * ```
     */
    static bool shutdown();

    /**
     * @brief Get latest sensor reading from task queue (non-blocking)
     * 
     * Attempts to retrieve the most recent sensor reading.
     * If no new data available, returns false immediately.
     * If data available, copies one reading and returns true.
     * 
     * @param[out] outData  sensorData structure to populate (only valid if returns true)
     * @param[in] timeoutMs Timeout in milliseconds:
     *                       - 0 = non-blocking (returns immediately)
     *                       - >0 = blocks up to N ms waiting for data
     *                       - portMAX_DELAY = wait forever for data
     * 
     * @return true if data was retrieved, false if queue empty/timeout
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** Yes, if timeoutMs > 0
     * **Common Use Cases:**
     * - timeoutMs=0: Quick non-blocking poll in main loop
     * - timeoutMs=1000: Wait up to 1 second if data expected
     * - portMAX_DELAY: Block until sensor update arrives
     * 
     * @note Each call consumes ONE item from queue
     * @note If no new data, outData unchanged (caller should initialize)
     * @note Each reading timestamp shows exact time measurement was taken
     * 
     * Example (non-blocking):
     * ```cpp
     * sensorData reading;
     * if (SensorTaskManager::getData(reading, 0)) {
     *     ESP_LOGI(TAG, "New data: %.1f%% moisture", reading.data.soil.soilMoisture);
     * }
     * ```
     * 
     * Example (with timeout):
     * ```cpp
     * sensorData reading;
     * if (SensorTaskManager::getData(reading, 5000)) {  // Wait up to 5 seconds
     *     process_sensor_data(reading);
     * } else {
     *     ESP_LOGW(TAG, "No sensor data for 5 seconds - device may be offline");
     * }
     * ```
     */
    static bool getData(sensorData& outData, uint32_t timeoutMs);

    /**
     * @brief Check if sensor task is currently running
     * 
     * @return true if task created and running, false if stopped or failed
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** No - returns immediately
     * 
     * Example:
     * ```cpp
     * if (SensorTaskManager::isRunning()) {
     *     ESP_LOGI(TAG, "Sensor task active");
     * } else {
     *     ESP_LOGI(TAG, "Sensor task not started");
     * }
     * ```
     */
    static bool isRunning();

    /**
     * @brief Get timestamp of last successful sensor reading
     * 
     * Returns the application uptime (milliseconds since boot) when
     * the most recent sensor reading was completed.
     * 
     * Useful for:
     * - Monitoring task responsiveness (should advance every 10 min)
     * - Detecting task crashes (timestamp stops advancing)
     * - Alerting if sensor data becomes stale
     * 
     * @return Timestamp in milliseconds since boot, or 0 if no reads yet
     * 
     * **Thread Safety:** Safe to call from any core (atomic read)
     * **May Block:** No - returns immediately
     * 
     * Example:
     * ```cpp
     * uint32_t lastRead = SensorTaskManager::getLastReadTime();
     * uint32_t now = millis();
     * uint32_t ageMs = now - lastRead;
     * 
     * if (ageMs > 65 * 60 * 1000) {  // More than 65 minutes
     *     ESP_LOGW(TAG, "Sensor data stale - task may be stuck");
     * }
     * ```
     */
    static uint32_t getLastReadTime();

    /**
     * @brief Get current queue depth (diagnostic)
     * 
     * Returns number of sensor readings waiting in queue.
     * Normally 0-1 (task generates one reading every 10 minutes).
     * 
     * High queue depth could indicate main loop not consuming data.
     * 
     * @return Number of items in queue, 0 if queue not initialized
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** No - returns immediately
     * 
     * Example:
     * ```cpp
     * if (SensorTaskManager::getQueueDepth() > 2) {
     *     ESP_LOGW(TAG, "Sensor queue backing up - main loop slow?");
     * }
     * ```
     */
    static size_t getQueueDepth();

    /**
     * @brief Get number of failed sensor reads since task started (diagnostic)
     * 
     * Counts consecutive or total sensor failures for monitoring.
     * Useful for long-term reliability tracking.
     * 
     * @return Number of failed read attempts, reset on shutdown
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** No - returns immediately
     */
    static uint32_t getFailedReadCount();

    /**
     * @brief Get number of successful sensor reads since task started (diagnostic)
     * 
     * Counts successful sensor measurements for monitoring.
     * Combined with failure count, indicates health status.
     * 
     * @return Number of successful reads, reset on shutdown
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** No - returns immediately
     * 
     * Example:
     * ```cpp
     * uint32_t success = SensorTaskManager::getSuccessfulReadCount();
     * uint32_t failed = SensorTaskManager::getFailedReadCount();
     * uint32_t total = success + failed;
     * ESP_LOGI(TAG, "Sensor reliability: %u/%u (%.1f%% success)", 
     *          success, total, (100.0 * success) / total);
     * ```
     */
    static uint32_t getSuccessfulReadCount();

    /**
     * @brief Put sensor data into queue (for startup initialization)
     * 
     * Allows manual insertion of sensor data into the queue.
     * Typically used at startup to pre-populate queue with initial reading.
     * 
     * @param[in] data sensorData to add to queue
     * @return true if successfully queued, false if queue full
     * 
     * **Thread Safety:** Safe to call from any core
     * **May Block:** No - non-blocking queue operation
     * 
     * Example (at startup):
     * ```cpp
     * sensorData initialReading = SoilSensorService::readData();
     * if (SensorTaskManager::putData(initialReading)) {
     *     ESP_LOGI(TAG, "Initial sensor reading queued");
     * }
     * ```
     */
    static bool putData(const sensorData& data);

private:
    // Prevent instantiation - static class only
    SensorTaskManager() = delete;
    ~SensorTaskManager() = delete;

    // Internal FreeRTOS task function (called by OS)
    static void sensorTaskFunction(void* param);
};

#endif // RS485_SENSOR_TASK_H
