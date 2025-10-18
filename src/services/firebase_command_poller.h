#ifndef FIREBASE_COMMAND_POLLER_H
#define FIREBASE_COMMAND_POLLER_H

#include <Arduino.h>
#include <FirebaseESP32.h>

/**
 * @brief Firebase Command Poller Service
 * 
 * Polls Firebase for pending commands from Mobile App and executes them.
 * Supports command queue with status tracking (pending → processing → completed/failed).
 * 
 * Command Types:
 * - start_provisioning: Start fast discovery mode for adding nodes
 * - stop_provisioning: Stop fast discovery mode
 * - set_netkey: Update network key (future)
 * 
 * @author Kagri IoT Team
 * @date 2025-10-18
 */
class FirebaseCommandPoller {
public:
    /**
     * @brief Command structure
     */
    struct Command {
        String id;              // Unique command ID (e.g., "cmd_1729234567001")
        String type;            // Command type (e.g., "start_provisioning")
        String params;          // JSON string with parameters
        uint32_t timestamp;     // Command creation timestamp
        uint8_t priority;       // Priority: 0=high, 1=normal, 2=low
        
        Command() : timestamp(0), priority(1) {}
    };

    /**
     * @brief Constructor
     * @param fbdo FirebaseData object pointer (from FirebaseClient)
     * @param userUID User ID from Firebase Auth
     * @param gatewayMAC Gateway MAC address (used as ID)
     */
    FirebaseCommandPoller(FirebaseData* fbdo, const String& userUID, const String& gatewayMAC);
    
    /**
     * @brief Initialize command poller
     */
    void begin();
    
    /**
     * @brief Poll for pending commands (call in main loop every 5-10 seconds)
     */
    void poll();
    
    /**
     * @brief Check if there's a command to process
     * @return true if command available
     */
    bool hasCommand();
    
    /**
     * @brief Get next command to process
     * @return Command object (clears internal flag)
     */
    Command getNextCommand();
    
    /**
     * @brief Move command from pending to processing
     * @param cmd Command to move
     */
    void moveToProcessing(const Command& cmd);
    
    /**
     * @brief Mark command as completed
     * @param cmd Command to complete
     * @param result Result string ("success")
     * @param message Human-readable message
     * @param details Optional JSON details
     */
    void moveToCompleted(const Command& cmd, const String& result, 
                        const String& message, const String& details = "");
    
    /**
     * @brief Mark command as failed
     * @param cmd Command that failed
     * @param errorCode Error code (e.g., "GATEWAY_OFFLINE")
     * @param message Human-readable error message
     */
    void moveToFailed(const Command& cmd, const String& errorCode, const String& message);
    
    /**
     * @brief Update command result in real-time (for Mobile App to listen)
     * @param cmdId Command ID
     * @param status Current status
     * @param message Status message
     */
    void updateCommandResult(const String& cmdId, const String& status, const String& message);
    
    /**
     * @brief Update provisioning progress
     * @param cmdId Command ID
     * @param nodesDiscovered Number of nodes discovered
     * @param timeRemaining Time remaining in milliseconds
     */
    void updateProgress(const String& cmdId, uint16_t nodesDiscovered, uint32_t timeRemaining);
    
    /**
     * @brief Get current command being processed
     * @return Current command (if any)
     */
    const Command& getCurrentCommand() const { return m_currentCommand; }
    
    /**
     * @brief Check if command polling is enabled
     * @return true if enabled
     */
    bool isEnabled() const { return m_enabled; }
    
    /**
     * @brief Enable/disable command polling
     * @param enabled true to enable
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }

private:
    FirebaseData* m_fbdo;           // Firebase data object
    String m_userUID;               // User ID
    String m_gatewayMAC;            // Gateway MAC address
    String m_basePath;              // Base path: users/{uid}/commands/{mac}
    
    Command m_currentCommand;       // Current command being processed
    bool m_hasCommand;              // Flag: has command to process
    uint32_t m_lastPoll;            // Last poll timestamp
    uint32_t m_pollInterval;        // Poll interval in milliseconds
    bool m_enabled;                 // Polling enabled flag
    
    /**
     * @brief Fetch pending commands from Firebase
     * @return true if command found
     */
    bool fetchPendingCommands();
    
    /**
     * @brief Parse command JSON from Firebase
     * @param key Command ID
     * @param value Command JSON string
     * @return true if parsed successfully
     */
    bool parseCommand(const String& key, const String& value);
    
    /**
     * @brief Cleanup old completed/failed commands (keep last 10)
     */
    void cleanupOldCommands();
};

#endif // FIREBASE_COMMAND_POLLER_H
