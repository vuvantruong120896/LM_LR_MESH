#ifndef FIREBASE_COORDINATOR_H
#define FIREBASE_COORDINATOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Firebase Operation Coordinator
 * 
 * Coordinates Firebase Worker and Command Poller tasks to prevent concurrent operations.
 * Ensures minimum 5 second interval between Firebase HTTP requests from different tasks
 * to avoid heap pressure from simultaneous connections and JSON buffer allocations.
 * 
 * @author Kagri IoT Team
 * @date 2025-10-24
 */
class FirebaseOperationCoordinator {
private:
    static uint32_t s_lastOperationTime;  // Last Firebase operation timestamp (any task)
    static SemaphoreHandle_t s_mutex;     // Mutex for thread-safe access
    static constexpr uint32_t MIN_INTERVAL_MS = 5000;  // Minimum 5s between operations

public:
    /**
     * @brief Initialize coordinator (call once at startup)
     */
    static void initialize();

    /**
     * @brief Check if operation can proceed
     * @return Wait time in milliseconds (0 = can proceed immediately)
     */
    static uint32_t getWaitTime();

    /**
     * @brief Mark operation started (call before Firebase HTTP request)
     */
    static void markOperationStart();

    /**
     * @brief Get time since last operation (for debugging)
     * @return Elapsed time in milliseconds
     */
    static uint32_t getTimeSinceLastOperation();
};

#endif // FIREBASE_COORDINATOR_H
