/**
 * @file firebase_command_queue.cpp
 * @brief Firebase Command Queue Implementation
 */

#include "firebase_command_queue.h"
#include <esp_log.h>

const char* FirebaseCommandQueue::TAG = "CMD_QUEUE";

FirebaseCommandQueue::FirebaseCommandQueue(CellularFirebaseClient* firebaseClient,
                                         uint32_t pollIntervalMs)
    : m_firebaseClient(firebaseClient)
    , m_pollInterval(pollIntervalMs)
    , m_enabled(true)
    , m_lastPollTime(0)
{
    memset(&m_stats, 0, sizeof(Stats));
}

bool FirebaseCommandQueue::initialize() {
    ESP_LOGI(TAG, "Initializing Firebase command queue...");
    
    if (!m_firebaseClient) {
        ESP_LOGE(TAG, "Firebase client is null");
        return false;
    }

    // Get gateway ID from Firebase client (assuming it has getter)
    // For now, hardcode or get from config
    m_gatewayId = "GW_TEST_001"; // TODO: Get from config
    
    ESP_LOGI(TAG, "✅ Command queue initialized");
    ESP_LOGI(TAG, "   Gateway: %s", m_gatewayId.c_str());
    ESP_LOGI(TAG, "   Poll interval: %ums", m_pollInterval);
    
    return true;
}

void FirebaseCommandQueue::update() {
    if (!m_enabled) {
        return;
    }

    uint32_t now = millis();
    
    // Check if it's time to poll
    if (now - m_lastPollTime >= m_pollInterval) {
        m_lastPollTime = now;
        
        ESP_LOGD(TAG, "Polling for commands...");
        uint16_t newCommands = pollCommands();
        
        if (newCommands > 0) {
            ESP_LOGI(TAG, "Found %d new command(s)", newCommands);
            m_stats.lastCommandTime = now;
        }
        
        m_stats.totalPolls++;
    }

    // Execute pending commands
    for (auto it = m_pendingCommands.begin(); it != m_pendingCommands.end(); ) {
        Command& cmd = *it;
        
        if (cmd.status == ExecutionStatus::PENDING) {
            ESP_LOGI(TAG, "Executing command: %s (ID: %s)", 
                    commandTypeToString(cmd.type), cmd.id.c_str());
            
            // Move to processing
            if (!moveToProcessing(cmd.id)) {
                ESP_LOGW(TAG, "Failed to move command to processing");
            }
            
            cmd.status = ExecutionStatus::PROCESSING;
            
            // Execute command
            bool success = executeCommand(cmd);
            
            if (success) {
                ESP_LOGI(TAG, "✅ Command completed: %s", cmd.id.c_str());
                cmd.status = ExecutionStatus::COMPLETED;
                moveToCompleted(cmd);
                m_stats.completedCommands++;
            } else {
                ESP_LOGE(TAG, "❌ Command failed: %s - %s", 
                        cmd.id.c_str(), cmd.result.c_str());
                cmd.status = ExecutionStatus::FAILED;
                moveToFailed(cmd);
                m_stats.failedCommands++;
            }
            
            // Delete from pending
            deletePending(cmd.id);
            
            // Remove from local list
            it = m_pendingCommands.erase(it);
        } else {
            ++it;
        }
    }
}

void FirebaseCommandQueue::onCommand(CommandType type, CommandCallback callback) {
    m_handlers[type] = callback;
    ESP_LOGI(TAG, "Registered handler for: %s", commandTypeToString(type));
}

void FirebaseCommandQueue::setPollInterval(uint32_t intervalMs) {
    m_pollInterval = intervalMs;
    ESP_LOGI(TAG, "Poll interval updated: %ums", intervalMs);
}

void FirebaseCommandQueue::setEnabled(bool enabled) {
    m_enabled = enabled;
    ESP_LOGI(TAG, "Command queue %s", enabled ? "enabled" : "disabled");
}

void FirebaseCommandQueue::resetStats() {
    memset(&m_stats, 0, sizeof(Stats));
    ESP_LOGI(TAG, "Statistics reset");
}

// ===== Command Type Conversion =====

