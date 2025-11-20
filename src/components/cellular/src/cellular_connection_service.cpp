#include "cellular_connection_service.h"
#include "cellular_firebase_queue.h"
#include <esp_log.h>
#include <time.h>
#include <sys/time.h>

const char* CellularConnectionService::TAG = "CELLULAR_CONN";

CellularConnectionService::CellularConnectionService(
    const APNConfig& apnConfig,
    bool autoReconnect,
    uint32_t reconnectIntervalMs
)
    : m_uart(nullptr)
    , m_atHandler(nullptr)
    , m_apnConfig(apnConfig)
    , m_autoReconnect(autoReconnect)
    , m_reconnectIntervalMs(reconnectIntervalMs)
    , m_status(Status::DISCONNECTED)
    , m_registrationState(RegState::NOT_REGISTERED)
    , m_currentRSSI(99)
    , m_signalThreshold(DEFAULT_SIGNAL_THRESHOLD)
    , m_lastReconnectAttempt(0)
    , m_currentReconnectInterval(reconnectIntervalMs)
    , m_reconnectBackoffMultiplier(RECONNECT_BACKOFF_MULTIPLIER)
    , m_maxReconnectInterval(MAX_RECONNECT_INTERVAL)
    , m_connectionStartTime(0)
    , m_eventCallback(nullptr)
    , m_netOpenSuccess(false)
    , m_netOpenReceived(false)
    , m_urcProcessingTask(nullptr)
{
    // Initialize statistics
    memset(&m_stats, 0, sizeof(Stats));
    m_stats.currentRSSI = 99;
    m_stats.registrationState = RegState::NOT_REGISTERED;
}

CellularConnectionService::~CellularConnectionService() {
    if (m_atHandler) {
        delete m_atHandler;
    }
    if (m_uart) {
        delete m_uart;
    }
}

bool CellularConnectionService::initialize() {
    ESP_LOGI(TAG, "Initializing cellular connection service...");
    
    // Create UART instance
    m_uart = new CellularUART();
    if (!m_uart) {
        ESP_LOGE(TAG, "Failed to create UART instance");
        return false;
    }

    // Initialize UART
    if (!m_uart->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize UART");
        return false;
    }

    // Power on module
    if (!powerOnModule()) {
        ESP_LOGE(TAG, "Failed to power on module");
        return false;
    }

    // Create async AT handler
    m_atHandler = new ATCommandAsync(m_uart);
    if (!m_atHandler) {
        ESP_LOGE(TAG, "Failed to create async AT handler");
        return false;
    }

    // Initialize async receiver task (runs on separate core)
    m_atHandler->initialize();
    ESP_LOGI(TAG, "🚀 Async AT handler initialized - receiver task running independently");

    // Register URC callback
    m_atHandler->registerURCCallback([this](const String& urc) {
        this->handleURC(urc);
    });
    
    // Start dedicated URC processing task (non-blocking, runs asynchronously)
    startURCProcessingTask();

    // Disable echo (blocking for init only)
    m_atHandler->sendCommand("+E0", 1000, true);

    // Configure error format (numeric)
    m_atHandler->sendCommand("+CMEE=1");  // Enable error codes

    // Check SIM card
    if (!checkSIMCard()) {
        ESP_LOGE(TAG, "SIM card not ready");
        return false;
    }

    // Get IMEI
    auto resp = m_atHandler->sendCommand("+GSN");
    if (resp.success) {
        m_imei = resp.data;
        m_imei.trim();
        ESP_LOGI(TAG, "IMEI: %s", m_imei.c_str());
    } else {
        ESP_LOGW(TAG, "Failed to get IMEI");
    }

    // Update signal quality
    updateSignalQuality();

    // Register this service with the Firebase queue for periodic checks
    CellularFirebaseQueue::getInstance().setConnectionService(this);

    ESP_LOGI(TAG, "Initialization complete");
    return true;
}

