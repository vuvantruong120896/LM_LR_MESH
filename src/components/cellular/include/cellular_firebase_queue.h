#ifndef CELLULAR_FIREBASE_QUEUE_H
#define CELLULAR_FIREBASE_QUEUE_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "mesh_utils.h"
#include "cellular_firebase_https_client.h"

/**
 * @brief Lightweight queue manager that decouples cellular producers from the blocking
 *        Firebase HTTPS client. A dedicated worker task on CPU1 drains the queue and
 *        performs the uploads sequentially so that the cellular modem is accessed from
 *        a single context.
 */
class CellularFirebaseQueue {
public:
    enum class Operation : uint8_t {
        SensorData,
        GatewayStatus,
        RoutingTable,
        LogEvent,
        SignalQualityCheck,      // Periodic AT+CSQ query
        RegistrationStateCheck,  // Periodic AT+CREG? query
        FetchCommands,           // Poll Firebase for pending commands
        Shutdown
    };

    // Callback for upload completion (success or final failure after retries)
    using UploadCallback = std::function<void(
        Operation operation,           // Type of operation
        bool success,                  // TRUE = upload succeeded, FALSE = all retries failed
        const sensorData* data,        // Pointer to sensor data (if SensorData op)
        uint8_t attempts               // Number of attempts made
    )>;

    struct QueueStats {
        uint32_t enqueued = 0;
        uint32_t processed = 0;
        uint32_t succeeded = 0;
        uint32_t failed = 0;
        uint32_t dropped = 0;
        uint32_t retries = 0;
        uint32_t duplicateRejections = 0;  // Counter rejected by Firebase rules (duplicate)
    };

    static CellularFirebaseQueue& getInstance();

    bool initialize(CellularFirebaseHTTPSClient* client);
    void shutdown();
    void setConnectionService(class CellularConnectionService* service) {
        m_connectionService = service;
    }
    bool isRunning() const { return m_workerTask != nullptr; }
    
    // Register callback to receive upload results
    void setUploadCallback(UploadCallback callback) {
        m_uploadCallback = callback;
    }

    bool enqueueSensorData(const sensorData& data, int16_t rssi, float snr, uint8_t priority = 2);
    bool enqueueGatewayStatus(uint16_t connectedNodes,
                              uint32_t totalPacketsReceived,
                              uint32_t totalPacketsSent,
                              int16_t signalRssi,
                              uint32_t freeHeap,
                              uint32_t uptimeSeconds,
                              uint8_t priority = 2);
    bool enqueueRoutingTable(const std::vector<RouteNode>& routingTable, uint8_t priority = 2);
    bool enqueueLogEvent(const String& eventType,
                         const String& nodeId,
                         const String& details,
                         uint8_t priority = 2);
    bool enqueueSignalQualityCheck(uint8_t priority = 1);
    bool enqueueRegistrationStateCheck(uint8_t priority = 1);
    bool enqueueFetchCommands(uint8_t priority = 1);
    
    QueueStats getStats() const;

private:
    struct QueueItem {
        Operation operation = Operation::SensorData;
        uint8_t priority = 2;
        uint8_t attempts = 0;
        uint8_t maxRetries = 3;
        uint32_t queuedAtMs = 0;

        struct {
            sensorData data;
            int16_t rssi = 0;
            float snr = 0.0f;
        } sensor;

        struct {
            uint16_t connectedNodes = 0;
            uint32_t totalPacketsReceived = 0;
            uint32_t totalPacketsSent = 0;
            int16_t signalRssi = 0;
            uint32_t freeHeap = 0;
            uint32_t uptimeSeconds = 0;
        } status;

        struct {
            std::vector<RouteNode>* table = nullptr;
        } routing;

        struct {
            char eventType[48] = {0};
            char nodeId[48] = {0};
            char details[128] = {0};
        } log;

        struct {
            // Placeholder for periodic checks (no additional data needed)
        } periodicCheck;
    };

    CellularFirebaseQueue() = default;

    bool enqueueItem(QueueItem item);
    void cleanupItem(QueueItem& item);
    static void workerTask(void* parameter);
    void processItem(QueueItem& item, uint32_t& consecutiveFailures, uint32_t& lastFailureTime);
    bool handleProcessResult(QueueItem& item, bool success);

    CellularFirebaseHTTPSClient* m_client = nullptr;
    class CellularConnectionService* m_connectionService = nullptr;
    QueueHandle_t m_queue = nullptr;
    TaskHandle_t m_workerTask = nullptr;
    SemaphoreHandle_t m_statsMutex = nullptr;
    QueueStats m_stats;
    UploadCallback m_uploadCallback = nullptr;  // Callback for upload results

    static constexpr uint16_t kQueueLength = 64;
    static constexpr uint16_t kWorkerStackBytes = 12288; // 12 KB
    static constexpr UBaseType_t kWorkerPriority = 3;
    static constexpr uint8_t kWorkerCore = 1;
};

#endif // CELLULAR_FIREBASE_QUEUE_H
