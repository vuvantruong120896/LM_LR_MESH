#include "../include/sensor_task.h"
#include "../include/soil_sensor_service.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <chrono>

static const char* TAG = "SensorTask";

// Configuration
#define SENSOR_READ_INTERVAL_MS (9 * 60 * 1000)  // 9 minutes (not used - on-demand only)
#define SENSOR_TASK_STACK_SIZE  (4096)             // 4KB stack for UART + Modbus ops
#define SENSOR_TASK_PRIORITY    (tskIDLE_PRIORITY + 2) // Priority 2 - slightly above IDLE
#define SENSOR_QUEUE_SIZE       2                  // Store up to 2 readings max
#define CORE_0                  0                  // FreeRTOS core 0

// Task state management
static TaskHandle_t sensorTaskHandle = nullptr;
static QueueHandle_t sensorDataQueue = nullptr;
static QueueHandle_t sensorTriggerQueue = nullptr;  // Command queue for triggering reads
static volatile uint32_t lastReadTimestamp = 0;
static volatile uint32_t successfulReadCount = 0;
static volatile uint32_t failedReadCount = 0;

/**
 * @brief Main FreeRTOS task function for sensor reading (ON-DEMAND mode)
 * 
 * Runs on core 0 and waits for trigger commands from main application.
 * When triggered, reads sensor data and sends results to queue.
 * Task is on-demand only - no scheduled reads.
 * 
 * @note Triggered by main app via SensorTaskManager::triggerRead()
 * @note Task blocks until trigger received
 */