bool CellularConnectionService::connect(uint32_t timeoutMs) {
    ESP_LOGI(TAG, "Connecting to cellular network...");
    ESP_LOGI(TAG, "APN: %s", m_apnConfig.apn.c_str());

    m_status = Status::REGISTERING;
    m_stats.connectionAttempts++;
    uint32_t startTime = millis();

    // Step 1: Wait for network registration
    if (!waitForNetworkRegistration(timeoutMs / 2)) {
        ESP_LOGE(TAG, "Network registration failed");
        m_status = Status::FAILED;
        m_stats.failedConnections++;
        triggerEvent(Event::CONNECTION_FAILED, m_currentRSSI);
        return false;
    }

    // Step 2: Configure APN
    if (!configureAPN()) {
        ESP_LOGE(TAG, "APN configuration failed");
        m_status = Status::FAILED;
        m_stats.failedConnections++;
        triggerEvent(Event::CONNECTION_FAILED, m_currentRSSI);
        return false;
    }

    // Step 3: Attach to GPRS/LTE
    if (!attachGPRS()) {
        ESP_LOGE(TAG, "GPRS attach failed");
        m_status = Status::FAILED;
        m_stats.failedConnections++;
        triggerEvent(Event::CONNECTION_FAILED, m_currentRSSI);
        return false;
    }

    // Step 4: Activate PDP context
    m_status = Status::ACTIVATING_PDP;
    if (!activatePDPContext()) {
        ESP_LOGE(TAG, "PDP context activation failed");
        m_status = Status::FAILED;
        m_stats.failedConnections++;
        triggerEvent(Event::CONNECTION_FAILED, m_currentRSSI);
        return false;
    }

    // Step 5: Get IP address
    if (!updateIPAddress()) {
        ESP_LOGW(TAG, "Failed to get IP address, but continuing...");
    }

    // Success!
    m_status = Status::CONNECTED;
    m_stats.successfulConnections++;
    m_stats.lastConnectTime = millis();
    m_connectionStartTime = millis();
    resetReconnectBackoff();

    uint32_t connectTime = millis() - startTime;
    ESP_LOGI(TAG, "✅ Connected to cellular network (%.1fs)", connectTime / 1000.0f);
    ESP_LOGI(TAG, "IP: %s, Operator: %s, RSSI: %d", 
             m_ipAddress.c_str(), m_operator.c_str(), m_currentRSSI);

    triggerEvent(Event::CONNECTED, m_currentRSSI);
    return true;
}

bool CellularConnectionService::disconnect() {
    ESP_LOGI(TAG, "Disconnecting from cellular network...");

    if (m_status == Status::DISCONNECTED) {
        ESP_LOGW(TAG, "Already disconnected");
        return true;
    }

    // Deactivate PDP context
    deactivatePDPContext();

    // Detach from GPRS
    detachGPRS();

    // Update statistics
    if (m_connectionStartTime > 0) {
        m_stats.totalConnectedTime += (millis() - m_connectionStartTime);
        m_connectionStartTime = 0;
    }
    m_stats.lastDisconnectTime = millis();

    m_status = Status::DISCONNECTED;
    m_ipAddress = "";

    ESP_LOGI(TAG, "Disconnected");
    triggerEvent(Event::DISCONNECTED, m_currentRSSI);

    return true;
}

bool CellularConnectionService::isConnected() const {
    return m_status == Status::CONNECTED;
}

CellularConnectionService::Status CellularConnectionService::getStatus() const {
    return m_status;
}