const char* FirebaseCommandQueue::commandTypeToString(CommandType type) {
    switch (type) {
        case CommandType::REBOOT:        return "REBOOT";
        case CommandType::UPDATE_CONFIG: return "UPDATE_CONFIG";
        case CommandType::GET_STATUS:    return "GET_STATUS";
        case CommandType::CLEAR_ROUTES:  return "CLEAR_ROUTES";
        case CommandType::SYNC_TIME:     return "SYNC_TIME";
        case CommandType::OTA_UPDATE:    return "OTA_UPDATE";
        default:                         return "UNKNOWN";
    }
}

FirebaseCommandQueue::CommandType FirebaseCommandQueue::stringToCommandType(const String& str) {
    if (str == "REBOOT")        return CommandType::REBOOT;
    if (str == "UPDATE_CONFIG") return CommandType::UPDATE_CONFIG;
    if (str == "GET_STATUS")    return CommandType::GET_STATUS;
    if (str == "CLEAR_ROUTES")  return CommandType::CLEAR_ROUTES;
    if (str == "SYNC_TIME")     return CommandType::SYNC_TIME;
    if (str == "OTA_UPDATE")    return CommandType::OTA_UPDATE;
    return CommandType::UNKNOWN;
}

// ===== Private Helper Methods =====

uint16_t FirebaseCommandQueue::pollCommands() {
    String path = "/commands/" + m_gatewayId + "/pending";
    String result;
    
    auto response = m_firebaseClient->get(path, result);
    
    if (!response.success()) {
        ESP_LOGW(TAG, "Failed to poll commands: %s", response.message.c_str());
        return 0;
    }

    // Parse response (should be JSON object with command IDs as keys)
    // Example: {"cmd_123": {"type":"REBOOT","timestamp":123456}, "cmd_456": {...}}
    
    if (result == "null" || result.isEmpty()) {
        ESP_LOGD(TAG, "No pending commands");
        return 0;
    }

    uint16_t newCommands = 0;
    
    // Simple JSON parsing - find all command IDs and their data
    // Note: This is simplified, should use ArduinoJson for production
    int startPos = 0;
    while (true) {
        // Find next command ID (between quotes before :)
        int quoteStart = result.indexOf("\"", startPos);
        if (quoteStart < 0) break;
        
        int quoteEnd = result.indexOf("\"", quoteStart + 1);
        if (quoteEnd < 0) break;
        
        String commandId = result.substring(quoteStart + 1, quoteEnd);
        
        // Find command data (between { and })
        int dataStart = result.indexOf("{", quoteEnd);
        if (dataStart < 0) break;
        
        int depth = 1;
        int dataEnd = dataStart + 1;
        while (depth > 0 && dataEnd < result.length()) {
            if (result[dataEnd] == '{') depth++;
            else if (result[dataEnd] == '}') depth--;
            dataEnd++;
        }
        
        String commandData = result.substring(dataStart, dataEnd);
        
        // Parse command
        Command cmd;
        if (parseCommand(commandId, commandData, cmd)) {
            m_pendingCommands.push_back(cmd);
            newCommands++;
            m_stats.totalCommands++;
            ESP_LOGI(TAG, "Parsed command: %s - %s", 
                    commandId.c_str(), commandTypeToString(cmd.type));
        }
        
        startPos = dataEnd;
    }
    
    return newCommands;
}

