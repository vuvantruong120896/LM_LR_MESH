#ifndef CELLULAR_FIREBASE_COMMAND_POLLER_H
#define CELLULAR_FIREBASE_COMMAND_POLLER_H

#include <Arduino.h>
#include "../components/cellular/include/cellular_firebase_https_client.h"

/**
 * @brief Cellular Firebase Command Poller Service
 * 
 * Polls Firebase for pending commands via HTTPS GET requests.
 * Similar to FirebaseCommandPoller but uses CellularFirebaseHTTPSClient.
 * 
 * Command Types:
 * - start_provisioning: Start fast discovery mode for adding nodes
 * - stop_provisioning: Stop fast discovery mode
 * - assign_netkey: Update network key
 * 
 * @author Kagri IoT Team
 * @date 2025-11-12
 */
class CellularFirebaseCommandPoller {
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
     * @param httpsClient CellularFirebaseHTTPSClient pointer
     * @param userUID User ID from Firebase Auth
     * @param gatewayMAC Gateway MAC address (used as ID)
     */
    CellularFirebaseCommandPoller(CellularFirebaseHTTPSClient* httpsClient, 
                                   const String& userUID, 
                                   const String& gatewayMAC);
    
    /**
     * @brief Destructor
     */
    ~CellularFirebaseCommandPoller();
    
    /**
     * @brief Initialize command poller and start dedicated polling task
     * @param stackSize Task stack size (default: 8192 bytes)
     * @param priority Task priority (default: 1)
     * @param coreId Core to pin task (default: 1 = CPU1)
     */
    void begin(uint32_t stackSize = 8192, uint8_t priority = 1, int coreId = 1);
    
    /**
     * @brief Poll for pending commands (now runs in dedicated task)
     * @note Legacy method - kept for compatibility but now runs in separate task
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
     * @return true if successful
     */
    bool moveToProcessing(const Command& cmd);
    
    /**
     * @brief Mark command as completed
     * @param cmd Command to complete
     * @param result Result string ("success")
     * @param message Human-readable message
     * @param details Optional JSON details
     * @return true if successful
     */
    bool moveToCompleted(const Command& cmd, const String& result, 
                        const String& message, const String& details = "");
    
    /**
     * @brief Mark command as failed
     * @param cmd Command that failed
     * @param errorCode Error code (e.g., "GATEWAY_OFFLINE")
     * @param message Human-readable error message
     * @return true if successful
     */
    bool moveToFailed(const Command& cmd, const String& errorCode, const String& message);
    
    /**
     * @brief Update provisioning progress
     * @param cmdId Command ID
     * @param nodesDiscovered Number of nodes discovered
     * @param timeRemaining Time remaining in milliseconds
     * @return true if successful
     */
    bool updateProgress(const String& cmdId, uint16_t nodesDiscovered, uint32_t timeRemaining);
    
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
    CellularFirebaseHTTPSClient* m_httpsClient; // HTTPS client
    String m_userUID;               // User ID
    String m_gatewayMAC;            // Gateway MAC address
    String m_basePath;              // Base path: users/{uid}/commands/{mac}
    
    Command m_currentCommand;       // Current command being processed
    bool m_hasCommand;              // Flag: has command to process
    uint32_t m_lastPoll;            // Last poll timestamp
    uint32_t m_pollInterval;        // Poll interval in milliseconds
    bool m_enabled;                 // Polling enabled flag
    
    // Task management
    TaskHandle_t m_pollingTaskHandle; // FreeRTOS task handle
    bool m_taskRunning;             // Task running flag
    
    /**
     * @brief Static task entry point for FreeRTOS
     * @param parameter Pointer to CellularFirebaseCommandPoller instance
     */
    static void pollingTask(void* parameter);
    
    /**
     * @brief Fetch pending commands from Firebase via HTTPS GET
     * @return true if command found
     */
    bool fetchPendingCommands();
    
    /**
     * @brief Parse command JSON from Firebase response
     * @param key Command ID
     * @param valueJson Command JSON string
     * @return true if parsed successfully
     */
    bool parseCommand(const String& key, const String& valueJson);
    
    /**
     * @brief Extract first command from pending commands JSON
     * @param jsonResponse Full JSON response from Firebase
     * @param outKey Output: Command ID
     * @param outValue Output: Command JSON
     * @return true if command extracted
     */
    bool extractFirstCommand(const String& jsonResponse, String& outKey, String& outValue);
};

#endif // CELLULAR_FIREBASE_COMMAND_POLLER_H