void CellularConnectionService::update() {
    // Process any pending async command responses (non-blocking)
    // This checks if any of our async queries have completed
    processPendingAsyncResponses();

    // URC processing is handled by background task - no need to call processURCs() here

    // Periodic checks are now enqueued to the Firebase queue to maintain
    // single-threaded AT command access
    static uint32_t lastSignalUpdate = 0;
    static uint32_t lastRegistrationUpdate = 15000;  // Offset by 15s
    
    uint32_t now = millis();
    
    // Enqueue signal quality check every 60 seconds (priority 1 = low)
    if (now - lastSignalUpdate > 60000) {
        lastSignalUpdate = now;
        CellularFirebaseQueue::getInstance().enqueueSignalQualityCheck(1);
    }
    
    // Enqueue registration state check every 60 seconds, offset by 15s (priority 1 = low)
    if (now - lastRegistrationUpdate > 60000) {
        lastRegistrationUpdate = now;
        CellularFirebaseQueue::getInstance().enqueueRegistrationStateCheck(1);
    }

    // Check connection status
    if (m_status == Status::CONNECTED) {
        // Verify PDP context is still active
        // (URCs will notify us if it deactivates, but we can also poll)
        // For now, rely on URCs
        
        // Check if connection lost
        if (m_registrationState != RegState::REGISTERED_HOME && 
            m_registrationState != RegState::REGISTERED_ROAMING) {
            ESP_LOGW(TAG, "Network registration lost!");
            m_status = Status::DISCONNECTED;
            if (m_connectionStartTime > 0) {
                m_stats.totalConnectedTime += (millis() - m_connectionStartTime);
                m_connectionStartTime = 0;
            }
            m_stats.lastDisconnectTime = millis();
            triggerEvent(Event::DISCONNECTED, m_currentRSSI);
        }
    }

    // Auto-reconnect logic
    if (m_autoReconnect && 
        (m_status == Status::DISCONNECTED || m_status == Status::FAILED)) {
        
        uint32_t now = millis();
        if (now - m_lastReconnectAttempt >= m_currentReconnectInterval) {
            m_lastReconnectAttempt = now;
            m_stats.reconnectAttempts++;
            
            ESP_LOGI(TAG, "Auto-reconnecting (attempt %d, interval %ds)...",
                     m_stats.reconnectAttempts,
                     m_currentReconnectInterval / 1000);
            
            m_status = Status::RECONNECTING;
            triggerEvent(Event::RECONNECTING, m_currentRSSI);
            
            if (connect(60000)) {
                ESP_LOGI(TAG, "✅ Auto-reconnect successful");
            } else {
                ESP_LOGE(TAG, "❌ Auto-reconnect failed");
                
                // Increase backoff interval
                m_currentReconnectInterval = min(
                    (uint32_t)(m_currentReconnectInterval * m_reconnectBackoffMultiplier),
                    m_maxReconnectInterval
                );
                
                ESP_LOGI(TAG, "Next retry in %ds", m_currentReconnectInterval / 1000);
            }
        }
    }
}

void CellularConnectionService::onEvent(EventCallback callback) {
    m_eventCallback = callback;
}

int8_t CellularConnectionService::getSignalQuality() const {
    return m_currentRSSI;
}

String CellularConnectionService::getIMEI() const {
    return m_imei;
}

String CellularConnectionService::getOperator() const {
    return m_operator;
}

String CellularConnectionService::getIPAddress() const {
    return m_ipAddress;
}

CellularConnectionService::RegState CellularConnectionService::getRegistrationState() const {
    return m_registrationState;
}

CellularConnectionService::Stats CellularConnectionService::getStats() const {
    Stats stats = m_stats;
    
    // Update current values
    stats.currentRSSI = m_currentRSSI;
    stats.registrationState = m_registrationState;
    
    // Update connected time if currently connected
    if (m_connectionStartTime > 0) {
        stats.totalConnectedTime = m_stats.totalConnectedTime + 
                                   (millis() - m_connectionStartTime);
    }
    
    return stats;
}

ATCommandAsync* CellularConnectionService::getATHandler() const {
    return m_atHandler;
}

void CellularConnectionService::setAutoReconnect(bool enable) {
    m_autoReconnect = enable;
    ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");
}

bool CellularConnectionService::getAutoReconnect() const {
    return m_autoReconnect;
}

void CellularConnectionService::setSignalQualityThreshold(int8_t threshold) {
    m_signalThreshold = threshold;
    ESP_LOGI(TAG, "Signal quality threshold set to %d", threshold);
}

// ===== Private Methods =====

bool CellularConnectionService::powerOnModule() {
    ESP_LOGI(TAG, "Powering on module...");
    
    if (!m_uart->powerOn(5000)) {
        return false;
    }
    
    // Wait longer for module to fully boot and stabilize UART
    // A7682S can take 8-10s to fully initialize after power-on
    ESP_LOGI(TAG, "Waiting for module to stabilize...");
    delay(5000);  // Increased from 2000ms to 5000ms for A7682S
    
    // Extra wait for UART to be ready
    delay(1000);
    
    return true;
}

