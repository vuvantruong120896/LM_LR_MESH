#ifndef FIREBASE_QUEUE_H
#define FIREBASE_QUEUE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <vector>
#include "../../components/lora_mesh_manager/src/services/RoutingTableService.h"
#include "../common/mesh_utils.h"
#include "TimeSyncService.h"

// Firebase operation types
typedef enum {
    FIREBASE_OP_UPLOAD_SENSOR = 1,      // Upload sensor data from nodes
    FIREBASE_OP_UPLOAD_GATEWAY_SENSOR,  // Upload gateway's own sensor data  
    FIREBASE_OP_UPLOAD_STATUS,          // Upload gateway status
    FIREBASE_OP_UPLOAD_ROUTING_TABLE,   // Upload routing table
    FIREBASE_OP_LOG_EVENT,              // Log events
    FIREBASE_OP_UPDATE_GATEWAY_INFO,    // Update gateway information
    FIREBASE_OP_COMMAND_RESPONSE,       // Send command response
    // Phase 3: Batch operations
    FIREBASE_OP_BATCH_SENSORS,          // Batch upload multiple sensor data
    FIREBASE_OP_BATCH_EVENTS,           // Batch upload multiple events
    FIREBASE_OP_BATCH_MIXED,            // Mixed batch operation
    FIREBASE_OP_SHUTDOWN                // Shutdown signal
} FirebaseOperation_t;

// Priority levels for queue items
typedef enum {
    FIREBASE_PRIORITY_LOW = 1,      // Periodic uploads, analytics
    FIREBASE_PRIORITY_NORMAL = 2,   // Sensor data, routing table
    FIREBASE_PRIORITY_HIGH = 3,     // Error reports, critical status
    FIREBASE_PRIORITY_URGENT = 4    // System errors, reboot notifications
} FirebasePriority_t;

// Phase 2: Error types and recovery strategies
typedef enum {
    FIREBASE_ERROR_NONE = 0,
    FIREBASE_ERROR_NETWORK,             // WiFi/Internet connectivity issues
    FIREBASE_ERROR_AUTH,                // Authentication/authorization failures
    FIREBASE_ERROR_TIMEOUT,             // Request timeout
    FIREBASE_ERROR_MEMORY,              // Out of memory
    FIREBASE_ERROR_QUEUE_FULL,          // Queue overflow
    FIREBASE_ERROR_INVALID_DATA,        // Malformed data
    FIREBASE_ERROR_SERVER,              // Firebase server errors (5xx)
    FIREBASE_ERROR_CLIENT,              // Client errors (4xx)
    FIREBASE_ERROR_UNKNOWN              // Unknown/unexpected errors
} FirebaseError_t;

typedef enum {
    RECOVERY_NONE = 0,                  // No recovery needed
    RECOVERY_RETRY_IMMEDIATE,           // Retry immediately
    RECOVERY_RETRY_BACKOFF,             // Retry with exponential backoff
    RECOVERY_RETRY_AFTER_RECONNECT,     // Retry after network reconnection
    RECOVERY_DROP_ITEM,                 // Drop the failed item
    RECOVERY_SYSTEM_RESTART,            // Restart the system
    RECOVERY_QUEUE_FLUSH                // Flush queue and restart
} RecoveryStrategy_t;

// Phase 3: Smart Retry Strategies
typedef enum {
    RETRY_STRATEGY_EXPONENTIAL = 0,     // Standard exponential backoff
    RETRY_STRATEGY_LINEAR,              // Linear increase in delay
    RETRY_STRATEGY_FIXED,               // Fixed delay between retries
    RETRY_STRATEGY_FIBONACCI,           // Fibonacci sequence delays
    RETRY_STRATEGY_JITTERED,            // Exponential with random jitter
    RETRY_STRATEGY_ADAPTIVE             // Adaptive based on success rate
} RetryStrategy_t;

// Phase 3: Queue Management Modes
typedef enum {
    QUEUE_MODE_STANDARD = 0,            // Standard FIFO with priority
    QUEUE_MODE_BATCH_OPTIMIZED,         // Optimize for batch operations
    QUEUE_MODE_LATENCY_OPTIMIZED,       // Optimize for low latency
    QUEUE_MODE_THROUGHPUT_OPTIMIZED     // Optimize for high throughput
} QueueMode_t;

// Forward declarations
struct FirebaseQueueItem;
class FirebaseClient;

// Callback function type for operation completion
typedef void (*FirebaseOperationCallback_t)(const FirebaseQueueItem* item, bool success, const String& errorMessage);

