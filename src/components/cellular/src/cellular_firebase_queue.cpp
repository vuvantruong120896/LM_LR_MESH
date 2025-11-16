#include "cellular_firebase_queue.h"

#include <esp_log.h>
#include <cstring>
#include <new>

static const char* TAG = "CELL_FB_QUEUE";
static CellularFirebaseQueue* s_instance = nullptr;

CellularFirebaseQueue& CellularFirebaseQueue::getInstance() {
    if (!s_instance) {
        s_instance = new CellularFirebaseQueue();
    }
    return *s_instance;
}

bool CellularFirebaseQueue::initialize(CellularFirebaseHTTPSClient* client) {
    if (m_workerTask) {
        ESP_LOGW(TAG, "Cellular Firebase queue already running");
        return true;
    }

    if (!client) {
        ESP_LOGE(TAG, "Cannot initialize queue without HTTPS client");
        return false;
    }

    m_client = client;

    m_queue = xQueueCreate(kQueueLength, sizeof(QueueItem));
    if (!m_queue) {
        ESP_LOGE(TAG, "Failed to create cellular Firebase queue");
        return false;
    }

    m_statsMutex = xSemaphoreCreateMutex();
    if (!m_statsMutex) {
        ESP_LOGE(TAG, "Failed to create queue stats mutex");
        vQueueDelete(m_queue);
        m_queue = nullptr;
        return false;
    }

    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        workerTask,
        "CellFirebaseWorker",
        kWorkerStackBytes,
        this,
        kWorkerPriority,
        &m_workerTask,
        kWorkerCore);

    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cellular Firebase worker task");
        vSemaphoreDelete(m_statsMutex);
        m_statsMutex = nullptr;
        vQueueDelete(m_queue);
        m_queue = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "✅ Cellular Firebase queue ready (len=%u, core=%u)", kQueueLength, kWorkerCore);
    return true;
}

void CellularFirebaseQueue::shutdown() {
    if (!m_workerTask) {
        return;
    }

    if (m_queue && m_workerTask) {
        QueueItem stopItem;
        stopItem.operation = Operation::Shutdown;
        xQueueSend(m_queue, &stopItem, portMAX_DELAY);
    }

    while (m_workerTask) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (m_queue) {
        QueueItem drained;
        while (xQueueReceive(m_queue, &drained, 0) == pdTRUE) {
            cleanupItem(drained);
        }
        vQueueDelete(m_queue);
        m_queue = nullptr;
    }

    if (m_statsMutex) {
        vSemaphoreDelete(m_statsMutex);
        m_statsMutex = nullptr;
    }

    ESP_LOGI(TAG, "Cellular Firebase queue stopped");
}

bool CellularFirebaseQueue::enqueueSensorData(const sensorData& data, int16_t rssi, float snr, uint8_t priority) {
    QueueItem item;
    item.operation = Operation::SensorData;
    item.priority = priority;
    item.sensor.data = data;
    item.sensor.rssi = rssi;
    item.sensor.snr = snr;
    item.queuedAtMs = millis();
    return enqueueItem(item);
}

bool CellularFirebaseQueue::enqueueGatewayStatus(uint16_t connectedNodes,
                                                  uint32_t totalPacketsReceived,
                                                  uint32_t totalPacketsSent,
                                                  int16_t signalRssi,
                                                  uint32_t freeHeap,
                                                  uint32_t uptimeSeconds,
                                                  uint8_t priority) {
    QueueItem item;
    item.operation = Operation::GatewayStatus;
    item.priority = priority;
    item.status.connectedNodes = connectedNodes;
    item.status.totalPacketsReceived = totalPacketsReceived;
    item.status.totalPacketsSent = totalPacketsSent;
    item.status.signalRssi = signalRssi;
    item.status.freeHeap = freeHeap;
    item.status.uptimeSeconds = uptimeSeconds;
    item.queuedAtMs = millis();
    return enqueueItem(item);
}

bool CellularFirebaseQueue::enqueueRoutingTable(const std::vector<RouteNode>& routingTable, uint8_t priority) {
    if (routingTable.empty()) {
        // Still enqueue an empty vector to clear remote copy
        ESP_LOGI(TAG, "Routing table empty - queueing clear operation");
    }

    auto* tableCopy = new (std::nothrow) std::vector<RouteNode>(routingTable);
    if (!tableCopy) {
        ESP_LOGE(TAG, "Failed to allocate routing table copy (%u entries)", routingTable.size());
        return false;
    }

    QueueItem item;
    item.operation = Operation::RoutingTable;
    item.priority = priority;
    item.routing.table = tableCopy;
    item.queuedAtMs = millis();
    return enqueueItem(item);
}