bool CellularConnectionService::checkSIMCard() {
    ESP_LOGI(TAG, "Checking SIM card...");
    
    // Try up to 5 times (SIM can take time to initialize)
    for (int i = 0; i < 5; i++) {
        auto resp = m_atHandler->sendCommand("+CPIN?", 5000);
        
        if (resp.success) {
            if (resp.data.indexOf("READY") >= 0) {
                ESP_LOGI(TAG, "✅ SIM card ready");
                return true;
            } else if (resp.data.indexOf("SIM PIN") >= 0) {
                ESP_LOGE(TAG, "SIM PIN required (not implemented)");
                return false;
            }
        }
        
        delay(1000);
    }
    
    ESP_LOGE(TAG, "SIM card not ready");
    return false;
}

bool CellularConnectionService::waitForNetworkRegistration(uint32_t timeoutMs) {
    ESP_LOGI(TAG, "Waiting for network registration (timeout: %ds)...", 
             timeoutMs / 1000);
    
    uint32_t startTime = millis();
    
    while (millis() - startTime < timeoutMs) {
        if (updateRegistrationState()) {
            if (m_registrationState == RegState::REGISTERED_HOME ||
                m_registrationState == RegState::REGISTERED_ROAMING) {
                
                // Get operator info
                auto resp = m_atHandler->sendCommand("+COPS?");
                if (resp.success) {
                    // Parse: +COPS: <mode>,<format>,"<oper>",<act>
                    int startIdx = resp.data.indexOf('"');
                    int endIdx = resp.data.indexOf('"', startIdx + 1);
                    if (startIdx >= 0 && endIdx > startIdx) {
                        m_operator = resp.data.substring(startIdx + 1, endIdx);
                    }
                }
                
                ESP_LOGI(TAG, "✅ Registered on network: %s (%s)",
                         m_operator.c_str(),
                         m_registrationState == RegState::REGISTERED_HOME ? "Home" : "Roaming");
                
                m_status = Status::REGISTERED;
                return true;
            }
        }
        
        delay(2000);  // Check every 2 seconds
    }
    
    ESP_LOGE(TAG, "Network registration timeout");
    return false;
}

bool CellularConnectionService::configureAPN() {
    ESP_LOGI(TAG, "Configuring APN: %s", m_apnConfig.apn.c_str());
    
    // Set PDP context (CID=1, IP type, APN)
    String cmd = "+CGDCONT=1,\"IP\",\"" + m_apnConfig.apn + "\"";
    auto resp = m_atHandler->sendCommand(cmd, 5000);
    
    if (!resp.success) {
        ESP_LOGE(TAG, "Failed to configure APN");
        return false;
    }
    
    // If username/password provided, configure authentication
    if (m_apnConfig.username.length() > 0) {
        // AT+CGAUTH=<cid>,<auth_type>,<username>,<password>
        // auth_type: 0=None, 1=PAP, 2=CHAP
        String authCmd = "+CGAUTH=1,1,\"" + m_apnConfig.username + "\",\"" + 
                         m_apnConfig.password + "\"";
        m_atHandler->sendCommand(authCmd, 5000);
    }
    
    ESP_LOGI(TAG, "✅ APN configured");
    return true;
}

bool CellularConnectionService::attachGPRS() {
    ESP_LOGI(TAG, "Attaching to GPRS/LTE...");
    
    // Check if already attached
    auto resp = m_atHandler->sendCommand("+CGATT?");
    if (resp.success && resp.data.indexOf("1") >= 0) {
        ESP_LOGI(TAG, "Already attached to GPRS");
        return true;
    }
    
    // Attach to GPRS (AT+CGATT=1)
    resp = m_atHandler->sendCommand("+CGATT=1", 30000);  // 30s timeout
    
    if (!resp.success) {
        ESP_LOGE(TAG, "GPRS attach failed");
        return false;
    }
    
    // Verify attachment
    delay(1000);
    resp = m_atHandler->sendCommand("+CGATT?");
    if (resp.success && resp.data.indexOf("1") >= 0) {
        ESP_LOGI(TAG, "✅ Attached to GPRS/LTE");
        return true;
    }
    
    ESP_LOGE(TAG, "GPRS attach verification failed");
    return false;
}