// Queue item structure
typedef struct FirebaseQueueItem {
    FirebaseOperation_t operation;      // Type of operation
    FirebasePriority_t priority;        // Priority level
    uint32_t timestamp;                 // When queued (millis())
    uint32_t retryCount;               // Number of retries attempted
    uint32_t maxRetries;               // Maximum retries allowed
    
    // Payload data (union to save memory)
    union {
        struct {
            sensorData data;
            int8_t rssi;
            float snr;
        } sensorUpload;
        
        struct {
            uint16_t connectedNodes;
            uint32_t totalPacketsReceived;
            uint32_t totalPacketsSent;
            int8_t wifiRssi;
            uint32_t freeHeap;
            uint32_t uptimeSeconds;
        } statusUpload;
        
        struct {
            char eventType[64];
            char eventData[64]; 
            char eventDetails[128];
        } logEvent;
        
        struct {
            char gatewayMAC[18];    // "AA:BB:CC:DD:EE:FF"
            char localIP[16];       // "192.168.1.100"
            char firmwareVersion[16]; // "1.0.0"
        } gatewayInfo;
        
        // For routing table, we'll use a pointer to avoid copying large data
        struct {
            void* routingTableData;  // Will cast to std::vector<RouteNode>*
            size_t tableSize;
        } routingTable;
        
        // Generic data buffer for other operations
        struct {
            uint8_t* data;
            size_t dataSize;
        } generic;
        
        // Phase 3: Batch operations
        struct {
            void* batchData;        // Pointer to batch data array
            uint16_t itemCount;     // Number of items in batch
            uint16_t maxBatchSize;  // Maximum batch size
            uint32_t batchId;       // Unique batch identifier
        } batchOperation;
    } payload;
    
    // Optional callback for completion notification
    FirebaseOperationCallback_t callback;
    
} FirebaseQueueItem_t;

// Macro to initialize FirebaseQueueItem_t
#include <stdint.h>

// Helper: return current timestamp (seconds) using NTP if available,
// otherwise fall back to millis()/1000.
static inline uint32_t firebase_now_timestamp_seconds() {
    uint32_t t = TimeSyncService::getCurrentTimestamp();
    if (t == 0) {
        return (uint32_t)(millis() / 1000);
    }
    return t;
}

#define CREATE_FIREBASE_QUEUE_ITEM(op, prio) { \
    .operation = (op), \
    .priority = (prio), \
    .timestamp = firebase_now_timestamp_seconds(), \
    .retryCount = 0, \
    .maxRetries = 3, \
    .payload = {}, \
    .callback = nullptr \
}

// Firebase Queue Manager class
class FirebaseQueueManager {
public:
    // Configuration constants
    static constexpr uint16_t QUEUE_SIZE = 100;           // Max queue items
    static constexpr uint32_t WORKER_STACK_SIZE = 16384;  // 16KB stack for worker task
    static constexpr uint32_t MIN_OPERATION_INTERVAL_MS = 500;  // Min time between operations
    static constexpr uint32_t QUEUE_TIMEOUT_MS = 1000;    // Queue receive timeout
    static constexpr uint8_t WORKER_TASK_PRIORITY = 2;    // Task priority
    static constexpr uint8_t WORKER_TASK_CORE = 1;        // Run on CPU1
    
    // Singleton instance
    static FirebaseQueueManager& getInstance();
    
    // Lifecycle methods
    bool initialize(FirebaseClient* firebaseClient);
    void shutdown();
    bool isRunning() const { return m_workerTaskHandle != nullptr; }
    