void SensorTaskManager::sensorTaskFunction(void* param) {
    ESP_LOGI(TAG, "📊 Sensor task started on core %d (ON-DEMAND mode)", xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1s to stabilize

    while (true) {
        // Wait for trigger command from main app (blocks indefinitely)
        uint8_t trigger = 0;
        if (xQueueReceive(sensorTriggerQueue, &trigger, portMAX_DELAY) != pdTRUE) {
            continue;  // Should never happen, but handle gracefully
        }
        
        // **READ PHASE**: Read sensor on demand
        ESP_LOGI(TAG, "🔄 Starting on-demand sensor read (triggered by button)...");
        uint32_t readStartTime = xTaskGetTickCount();
        
        sensorData reading = SoilSensorService::readData();
        
        uint32_t readEndTime = xTaskGetTickCount();
        uint32_t readDurationMs = (readEndTime - readStartTime) * portTICK_PERIOD_MS;
        
        
        // **QUEUE PHASE**: Send reading to main application
        // Uses non-blocking xQueueSend to avoid blocking the task
        // If queue full (shouldn't happen with proper main loop), drop oldest
        if (xQueueSendToBack(sensorDataQueue, &reading, 0) == pdTRUE) {
            successfulReadCount++;
            
            // Log success with reading details
            if (reading.deviceType == DeviceType::SOIL_SENSOR) {
                ESP_LOGI(TAG, "✅ Read #%u: 🌱 Soil Sensor (took %u ms)",
                         successfulReadCount, readDurationMs);
                ESP_LOGI(TAG, "   Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f",
                         reading.data.soil.soilMoisture,
                         reading.data.soil.soilTemperature,
                         reading.data.soil.pH);
                ESP_LOGI(TAG, "   EC: %.1f µS/cm, N: %.1f, P: %.1f, K: %.1f",
                         reading.data.soil.conductivity,
                         reading.data.soil.nitrogen,
                         reading.data.soil.phosphorus,
                         reading.data.soil.potassium);
                ESP_LOGI(TAG, "   Status: %s", reading.error ? "ERROR" : "OK");
            } else if (reading.deviceType == DeviceType::ENV_SENSOR) {
                ESP_LOGI(TAG, "✅ Read #%u: 🌡️ Environment Sensor (took %u ms)",
                         successfulReadCount, readDurationMs);
            } else {
                ESP_LOGI(TAG, "✅ Read #%u: Sensor data (took %u ms)", 
                         successfulReadCount, readDurationMs);
            }
        } else {
            failedReadCount++;
            ESP_LOGW(TAG, "⚠️ Queue send failed - buffer full? Dropped reading #%u",
                     successfulReadCount + failedReadCount);
            // Queue overflow is rare but possible if main loop is hung
            // In production, this would trigger an alert
        }
        
        // **DIAGNOSTICS**: Log queue status
        size_t itemsInQueue = uxQueueMessagesWaiting(sensorDataQueue);
        if (itemsInQueue > 0) {
            ESP_LOGD(TAG, "📦 Queue depth: %u items waiting", itemsInQueue);
        }

        // **WAIT PHASE**: Loop back to wait for next trigger
        // Task blocks at xQueueReceive() until main app calls triggerRead()
        ESP_LOGD(TAG, "⏳ Waiting for next trigger command...");
    }
    
    // This line never executes under normal conditions
    vTaskDelete(nullptr);
}

bool SensorTaskManager::initialize() {
    // Check if already running
    if (sensorTaskHandle != nullptr) {
        ESP_LOGW(TAG, "Sensor task already initialized");
        return false;
    }
    
    // Create queue for sensor data (stores sensorData structs)
    sensorDataQueue = xQueueCreate(SENSOR_QUEUE_SIZE, sizeof(sensorData));
    if (sensorDataQueue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create sensor data queue");
        return false;
    }
    
    // Create queue for trigger commands (on-demand mode)
    sensorTriggerQueue = xQueueCreate(1, sizeof(uint8_t));
    if (sensorTriggerQueue == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create trigger queue");
        vQueueDelete(sensorDataQueue);
        sensorDataQueue = nullptr;
        return false;
    }
    
    // Create FreeRTOS task pinned to core 1
    BaseType_t result = xTaskCreatePinnedToCore(
        sensorTaskFunction,           // Task function
        "SensorTask",                 // Task name (for debugging)
        SENSOR_TASK_STACK_SIZE,       // Stack size in words (4KB)
        nullptr,                      // Parameter (not used)
        SENSOR_TASK_PRIORITY,         // Priority (low - won't starve others)
        &sensorTaskHandle,            // Output task handle
        1                             // Core 1 (separate from WiFi/BLE)
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create FreeRTOS task - xTaskCreatePinnedToCore returned %d", result);
        // Clean up queues since task creation failed
        vQueueDelete(sensorDataQueue);
        vQueueDelete(sensorTriggerQueue);
        sensorDataQueue = nullptr;
        sensorTriggerQueue = nullptr;
        return false;
    }
    
    // Reset statistics
    successfulReadCount = 0;
    failedReadCount = 0;
    lastReadTimestamp = millis();
    
    ESP_LOGI(TAG, "✅ Initialized (ON-DEMAND mode - reads triggered by button press)");

    return true;
}

bool SensorTaskManager::shutdown() {
    if (sensorTaskHandle == nullptr) {
        ESP_LOGW(TAG, "Sensor task not running");
        return false;
    }
    
    ESP_LOGI(TAG, "Shutting down sensor task...");
    
    // Delete the task (it will stop on next scheduling cycle)
    vTaskDelete(sensorTaskHandle);
    sensorTaskHandle = nullptr;
    
    // Delete the queues (free queue memory)
    if (sensorDataQueue != nullptr) {
        vQueueDelete(sensorDataQueue);
        sensorDataQueue = nullptr;
    }
    
    if (sensorTriggerQueue != nullptr) {
        vQueueDelete(sensorTriggerQueue);
        sensorTriggerQueue = nullptr;
    }
    
    ESP_LOGI(TAG, "✅ Sensor task shut down");
    ESP_LOGI(TAG, "   Stats: %u successful, %u failed reads",
             successfulReadCount, failedReadCount);
    
    return true;
}

bool SensorTaskManager::triggerRead() {
    if (sensorTriggerQueue == nullptr) {
        ESP_LOGW(TAG, "Cannot trigger read - queue not initialized");
        return false;
    }
    
    // Send trigger command to task (non-blocking)
    uint8_t trigger = 1;
    if (xQueueSend(sensorTriggerQueue, &trigger, 0) == pdTRUE) {
        ESP_LOGI(TAG, "📍 Trigger command sent to sensor task");
        return true;
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to send trigger (queue may be full)");
        return false;
    }
}

bool SensorTaskManager::getData(sensorData& outData, uint32_t timeoutMs) {
    if (sensorDataQueue == nullptr) {
        ESP_LOGW(TAG, "Cannot get data - queue not initialized");
        return false;
    }
    
    // Convert timeout to FreeRTOS ticks
    TickType_t timeout = (timeoutMs == 0) ? 
        0 :  // pdMS_TO_TICKS(0) returns 0 for non-blocking
        pdMS_TO_TICKS(timeoutMs);
    
    // Try to receive one sensor reading from queue
    // If queue empty and timeout=0, returns immediately
    // If timeout>0, blocks up to that duration waiting for data
    if (xQueueReceive(sensorDataQueue, &outData, timeout) == pdTRUE) {
        ESP_LOGD(TAG, "📥 Retrieved sensor data from queue - age: %u ms",
                 millis() - outData.timestamp);
        return true;
    }
    
    // No data available (timeout or queue empty)
    return false;
}

bool SensorTaskManager::isRunning() {
    return sensorTaskHandle != nullptr;
}

uint32_t SensorTaskManager::getLastReadTime() {
    return lastReadTimestamp;
}

size_t SensorTaskManager::getQueueDepth() {
    if (sensorDataQueue == nullptr) {
        return 0;
    }
    return uxQueueMessagesWaiting(sensorDataQueue);
}

uint32_t SensorTaskManager::getFailedReadCount() {
    return failedReadCount;
}

uint32_t SensorTaskManager::getSuccessfulReadCount() {
    return successfulReadCount;
}

bool SensorTaskManager::putData(const sensorData& data) {
    if (sensorDataQueue == nullptr) {
        ESP_LOGW(TAG, "Cannot put data - queue not initialized");
        return false;
    }
    
    if (xQueueSendToBack(sensorDataQueue, &data, 0) == pdTRUE) {
        ESP_LOGI(TAG, "✅ Initial sensor data queued successfully");
        return true;
    } else {
        ESP_LOGW(TAG, "⚠️ Queue full - cannot add initial sensor data");
        return false;
    }
}