bool CellularConnectionService::activatePDPContext() {
    ESP_LOGI(TAG, "Activating PDP context...");
    
    // Check if already opened
    auto checkResp = m_atHandler->sendCommand("+NETOPEN?");
    if (checkResp.success && checkResp.data.indexOf("1") >= 0) {
        ESP_LOGI(TAG, "PDP context already active");
        return true;
    }
    
    // Reset flags
    m_netOpenReceived = false;
    m_netOpenSuccess = false;
    
    // A7682S uses AT+NETOPEN for network activation
    // IMPORTANT: Response OK doesn't mean network is ready!
    // Must wait for URC: +NETOPEN: 0
    auto resp = m_atHandler->sendCommand("+NETOPEN", 5000);
    
    if (!resp.success) {
        if (resp.data.indexOf("Network is already opened") >= 0) {
            ESP_LOGI(TAG, "PDP context already active");
            return true;
        }
        ESP_LOGE(TAG, "PDP context activation failed: %s", resp.data.c_str());
        return false;
    }
    
    // Now wait for URC: +NETOPEN: 0 (handled by handleURC)
    ESP_LOGI(TAG, "Waiting for +NETOPEN URC...");
    uint32_t startTime = millis();
    
    while (millis() - startTime < 30000) {  // 30s timeout
        // URC callback will automatically update flags (background task handles URCs)
        // No need to call processURCs() - it runs in background task
        delay(100);
        
        // Check flags set by handleURC
        if (m_netOpenReceived) {
            if (m_netOpenSuccess) {
                // Wait a bit more for network to stabilize
                delay(1000);
                ESP_LOGI(TAG, "✅ PDP context activated");
                triggerEvent(Event::PDP_ACTIVATED, m_currentRSSI);
                return true;
            } else {
                ESP_LOGE(TAG, "Network open failed (URC indicated error)");
                return false;
            }
        }
    }
    
    ESP_LOGE(TAG, "Timeout waiting for +NETOPEN URC");
    return false;
}

bool CellularConnectionService::deactivatePDPContext() {
    ESP_LOGI(TAG, "Deactivating PDP context...");
    
    auto resp = m_atHandler->sendCommand("+NETCLOSE", 10000);
    
    if (resp.success || resp.data.indexOf("Network closed") >= 0) {
        ESP_LOGI(TAG, "PDP context deactivated");
        triggerEvent(Event::PDP_DEACTIVATED, m_currentRSSI);
        return true;
    }
    
    return false;
}

bool CellularConnectionService::detachGPRS() {
    ESP_LOGI(TAG, "Detaching from GPRS...");
    
    auto resp = m_atHandler->sendCommand("+CGATT=0", 10000);
    return resp.success;
}

bool CellularConnectionService::updateSignalQuality() {
    // Async approach: Check if previous query is done
    if (m_signalQualityQueryId != 0) {
        // Previous query is pending - check if it's complete
        ATCommandAsync::Response resp;
        if (m_atHandler->isCommandComplete(m_signalQualityQueryId, &resp)) {
            // Response received!
            if (resp.success) {
                // Parse: +CSQ: 25,99
                String line = resp.data;
                int colonIdx = line.indexOf(':');
                if (colonIdx >= 0) {
                    String value = line.substring(colonIdx + 1);
                    value.trim();
                    int commaIdx = value.indexOf(',');
                    if (commaIdx >= 0) {
                        String csqStr = value.substring(0, commaIdx);
                        int8_t newRSSI = csqStr.toInt();
                        
                        if (newRSSI != m_currentRSSI) {
                            m_currentRSSI = newRSSI;
                            m_stats.currentRSSI = newRSSI;
                            
                            // Check if signal is weak
                            if (m_currentRSSI < m_signalThreshold && m_currentRSSI != 99) {
                                ESP_LOGW(TAG, "⚠️  Weak signal: RSSI=%d", m_currentRSSI);
                                triggerEvent(Event::SIGNAL_LOW, m_currentRSSI);
                            }
                        }
                        
                        ESP_LOGD(TAG, "📶 Signal quality updated: RSSI=%d", m_currentRSSI);
                    }
                }
            } else {
                ESP_LOGD(TAG, "Signal quality query failed: %s", resp.errorMessage.c_str());
            }
            
            m_signalQualityQueryId = 0;  // Reset for next query
            m_lastSignalQualityQueryTime = millis();
            return resp.success;
        }
        // Still waiting for response - don't send new query
        return false;
    } else {
        // No pending query - check if we should send new one
        uint32_t now = millis();
        if (now - m_lastSignalQualityQueryTime >= m_signalQualityQueryIntervalMs) {
            // Send new query (non-blocking)
            m_signalQualityQueryId = m_atHandler->sendCommandAsync("+CSQ", 3000, true);
            ESP_LOGD(TAG, "📶 Async signal quality query sent (ID: %u)", m_signalQualityQueryId);
            return true;
        }
    }
    
    return false;
}