    // Queue operations
    bool enqueue(const FirebaseQueueItem_t& item);
    bool enqueueSensorData(const sensorData& data, int8_t rssi, float snr, 
                          FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    bool enqueueGatewayStatus(uint16_t connectedNodes, uint32_t totalRx, uint32_t totalTx,
                             int8_t wifiRssi, uint32_t freeHeap, uint32_t uptime,
                             FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    bool enqueueRoutingTable(const std::vector<RouteNode>& routingTable,
                            FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    bool enqueueLogEvent(const String& eventType, const String& eventData, const String& eventDetails,
                        FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    
    // Queue status
    uint16_t getQueueSize() const;
    uint16_t getQueueUsage() const;
    bool isQueueFull() const;
    
    // Statistics
    struct QueueStats {
        uint32_t totalEnqueued;
        uint32_t totalProcessed;
        uint32_t totalSuccessful;
        uint32_t totalFailed;
        uint32_t totalRetries;
        uint32_t currentQueueSize;
        uint32_t maxQueueUsage;
        uint32_t droppedItems;       // Items dropped due to full queue
        uint32_t priorityDrops;      // High priority items dropped low priority
        
        // Memory monitoring (Phase 2)
        uint32_t heapBeforeOperation;
        uint32_t heapAfterOperation;
        uint32_t minFreeHeap;
        uint32_t memoryFragmentation;
        uint32_t largestFreeBlock;
        uint32_t totalHeapSize;
        uint32_t memoryAllocFailures;
        
        // Performance metrics (Phase 2)
        uint32_t avgProcessingTime;
        uint32_t maxProcessingTime;
        uint32_t minProcessingTime;
        uint32_t networkLatency;
        uint32_t queueWaitTime;
        
        // Error tracking (Phase 2)
        uint32_t networkErrors;
        uint32_t authErrors;
        uint32_t timeoutErrors;
        uint32_t memoryErrors;
        uint32_t serverErrors;
        uint32_t clientErrors;
        uint32_t unknownErrors;
        uint32_t totalRecoveryAttempts;
        uint32_t successfulRecoveries;
        uint32_t systemRestarts;
    };
    
    QueueStats getStatistics() const { return m_stats; }
    void resetStatistics();
    
    // Phase 2: Memory Management & Performance
    void enableMemoryMonitoring(bool enable = true);
    uint32_t getCurrentFreeHeap() const;
    uint32_t getLargestFreeBlock() const;
    float getMemoryFragmentationRatio() const;
    bool isMemoryHealthy() const;
    void optimizeMemoryUsage();
    void forceGarbageCollection();
    
    // Performance monitoring
    void enablePerformanceMonitoring(bool enable = true);
    uint32_t getAverageProcessingTime() const;
    uint32_t getQueueLatency() const;
    
    // Health checks
    bool isSystemHealthy() const;
    String getHealthReport() const;
    
    // Phase 2: Error Handling & Recovery
    void enableErrorRecovery(bool enable = true);
    FirebaseError_t classifyError(const String& errorMessage, int httpCode) const;
    RecoveryStrategy_t getRecoveryStrategy(FirebaseError_t errorType) const;
    bool executeRecovery(RecoveryStrategy_t strategy, FirebaseError_t errorType);
    void logError(FirebaseError_t errorType, const String& details);
    void incrementErrorCounter(FirebaseError_t errorType);
    String getErrorReport() const;
    
    // Network connectivity monitoring
    bool isNetworkConnected() const;
    void handleNetworkDisconnection();
    void handleNetworkReconnection();
    
    // System recovery
    void triggerSystemRecovery(const String& reason);
    void flushQueueForRecovery();
    bool attemptFirebaseReconnection();
    
    // Phase 3: Advanced Features
    // Batch Operations
    bool enqueueBatchSensors(const std::vector<sensorData>& sensorList, 
                            const std::vector<int8_t>& rssiList,
                            const std::vector<float>& snrList,
                            FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    bool enqueueBatchEvents(const std::vector<String>& eventTypes,
                           const std::vector<String>& eventData,
                           const std::vector<String>& eventDetails,
                           FirebasePriority_t priority = FIREBASE_PRIORITY_NORMAL);
    void setBatchSize(uint16_t maxBatchSize);
    void enableBatchOptimization(bool enable = true);
    
    // Smart Retry Management
    void setRetryStrategy(RetryStrategy_t strategy);
    void setAdaptiveRetryParams(float successThreshold, uint32_t adaptiveWindow);
    uint32_t calculateRetryDelay(uint32_t attempt, RetryStrategy_t strategy) const;
    
    // Queue Management Modes
    void setQueueMode(QueueMode_t mode);
    QueueMode_t getQueueMode() const;
    void optimizeQueueForMode();
    
    // Priority Management
    void enableDynamicPriority(bool enable = true);
    void adjustPriorityBasedOnLoad();
    void promoteOldItems(uint32_t ageThresholdMs);
    
    // Queue Persistence
    bool saveQueueState(const String& filename = "/spiffs/firebase_queue.dat");
    bool loadQueueState(const String& filename = "/spiffs/firebase_queue.dat");
    void enableQueuePersistence(bool enable = true);
    
    // Advanced Statistics
    String getAdvancedStats() const;
    void generatePerformanceReport();
    
private:
    // Private constructor for singleton
    FirebaseQueueManager();
    ~FirebaseQueueManager();
    
    // Disable copy and assignment
    FirebaseQueueManager(const FirebaseQueueManager&) = delete;
    FirebaseQueueManager& operator=(const FirebaseQueueManager&) = delete;
    
    // Member variables
    QueueHandle_t m_queue;
    TaskHandle_t m_workerTaskHandle;
    FirebaseClient* m_firebaseClient;
    SemaphoreHandle_t m_statsMutex;
    QueueStats m_stats;
    uint32_t m_lastOperationTime;
    bool m_initialized;
    
    // Phase 2: Memory & Performance monitoring
    bool m_memoryMonitoringEnabled;
    bool m_performanceMonitoringEnabled;
    uint32_t m_memoryCheckInterval;
    uint32_t m_lastMemoryCheck;
    uint32_t m_gcTriggerThreshold;  // Trigger GC when free heap below this
    uint32_t m_totalProcessingTime;
    uint32_t m_processingTimeCount;
    
    // Phase 2: Error handling & recovery
    bool m_errorRecoveryEnabled;
    uint32_t m_lastNetworkCheck;
    uint32_t m_networkCheckInterval;
    uint32_t m_consecutiveFailures;
    uint32_t m_maxConsecutiveFailures;
    uint32_t m_lastRecoveryAttempt;
    uint32_t m_recoveryAttemptInterval;
    bool m_isNetworkConnected;
    String m_lastErrorMessage;
    
    // Phase 3: Advanced Features
    // Batch operations
    bool m_batchOptimizationEnabled;
    uint16_t m_maxBatchSize;
    uint32_t m_batchTimeoutMs;
    uint32_t m_nextBatchId;
    
    // Smart retry management
    RetryStrategy_t m_retryStrategy;
    float m_adaptiveSuccessThreshold;
    uint32_t m_adaptiveWindow;
    uint32_t m_recentSuccesses;
    uint32_t m_recentAttempts;
    
    // Queue management
    QueueMode_t m_queueMode;
    bool m_dynamicPriorityEnabled;
    uint32_t m_priorityAgeThreshold;
    uint32_t m_lastPriorityAdjustment;
    
    // Queue persistence
    bool m_queuePersistenceEnabled;
    String m_persistenceFilename;
    uint32_t m_lastPersistenceWrite;
    uint32_t m_persistenceInterval;
    
    // Internal methods
    static void workerTask(void* parameter);
    void processQueueItem(const FirebaseQueueItem_t& item);
    bool processOperation(const FirebaseQueueItem_t& item);
    void updateStatistics(bool enqueued, bool processed, bool success);
    void cleanupQueueItem(const FirebaseQueueItem_t& item);
    bool shouldDropLowPriorityItem(FirebasePriority_t newItemPriority);
    void dropLowPriorityItem();
    
    // Operation processors
    bool processSensorUpload(const FirebaseQueueItem_t& item);
    bool processStatusUpload(const FirebaseQueueItem_t& item);
    bool processRoutingTableUpload(const FirebaseQueueItem_t& item);
    bool processLogEvent(const FirebaseQueueItem_t& item);
    bool processGatewayInfoUpdate(const FirebaseQueueItem_t& item);
    
    // Phase 3: Batch processing methods
    bool processBatchSensors(const FirebaseQueueItem_t& item);
    bool processBatchEvents(const FirebaseQueueItem_t& item);
    bool processBatchMixed(const FirebaseQueueItem_t& item);
    void updateAdaptiveRetryStats(bool success);
    
    // Phase 2: Memory management internals
    void checkMemoryHealth();
    void updateMemoryStats();
    void performGarbageCollection();
    bool isMemoryFragmented() const;
    void logMemoryUsage() const;
    
    // Performance tracking internals
    void recordProcessingTime(uint32_t startTime);
    void updatePerformanceStats(uint32_t processingTime);
};

// Convenience macros for common operations
#define FIREBASE_QUEUE() FirebaseQueueManager::getInstance()

#define FIREBASE_ENQUEUE_SENSOR(data, rssi, snr) \
    FIREBASE_QUEUE().enqueueSensorData(data, rssi, snr)

#define FIREBASE_ENQUEUE_STATUS(nodes, rx, tx, rssi, heap, uptime) \
    FIREBASE_QUEUE().enqueueGatewayStatus(nodes, rx, tx, rssi, heap, uptime)

#define FIREBASE_ENQUEUE_ROUTING_TABLE(table) \
    FIREBASE_QUEUE().enqueueRoutingTable(table)

#define FIREBASE_ENQUEUE_LOG(type, data, details) \
    FIREBASE_QUEUE().enqueueLogEvent(type, data, details)

#endif // FIREBASE_QUEUE_H