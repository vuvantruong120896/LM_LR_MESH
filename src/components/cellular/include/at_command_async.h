#ifndef AT_COMMAND_ASYNC_H
#define AT_COMMAND_ASYNC_H

#include <Arduino.h>
#include "cellular_uart.h"
#include <functional>
#include <queue>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

/**
 * @brief Async AT Command Handler - Non-blocking send/receive
 * 
 * Architecture:
 * - sendCommandAsync(): Non-blocking send (just queue to UART)
 * - backgroundReceiver(): Task that reads UART independently
 * - Response matching via command ID
 * - Prevents timeout issues from blocking I/O
 * 
 * Benefits:
 * - Mesh task can run while cellular awaits response
 * - No thread blocking cellular task
 * - Proper queuing of commands and responses
 */
class ATCommandAsync {
public:
    /**
     * @brief AT command response
     */
    struct Response {
        bool success;
        String data;
        String errorMessage;
        int errorCode;
        uint32_t responseTimeMs;
        
        Response()
            : success(false)
            , errorCode(-1)
            , responseTimeMs(0)
        {}
    };

    /**
     * @brief Command status
     */
    enum CommandStatus {
        PENDING,      ///< Waiting for response
        COMPLETED,    ///< Response received
        TIMEOUT,      ///< Timeout occurred
        ERROR         ///< Other error
    };

    /**
     * @brief Command context for async operations
     */
    struct PendingCommand {
        uint32_t commandId;           ///< Unique command ID
        String command;               ///< Command sent
        uint32_t sentTimeMs;          ///< When command was sent
        uint32_t timeoutMs;           ///< Timeout duration
        Response response;            ///< Response data (filled by receiver)
        CommandStatus status;         ///< Current status
        bool expectOK;                ///< Expect "OK" response
    };

    using URCCallback = std::function<void(const String& urc)>;

    /**
     * @brief Constructor
     * @param uart Pointer to CellularUART instance
     */
    explicit ATCommandAsync(CellularUART* uart);

    /**
     * @brief Destructor
     */
    ~ATCommandAsync();

    /**
     * @brief Initialize async receiver task
     * Must be called once during setup
     */
    void initialize();

    /**
     * @brief Send AT command asynchronously (non-blocking)
     * 
     * @param command AT command (without "AT" prefix)
     * @param timeoutMs Timeout for response (default: 1000ms)
     * @param expectOK Expect "OK" response (default: true)
     * @return Command ID to use with waitForResponse()
     */
    uint32_t sendCommandAsync(const String& command,
                              uint32_t timeoutMs = 1000,
                              bool expectOK = true);

    /**
     * @brief Wait for response to specific command
     * 
     * Non-blocking if checkIntervalMs > 0, blocking if 0
     * 
     * @param commandId Command ID from sendCommandAsync()
     * @param outResponse Output response (filled on success)
     * @param maxWaitMs Max time to wait (0 = blocking until response)
     * @param checkIntervalMs How often to check (0 = blocking wait)
     * @return true if response received, false if timeout
     */
    bool waitForResponse(uint32_t commandId,
                        Response& outResponse,
                        uint32_t maxWaitMs = 5000,
                        uint32_t checkIntervalMs = 0);

    /**
     * @brief Check if command has completed
     * 
     * @param commandId Command ID from sendCommandAsync()
     * @param outResponse Output response (if completed)
     * @return true if completed (success or timeout)
     */
    bool isCommandComplete(uint32_t commandId, Response* outResponse = nullptr);

    /**
     * @brief Synchronous wrapper (backward compatibility)
     * 
     * Sends command and waits for response (blocking)
     * 
     * @param command AT command
     * @param timeoutMs Timeout in milliseconds
     * @param expectOK Expect "OK" response
     * @return Response object
     */
    Response sendCommand(const String& command,
                        uint32_t timeoutMs = 1000,
                        bool expectOK = true);

    /**
     * @brief Register URC callback
     * @param callback Callback function for URCs
     */
    void registerURCCallback(URCCallback callback);

    /**
     * @brief Extract value from response
     * 
     * Example: "+CPIN: READY" -> "READY"
     *          "+CSQ: 25,0" -> "25,0"
     * 
     * @param response Full response string
     * @param prefix Prefix to remove (e.g., "+CPIN:")
     * @return Extracted value
     */
    static String extractValue(const String& response, const String& prefix);

    /**
     * @brief Split comma-separated values
     * @param value Comma-separated string (e.g., "25,0")
     * @return Vector of values
     */
    static std::vector<String> splitValues(const String& value);

    /**
     * @brief Get UART instance
     * @return Pointer to UART
     */
    CellularUART* getUART() const { return m_uart; }

    /**
     * @brief Get pending command count
     * @return Number of commands waiting for response
     */
    size_t getPendingCommandCount() const;

    /**
     * @brief Enable/disable debug logging
     * @param enable true to enable
     */
    void setDebugLogging(bool enable) { m_debugLogging = enable; }

private:
    CellularUART* m_uart;
    URCCallback m_urcCallback;
    
    // Async operations
    std::map<uint32_t, PendingCommand> m_pendingCommands;
    SemaphoreHandle_t m_commandsMutex;
    SemaphoreHandle_t m_cmdCompleted;  ///< Binary semaphore for new commands
    TaskHandle_t m_receiverTaskHandle;
    
    uint32_t m_nextCommandId;
    bool m_debugLogging;
    
    static const char* TAG;
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 5000;
    
    /**
     * @brief Background receiver task (runs independently)
     */
    void receiverTask();

    /**
     * @brief Static wrapper for FreeRTOS task
     */
    static void receiverTaskWrapper(void* param);

    /**
     * @brief Process received lines
     */
    void processReceivedLine(const String& line);

    /**
     * @brief Match response to pending command
     */
    bool matchResponseToCommand(const String& line);

    /**
     * @brief Check if line is final response
     */
    bool isFinalResponse(const String& line) const;

    /**
     * @brief Check if line is URC
     */
    bool isURC(const String& line) const;

    /**
     * @brief Clean up timed-out commands
     */
    void cleanupTimedOutCommands();
};

#endif // AT_COMMAND_ASYNC_H