bool CellularConnectionService::updateRegistrationState() {
    // Async approach: Check if previous query is done
    if (m_registrationQueryId != 0) {
        // Previous query is pending - check if it's complete
        ATCommandAsync::Response resp;
        if (m_atHandler->isCommandComplete(m_registrationQueryId, &resp)) {
            // Response received!
            if (resp.success) {
                // Parse: +CREG: 0,1 or +CREG: 0,1,0001,DEADBEEF
                String line = resp.data;
                int colonIdx = line.indexOf(':');
                if (colonIdx >= 0) {
                    String value = line.substring(colonIdx + 1);
                    value.trim();
                    int commaIdx = value.indexOf(',');
                    if (commaIdx >= 0) {
                        String regStr = value.substring(commaIdx + 1);
                        regStr.trim();
                        int regVal = regStr.toInt();
                        
                        RegState newState = (RegState)regVal;
                        if (newState != m_registrationState) {
                            m_registrationState = newState;
                            m_stats.registrationState = newState;
                            ESP_LOGI(TAG, "📡 Registration state changed: %s", regStateToString(newState));
                        }
                    }
                }
            } else {
                ESP_LOGD(TAG, "Registration query failed: %s", resp.errorMessage.c_str());
            }
            
            m_registrationQueryId = 0;  // Reset for next query
            m_lastRegistrationQueryTime = millis();
            return resp.success;
        }
        // Still waiting for response - don't send new query
        return false;
    } else {
        // No pending query - check if we should send new one
        uint32_t now = millis();
        if (now - m_lastRegistrationQueryTime >= m_registrationQueryIntervalMs) {
            // Send new query (non-blocking)
            m_registrationQueryId = m_atHandler->sendCommandAsync("+CREG?", 3000, true);
            ESP_LOGD(TAG, "📡 Async registration query sent (ID: %u)", m_registrationQueryId);
            return true;
        }
    }
    
    return false;
}

// Helper: Process pending async responses (called in update())
void CellularConnectionService::processPendingAsyncResponses() {
    // Check signal quality response
    if (m_signalQualityQueryId != 0) {
        ATCommandAsync::Response resp;
        if (m_atHandler->isCommandComplete(m_signalQualityQueryId, &resp)) {
            if (resp.success) {
                // Parse: +CSQ: 25,99
                int colonIdx = resp.data.indexOf(':');
                if (colonIdx >= 0) {
                    String value = resp.data.substring(colonIdx + 1);
                    value.trim();
                    int commaIdx = value.indexOf(',');
                    if (commaIdx >= 0) {
                        String csqStr = value.substring(0, commaIdx);
                        int8_t newRSSI = csqStr.toInt();
                        
                        if (newRSSI != m_currentRSSI) {
                            m_currentRSSI = newRSSI;
                            m_stats.currentRSSI = newRSSI;
                            
                            if (m_currentRSSI < m_signalThreshold && m_currentRSSI != 99) {
                                triggerEvent(Event::SIGNAL_LOW, m_currentRSSI);
                            }
                        }
                    }
                }
            }
            m_signalQualityQueryId = 0;
        }
    }
    
    // Check registration state response
    if (m_registrationQueryId != 0) {
        ATCommandAsync::Response resp;
        if (m_atHandler->isCommandComplete(m_registrationQueryId, &resp)) {
            if (resp.success) {
                // Parse: +CREG: 0,1 or +CREG: 0,1,0001,DEADBEEF
                int colonIdx = resp.data.indexOf(':');
                if (colonIdx >= 0) {
                    String value = resp.data.substring(colonIdx + 1);
                    value.trim();
                    int commaIdx = value.indexOf(',');
                    if (commaIdx >= 0) {
                        String regStr = value.substring(commaIdx + 1);
                        regStr.trim();
                        int regVal = regStr.toInt();
                        
                        RegState newState = (RegState)regVal;
                        if (newState != m_registrationState) {
                            m_registrationState = newState;
                            m_stats.registrationState = newState;
                        }
                    }
                }
            }
            m_registrationQueryId = 0;
        }
    }
}