bool CellularFirebaseQueue::enqueueLogEvent(const String& eventType,
                                            const String& nodeId,
                                            const String& details,
                                            uint8_t priority) {
    QueueItem item;
    item.operation = Operation::LogEvent;
    item.priority = priority;
    item.queuedAtMs = millis();

    strncpy(item.log.eventType, eventType.c_str(), sizeof(item.log.eventType) - 1);
    strncpy(item.log.nodeId, nodeId.c_str(), sizeof(item.log.nodeId) - 1);
    strncpy(item.log.details, details.c_str(), sizeof(item.log.details) - 1);
    return enqueueItem(item);
}

bool CellularFirebaseQueue::enqueueSignalQualityCheck(uint8_t priority) {
    QueueItem item;
    item.operation = Operation::SignalQualityCheck;
    item.priority = priority;
    item.maxRetries = 1;  // Only retry once for periodic checks
    item.queuedAtMs = millis();
    return enqueueItem(item);
}

bool CellularFirebaseQueue::enqueueRegistrationStateCheck(uint8_t priority) {
    QueueItem item;
    item.operation = Operation::RegistrationStateCheck;
    item.priority = priority;
    item.maxRetries = 1;  // Only retry once for periodic checks
    item.queuedAtMs = millis();
    return enqueueItem(item);
}

CellularFirebaseQueue::QueueStats CellularFirebaseQueue::getStats() const {
    QueueStats copy;
    if (m_statsMutex && xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_statsMutex);
    } else {
        copy = m_stats;
    }
    return copy;
}

bool CellularFirebaseQueue::enqueueItem(QueueItem item) {
    if (!m_queue) {
        ESP_LOGE(TAG, "Queue not initialized - dropping operation %u", static_cast<uint8_t>(item.operation));
        return false;
    }

    if (xQueueSend(m_queue, &item, 0) != pdTRUE) {
        if (m_statsMutex && xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            m_stats.dropped++;
            xSemaphoreGive(m_statsMutex);
        }
        ESP_LOGW(TAG, "Queue full - dropping operation %u", static_cast<uint8_t>(item.operation));
        cleanupItem(item);
        return false;
    }

    if (m_statsMutex && xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m_stats.enqueued++;
        xSemaphoreGive(m_statsMutex);
    }

    return true;
}

void CellularFirebaseQueue::cleanupItem(QueueItem& item) {
    if (item.operation == Operation::RoutingTable && item.routing.table) {
        delete item.routing.table;
        item.routing.table = nullptr;
    }
}

