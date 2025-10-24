#ifndef FIREBASE_COORDINATOR_H
#define FIREBASE_COORDINATOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Firebase Operation Coordinator
 * 
 * Coordinates Firebase Worker and Command Poller tasks to prevent concurrent operations.
 * Uses binary semaphore to ensure ONLY ONE Firebase HTTP operation runs at a time,
 * preventing SSL engine corruption and CPU0 crashes from simultaneous connections.
 * 
 * BINARY SEMAPHORE PATTERN (Oct 24, 2025 - v2):
 * Fixed watchdog timeout caused by holding mutex during long Firebase HTTP requests.
 * New approach uses binary semaphore for mutual exclusion during actual HTTP operations.
 * 
 * Pattern:
 * 1. tryAcquireOperationLock() - non-blocking attempt to get exclusive Firebase access
 * 2. Perform Firebase HTTP request (may take 5-10s)
 * 3. releaseOperationLock() - release exclusive access
 * 
 * @author Kagri IoT Team
 * @date 2025-10-24
 */
class FirebaseOperationCoordinator {
private:
    static uint32_t s_lastOperationTime;    // Last Firebase operation timestamp (any task)
    static SemaphoreHandle_t s_timeMutex;   // Mutex for timestamp access only
    static SemaphoreHandle_t s_opSemaphore; // Binary semaphore for operation exclusion
    static constexpr uint32_t MIN_INTERVAL_MS = 5000;  // Minimum 5s between operations

public:
    /**
     * @brief Initialize coordinator (call once at startup)
     */
    static void initialize();

    /**
     * @brief Try to acquire exclusive Firebase operation lock (non-blocking or with timeout)
     * 
     * Acquires binary semaphore to ensure only ONE Firebase operation runs at a time.
     * Does NOT hold lock during HTTP request - just prevents concurrent starts.
     * 
     * Usage:
     *   if (tryAcquireOperationLock(5000)) {  // 5s timeout
     *       // Perform Firebase HTTP operation
     *       releaseOperationLock();
     *   }
     * 
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking, portMAX_DELAY = wait forever)
     * @return true if lock acquired, false on timeout
     */
    static bool tryAcquireOperationLock(uint32_t timeoutMs = 5000);

    /**
     * @brief Release exclusive Firebase operation lock
     * 
     * MUST be called after tryAcquireOperationLock() succeeds.
     * Call immediately after Firebase HTTP request completes (success or failure).
     */
    static void releaseOperationLock();

    /**
     * @brief Check minimum interval and wait if needed (call BEFORE tryAcquireOperationLock)
     * 
     * Checks if minimum 5s interval has elapsed since last operation.
     * If not, sleeps the calling task for remaining time.
     * Thread-safe with lightweight mutex.
     * 
     * @return Wait time that was applied in milliseconds (0 = no wait needed)
     */
    static uint32_t waitForMinInterval();

    /**
     * @brief Mark operation completed (call AFTER releaseOperationLock)
     * 
     * Updates timestamp for minimum interval tracking.
     * Thread-safe with lightweight mutex.
     */
    static void markOperationComplete();

    /**
     * @brief Get time since last operation (for debugging)
     * @return Elapsed time in milliseconds
     */
    static uint32_t getTimeSinceLastOperation();
};

#endif // FIREBASE_COORDINATOR_H