bool CellularConnectionService::updateIPAddress() {
    ESP_LOGI(TAG, "Getting IP address...");
    
    // A7682S: AT+IPADDR to get IP address
    // Sometimes takes a few seconds after NETOPEN, so retry
    for (int retry = 0; retry < 5; retry++) {
        auto resp = m_atHandler->sendCommand("+IPADDR", 5000);
        
        if (resp.success) {
            // Response: +IPADDR: <ip>
            String value = ATCommandAsync::extractValue(resp.data, "+IPADDR:");
            value.trim();
            
            if (value.length() > 0 && !value.startsWith("ERROR")) {
                m_ipAddress = value;
                ESP_LOGI(TAG, "✅ IP address: %s", m_ipAddress.c_str());
                return true;
            }
        }
        
        if (retry < 4) {
            ESP_LOGD(TAG, "IP not ready yet, retry %d/5 in 2s...", retry + 1);
            delay(2000);
        }
    }
    
    ESP_LOGW(TAG, "Failed to get IP address after 5 retries");
    return false;
}

void CellularConnectionService::resetReconnectBackoff() {
    m_currentReconnectInterval = m_reconnectIntervalMs;
}

void CellularConnectionService::triggerEvent(Event event, int8_t rssi) {
    if (m_eventCallback) {
        m_eventCallback(event, rssi);
    }
}

void CellularConnectionService::handleURC(const String& urc) {
    // Handle unsolicited result codes
    
    // Forward to secondary callback (for SSL/TCP clients) FIRST
    // so they can handle their specific URCs
    if (m_secondaryURCCallback) {
        m_secondaryURCCallback(urc);
    }
    
    if (urc.startsWith("+CREG:")) {
        // Network registration changed
        updateRegistrationState();
    } else if (urc.startsWith("+CSQ:")) {
        // Signal quality changed
        updateSignalQuality();
    } else if (urc.startsWith("+NETOPEN:")) {
        // Network open notification
        ESP_LOGD(TAG, "URC: %s", urc.c_str());
        
        // Parse: +NETOPEN: <err>
        // err=0: success, err>0: failed
        int colonPos = urc.indexOf(':');
        if (colonPos >= 0) {
            String errCode = urc.substring(colonPos + 1);
            errCode.trim();
            
            m_netOpenReceived = true;
            if (errCode == "0") {
                m_netOpenSuccess = true;
                ESP_LOGI(TAG, "✅ Network opened successfully (URC)");
            } else {
                m_netOpenSuccess = false;
                ESP_LOGE(TAG, "Network open failed: error code %s (URC)", errCode.c_str());
            }
        }
    } else if (urc.startsWith("+NETCLOSE:") || urc.startsWith("+CIPERROR:")) {
        // PDP context closed unexpectedly
        ESP_LOGW(TAG, "URC: Network closed: %s", urc.c_str());
        if (m_status == Status::CONNECTED) {
            m_status = Status::DISCONNECTED;
            triggerEvent(Event::DISCONNECTED, m_currentRSSI);
        }
    } else if (urc.startsWith("+CGEV:")) {
        // GPRS event notification (informational only)
        ESP_LOGD(TAG, "URC: %s", urc.c_str());
    }
}

const char* CellularConnectionService::regStateToString(RegState state) {
    switch (state) {
        case RegState::NOT_REGISTERED: return "NOT_REGISTERED";
        case RegState::REGISTERED_HOME: return "REGISTERED_HOME";
        case RegState::SEARCHING: return "SEARCHING";
        case RegState::DENIED: return "DENIED";
        case RegState::UNKNOWN: return "UNKNOWN";
        case RegState::REGISTERED_ROAMING: return "REGISTERED_ROAMING";
        default: return "INVALID";
    }
}