void CellularFirebaseQueue::workerTask(void* parameter) {
    auto* self = static_cast<CellularFirebaseQueue*>(parameter);
    ESP_LOGI(TAG, "Cellular Firebase worker running on core %d", xPortGetCoreID());

    QueueItem item;
    static uint32_t consecutiveFailures = 0;
    static uint32_t lastFailureTime = 0;
    
    while (true) {
        if (xQueueReceive(self->m_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (item.operation == Operation::Shutdown) {
            self->cleanupItem(item);
            break;
        }

        self->processItem(item, consecutiveFailures, lastFailureTime);

        vTaskDelay(pdMS_TO_TICKS(5000)); // 5 second delay between items
    }

    ESP_LOGI(TAG, "Cellular Firebase worker exiting");
    self->m_workerTask = nullptr;
    vTaskDelete(nullptr);
}

void CellularFirebaseQueue::processItem(QueueItem& item, uint32_t& consecutiveFailures, uint32_t& lastFailureTime) {
    if (!m_client) {
        ESP_LOGE(TAG, "Firebase HTTPS client not set - dropping operation");
        
        // 🔔 Notify failure immediately
        if (m_uploadCallback && item.operation == Operation::SensorData) {
            m_uploadCallback(item.operation, false, &item.sensor.data, 0);
        }
        
        cleanupItem(item);
        return;
    }

    bool success = false;

    switch (item.operation) {
        case Operation::SensorData: {
            ESP_LOGI(TAG, "📡 Uploading sensor data (Node ID: %u, Counter: %u)",
                     item.sensor.data.nodeId,
                     item.sensor.data.counter);
            auto result = m_client->uploadSensorData(item.sensor.data,
                                                     static_cast<int8_t>(item.sensor.rssi),
                                                     item.sensor.snr);
            success = result.success;
            if (!success) {
                ESP_LOGW(TAG, "✗✗✗✗  Sensor upload failed: %s", result.message.c_str());
            }
            break;
        }
        case Operation::GatewayStatus: {
            ESP_LOGI(TAG, "📊 Uploading gateway status");
            auto result = m_client->uploadGatewayStatus(
                item.status.connectedNodes,
                item.status.totalPacketsReceived,
                item.status.totalPacketsSent,
                item.status.signalRssi,
                item.status.freeHeap,
                item.status.uptimeSeconds);
            success = result.success;
            if (!success) {
                ESP_LOGW(TAG, "Gateway status upload failed: %s", result.message.c_str());
            }
            break;
        }
        case Operation::RoutingTable: {
            ESP_LOGI(TAG, "📚 Uploading routing table");
            if (!item.routing.table) {
                ESP_LOGE(TAG, "Routing table payload missing");
                success = false;
                break;
            }
            auto result = m_client->uploadRoutingTable(*item.routing.table);
            success = result.success;
            if (!success) {
                ESP_LOGW(TAG, "Routing table upload failed: %s", result.message.c_str());
            }
            break;
        }
        case Operation::LogEvent: {
            ESP_LOGI(TAG, "📝 Uploading log event: %s", item.log.eventType);
            success = m_client->logEvent(String(item.log.eventType),
                                         String(item.log.nodeId),
                                         String(item.log.details));
            if (!success) {
                ESP_LOGW(TAG, "Log event upload failed for %s", item.log.eventType);
            }
            break;
        }
        case Operation::SignalQualityCheck: {
            ESP_LOGD(TAG, "📶 Periodic signal quality check (AT+CSQ)");
            if (m_connectionService) {
                m_connectionService->updateSignalQuality();
                success = true;
            } else {
                ESP_LOGW(TAG, "CellularConnectionService not set for signal quality check");
                success = false;
            }
            break;
        }
        case Operation::RegistrationStateCheck: {
            ESP_LOGD(TAG, "📋 Periodic registration state check (AT+CREG?)");
            if (m_connectionService) {
                m_connectionService->updateRegistrationState();
                success = true;
            } else {
                ESP_LOGW(TAG, "CellularConnectionService not set for registration state check");
                success = false;
            }
            break;
        }
        case Operation::Shutdown:
            // handled earlier
            success = true;
            break;
    }

    // Update cellular health tracking
    if (success) {
        consecutiveFailures = 0;
        ESP_LOGD(TAG, "✓ Operation completed successfully, cellular health good");
    } else {
        consecutiveFailures++;
        lastFailureTime = millis();
        ESP_LOGW(TAG, "✗ Operation failed, cellular health degraded (failures: %d)", consecutiveFailures);
    }

    if (m_statsMutex && xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m_stats.processed++;
        if (success) {
            m_stats.succeeded++;
        } else {
            m_stats.failed++;
        }
        xSemaphoreGive(m_statsMutex);
    }

    if (!handleProcessResult(item, success)) {
        // Operation completed (success or max retries exceeded)
        // 🔔 NOTIFY WITH ACTUAL RESULT (not just enqueue)
        if (m_uploadCallback && item.operation == Operation::SensorData) {
            m_uploadCallback(
                item.operation,
                success,                    // ← TRUE = uploaded OK, FALSE = all retries failed
                &item.sensor.data,          // ← Pointer to sensor data for reference
                item.attempts + 1           // ← Total attempts made (0-based, so +1)
            );
        }
        
        cleanupItem(item);
    }
}

bool CellularFirebaseQueue::handleProcessResult(QueueItem& item, bool success) {
    if (success) {
        return false; // cleanup now
    }

    if (item.attempts >= item.maxRetries) {
        ESP_LOGE(TAG, "Operation %u failed after %u attempts", static_cast<uint8_t>(item.operation), item.attempts + 1);
        return false;
    }

    QueueItem retryItem = item;
    retryItem.attempts++;

    if (m_statsMutex && xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        m_stats.retries++;
        xSemaphoreGive(m_statsMutex);
    }

    ESP_LOGW(TAG, "Retrying operation %u (attempt %u/%u)",
             static_cast<uint8_t>(retryItem.operation),
             retryItem.attempts + 1,
             retryItem.maxRetries + 1);

    vTaskDelay(pdMS_TO_TICKS(250 * retryItem.attempts));

    if (!enqueueItem(retryItem)) {
        if (retryItem.operation == Operation::RoutingTable) {
            item.routing.table = nullptr;
        }
        return false;
    }

    return true; // ownership transferred to queue copy, skip cleanup of current instance
}