bool FirebaseCommandQueue::parseCommand(const String& commandId, 
                                       const String& json, 
                                       Command& command) {
    command.id = commandId;
    
    // Extract "type" field
    int typeStart = json.indexOf("\"type\"");
    if (typeStart < 0) {
        ESP_LOGW(TAG, "Command missing 'type' field");
        return false;
    }
    
    int valueStart = json.indexOf("\"", typeStart + 6);
    int valueEnd = json.indexOf("\"", valueStart + 1);
    
    if (valueStart < 0 || valueEnd < 0) {
        ESP_LOGW(TAG, "Failed to parse type value");
        return false;
    }
    
    String typeStr = json.substring(valueStart + 1, valueEnd);
    command.type = stringToCommandType(typeStr);
    
    if (command.type == CommandType::UNKNOWN) {
        ESP_LOGW(TAG, "Unknown command type: %s", typeStr.c_str());
        return false;
    }
    
    // Extract "timestamp" field
    int tsStart = json.indexOf("\"timestamp\"");
    if (tsStart >= 0) {
        int tsValueStart = json.indexOf(":", tsStart);
        int tsValueEnd = json.indexOf(",", tsValueStart);
        if (tsValueEnd < 0) tsValueEnd = json.indexOf("}", tsValueStart);
        
        if (tsValueStart >= 0 && tsValueEnd >= 0) {
            String tsStr = json.substring(tsValueStart + 1, tsValueEnd);
            tsStr.trim();
            command.timestamp = tsStr.toInt();
        }
    }
    
    // Extract "params" object if exists
    int paramsStart = json.indexOf("\"params\"");
    if (paramsStart >= 0) {
        int objStart = json.indexOf("{", paramsStart);
        int objEnd = json.indexOf("}", objStart);
        
        if (objStart >= 0 && objEnd >= 0) {
            String paramsJson = json.substring(objStart, objEnd + 1);
            parseParams(paramsJson, command.params);
        }
    }
    
    command.status = ExecutionStatus::PENDING;
    
    return true;
}

bool FirebaseCommandQueue::parseParams(const String& json, 
                                      std::map<String, String>& params) {
    // Simple key-value parsing
    // Example: {"key1":"value1","key2":"value2"}
    
    int pos = 1; // Skip opening {
    while (pos < json.length()) {
        int keyStart = json.indexOf("\"", pos);
        if (keyStart < 0) break;
        
        int keyEnd = json.indexOf("\"", keyStart + 1);
        if (keyEnd < 0) break;
        
        String key = json.substring(keyStart + 1, keyEnd);
        
        int valueStart = json.indexOf("\"", keyEnd + 1);
        if (valueStart < 0) break;
        
        int valueEnd = json.indexOf("\"", valueStart + 1);
        if (valueEnd < 0) break;
        
        String value = json.substring(valueStart + 1, valueEnd);
        
        params[key] = value;
        
        pos = valueEnd + 1;
    }
    
    return true;
}

bool FirebaseCommandQueue::executeCommand(Command& command) {
    // Check if handler is registered
    auto it = m_handlers.find(command.type);
    if (it == m_handlers.end()) {
        command.result = "No handler registered for command type";
        ESP_LOGW(TAG, "%s", command.result.c_str());
        return false;
    }

    // Execute command via callback
    try {
        bool success = it->second(command);
        
        if (!success && command.result.isEmpty()) {
            command.result = "Command handler returned false";
        } else if (success && command.result.isEmpty()) {
            command.result = "Command executed successfully";
        }
        
        return success;
    } catch (...) {
        command.result = "Exception during command execution";
        ESP_LOGE(TAG, "%s", command.result.c_str());
        return false;
    }
}

bool FirebaseCommandQueue::moveToProcessing(const String& commandId) {
    String path = "/commands/" + m_gatewayId + "/processing/" + commandId;
    String json = "{\"status\":\"processing\",\"timestamp\":" + String(millis()) + "}";
    
    auto result = m_firebaseClient->put(path, json);
    return result.success();
}

bool FirebaseCommandQueue::moveToCompleted(const Command& command) {
    String path = "/commands/" + m_gatewayId + "/completed/" + command.id;
    
    String json = "{";
    json += "\"type\":\"" + String(commandTypeToString(command.type)) + "\",";
    json += "\"result\":\"" + command.result + "\",";
    json += "\"completedAt\":" + String(millis());
    json += "}";
    
    auto result = m_firebaseClient->put(path, json);
    return result.success();
}

bool FirebaseCommandQueue::moveToFailed(const Command& command) {
    String path = "/commands/" + m_gatewayId + "/failed/" + command.id;
    
    String json = "{";
    json += "\"type\":\"" + String(commandTypeToString(command.type)) + "\",";
    json += "\"error\":\"" + command.result + "\",";
    json += "\"failedAt\":" + String(millis());
    json += "}";
    
    auto result = m_firebaseClient->put(path, json);
    return result.success();
}

bool FirebaseCommandQueue::deletePending(const String& commandId) {
    String path = "/commands/" + m_gatewayId + "/pending/" + commandId;
    
    auto result = m_firebaseClient->deleteData(path);
    return result.success();
}