const char* CellularConnectionService::statusToString(Status status) {
    switch (status) {
        case Status::DISCONNECTED: return "DISCONNECTED";
        case Status::INITIALIZING: return "INITIALIZING";
        case Status::REGISTERING: return "REGISTERING";
        case Status::REGISTERED: return "REGISTERED";
        case Status::ACTIVATING_PDP: return "ACTIVATING_PDP";
        case Status::CONNECTED: return "CONNECTED";
        case Status::RECONNECTING: return "RECONNECTING";
        case Status::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

void CellularConnectionService::registerSecondaryURCCallback(SecondaryURCCallback callback) {
    m_secondaryURCCallback = callback;
    ESP_LOGD(TAG, "Secondary URC callback registered");
}

bool CellularConnectionService::syncTimeFromNetwork() {
    if (!m_atHandler) return false;

    // Query clock from modem: +CCLK: "yy/MM/dd,hh:mm:ss+zz"
    auto resp = m_atHandler->sendCommand("+CCLK?", 2000);
    if (!resp.success) {
        ESP_LOGW(TAG, "Failed to query modem time (+CCLK?)");
        return false;
    }

    int idx = resp.data.indexOf("+CCLK:");
    if (idx < 0) {
        ESP_LOGW(TAG, "+CCLK? returned unexpected format");
        return false;
    }

    int quote1 = resp.data.indexOf('"', idx);
    int quote2 = resp.data.indexOf('"', quote1 + 1);
    if (quote1 < 0 || quote2 < 0 || quote2 <= quote1 + 1) {
        ESP_LOGW(TAG, "+CCLK? missing quoted datetime");
        return false;
    }

    String dt = resp.data.substring(quote1 + 1, quote2);
    // Expected: yy/MM/dd,hh:mm:ss±tz
    // We'll parse date and time; timezone is optional and may be in 15-min units.
    int yy = dt.substring(0, 2).toInt();
    int MM = dt.substring(3, 5).toInt();
    int dd = dt.substring(6, 8).toInt();
    int hh = dt.substring(9, 11).toInt();
    int mm = dt.substring(12, 14).toInt();
    int ss = dt.substring(15, 17).toInt();

    struct tm t = {};
    t.tm_year = 100 + yy;   // years since 1900, 2000+yy
    t.tm_mon  = MM - 1;     // 0-11
    t.tm_mday = dd;
    t.tm_hour = hh;
    t.tm_min  = mm;
    t.tm_sec  = ss;

    // Treat parsed time as local; convert to epoch (we assume UTC; adjust if tz sign present)
    time_t epoch = mktime(&t);
    if (epoch <= 0) {
        ESP_LOGW(TAG, "Failed to convert modem time");
        return false;
    }

    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(TAG, "settimeofday failed");
        return false;
    }

    ESP_LOGI(TAG, "🕒 System time set from modem: %04d-%02d-%02d %02d:%02d:%02d", 
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return true;
}

void CellularConnectionService::startURCProcessingTask() {
    if (m_urcProcessingTask) {
        ESP_LOGW(TAG, "URC processing task already running");
        return;
    }
    
    if (!m_atHandler) {
        ESP_LOGE(TAG, "AT handler not available - cannot start URC processing task");
        return;
    }
    
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        urcProcessingTaskFunction,      // Task function
        "CellularURCProcessor",         // Task name
        4096,                           // Stack size
        this,                           // Parameter (this pointer)
        5,                              // Priority
        &m_urcProcessingTask,           // Task handle
        1                               // Core (Core 1)
    );
    
    if (taskCreated != pdPASS) {
        ESP_LOGE(TAG, "Failed to create URC processing task");
        m_urcProcessingTask = nullptr;
        return;
    }
    
    ESP_LOGI(TAG, "✅ URC processing task started (non-blocking, asynchronous)");
}

void CellularConnectionService::urcProcessingTaskFunction(void* parameter) {
    auto* self = static_cast<CellularConnectionService*>(parameter);
    
    if (!self || !self->m_atHandler) {
        ESP_LOGE("CellularURCProcessor", "Invalid parameters");
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI("CellularURCProcessor", "URC processing task started on Core %d", xPortGetCoreID());
    
    // Continuously process URCs in background (via ATCommandAsync receiver task)
    // This task primarily handles high-level URC callbacks
    while (true) {
        // Background receiver task in ATCommandAsync handles UART reading
        // This task can do additional URC post-processing if needed
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
