/**
 * @file firebase_command_queue.h
 * @brief Firebase Command Queue for Remote Gateway Control
 * 
 * Polls Firebase for pending commands and executes them:
 * - REBOOT: Restart gateway
 * - UPDATE_CONFIG: Update configuration
 * - GET_STATUS: Force status upload
 * - CLEAR_ROUTES: Clear routing table
 * - SYNC_TIME: Synchronize time with server
 * - OTA_UPDATE: Firmware update (future)
 */

#ifndef FIREBASE_COMMAND_QUEUE_H
#define FIREBASE_COMMAND_QUEUE_H

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>
#include "cellular_firebase_client.h"

/**
 * @brief Firebase Command Queue Manager
 * 
 * Monitors /commands/{gatewayId}/pending for new commands,
 * executes them, and updates status to /completed or /failed
 */
class FirebaseCommandQueue {
public:
    /**
     * @brief Command types
     */
    enum class CommandType {
        REBOOT,             ///< Restart gateway
        UPDATE_CONFIG,      ///< Update configuration
        GET_STATUS,         ///< Force status upload
        CLEAR_ROUTES,       ///< Clear routing table
        SYNC_TIME,          ///< Sync time from server
        OTA_UPDATE,         ///< Firmware OTA update
        UNKNOWN             ///< Unknown command
    };

    /**
     * @brief Command execution status
     */
    enum class ExecutionStatus {
        PENDING,            ///< Waiting to execute
        PROCESSING,         ///< Currently executing
        COMPLETED,          ///< Successfully completed
        FAILED              ///< Execution failed
    };

    /**
     * @brief Command structure
     */
    struct Command {
        String id;                      ///< Unique command ID (Firebase key)
        CommandType type;               ///< Command type
        std::map<String, String> params; ///< Command parameters
        uint32_t timestamp;             ///< Command creation timestamp
        ExecutionStatus status;         ///< Current execution status
        String result;                  ///< Execution result message
        
        Command() : type(CommandType::UNKNOWN), timestamp(0), 
                   status(ExecutionStatus::PENDING) {}
    };

    /**
     * @brief Command execution callback
     * @param command Command to execute
     * @return true if execution successful
     */
    using CommandCallback = std::function<bool(const Command& command)>;

    /**
     * @brief Constructor
     * @param firebaseClient Pointer to initialized CellularFirebaseClient
     * @param pollIntervalMs Polling interval in milliseconds (default: 30s)
     */
    explicit FirebaseCommandQueue(CellularFirebaseClient* firebaseClient,
                                  uint32_t pollIntervalMs = 30000);

    /**
     * @brief Initialize command queue
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Update command queue (call in loop)
     * 
     * Polls for new commands at pollIntervalMs interval
     */
    void update();

    /**
     * @brief Register command handler
     * @param type Command type
     * @param callback Callback function to execute command
     */
    void onCommand(CommandType type, CommandCallback callback);

    /**
     * @brief Set polling interval
     * @param intervalMs Polling interval in milliseconds
     */
    void setPollInterval(uint32_t intervalMs);

    /**
     * @brief Get polling interval
     * @return Current polling interval (ms)
     */
    uint32_t getPollInterval() const { return m_pollInterval; }

    /**
     * @brief Enable/disable command queue
     * @param enabled True to enable polling
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if command queue is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Get pending commands count
     * @return Number of pending commands
     */
    uint16_t getPendingCount() const { return m_pendingCommands.size(); }

    /**
     * @brief Get statistics
     */
    struct Stats {
        uint32_t totalPolls;
        uint32_t totalCommands;
        uint32_t completedCommands;
        uint32_t failedCommands;
        uint32_t lastPollTime;
        uint32_t lastCommandTime;
    };

    /**
     * @brief Get command queue statistics
     * @return Statistics structure
     */
    Stats getStats() const { return m_stats; }

    /**
     * @brief Reset statistics
     */
    void resetStats();

    /**
     * @brief Convert CommandType to string
     */
    static const char* commandTypeToString(CommandType type);

    /**
     * @brief Convert string to CommandType
     */
    static CommandType stringToCommandType(const String& str);

private:
    // Firebase client
    CellularFirebaseClient* m_firebaseClient;

    // Configuration
    uint32_t m_pollInterval;        ///< Polling interval (ms)
    bool m_enabled;                 ///< Command queue enabled
    String m_gatewayId;             ///< Gateway ID

    // Command handlers
    std::map<CommandType, CommandCallback> m_handlers;

    // Pending commands
    std::vector<Command> m_pendingCommands;

    // Timing
    uint32_t m_lastPollTime;

    // Statistics
    Stats m_stats;

    // Helper Methods

    /**
     * @brief Poll Firebase for new commands
     * @return Number of new commands found
     */
    uint16_t pollCommands();

    /**
     * @brief Parse command from JSON
     * @param commandId Command ID (Firebase key)
     * @param json JSON string
     * @param command Output command structure
     * @return true if parsing successful
     */
    bool parseCommand(const String& commandId, const String& json, Command& command);

    /**
     * @brief Execute command
     * @param command Command to execute
     * @return true if execution successful
     */
    bool executeCommand(Command& command);

    /**
     * @brief Update command status in Firebase
     * @param command Command with updated status
     * @return true if update successful
     */
    bool updateCommandStatus(const Command& command);

    /**
     * @brief Move command to processing
     * @param commandId Command ID
     * @return true if successful
     */
    bool moveToProcessing(const String& commandId);

    /**
     * @brief Move command to completed
     * @param command Command to move
     * @return true if successful
     */
    bool moveToCompleted(const Command& command);

    /**
     * @brief Move command to failed
     * @param command Command to move
     * @return true if successful
     */
    bool moveToFailed(const Command& command);

    /**
     * @brief Delete command from pending
     * @param commandId Command ID
     * @return true if successful
     */
    bool deletePending(const String& commandId);

    /**
     * @brief Parse command parameters from JSON
     * @param json JSON object string
     * @param params Output parameters map
     * @return true if parsing successful
     */
    bool parseParams(const String& json, std::map<String, String>& params);

    static const char* TAG;
};

#endif // FIREBASE_COMMAND_QUEUE_H
