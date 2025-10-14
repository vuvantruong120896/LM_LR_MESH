#include "wifi_connection_service.h"
#include <Arduino.h>

// Constants
constexpr uint32_t RECONNECT_BACKOFF_MULTIPLIER = 2;
constexpr uint32_t MAX_RECONNECT_INTERVAL = 60000; // 60 seconds
constexpr uint32_t RSSI_UPDATE_INTERVAL = 5000;    // 5 seconds
constexpr int8_t DEFAULT_RSSI_THRESHOLD = -80;     // dBm

WiFiConnectionService::WiFiConnectionService(
    const char* ssid,
    const char* password,
    bool autoReconnect,
    uint32_t reconnectIntervalMs
)
    : m_ssid(ssid)
    , m_password(password)
    , m_autoReconnect(autoReconnect)
    , m_reconnectIntervalMs(reconnectIntervalMs)
    , m_rssiThreshold(DEFAULT_RSSI_THRESHOLD)
    , m_status(WiFiStatus::DISCONNECTED)
    , m_lastReconnectAttempt(0)
    , m_currentReconnectInterval(reconnectIntervalMs)
    , m_reconnectBackoffMultiplier(RECONNECT_BACKOFF_MULTIPLIER)
    , m_maxReconnectInterval(MAX_RECONNECT_INTERVAL)
    , m_connectionStartTime(0)
{
    // Initialize statistics
    memset(&m_stats, 0, sizeof(WiFiStats));
}

WiFiConnectionService::~WiFiConnectionService() {
    disconnect();
}

bool WiFiConnectionService::initialize() {
    // Set WiFi mode to station
    WiFi.mode(WIFI_STA);
    
    // Disable auto-reconnect (we'll handle it ourselves)
    WiFi.setAutoReconnect(false);
    
    // Set hostname
    WiFi.setHostname("ESP32-Gateway");
    
    Serial.println("[WiFi] Service initialized");
    return true;
}

bool WiFiConnectionService::connect(uint32_t timeoutMs) {
    if (m_status == WiFiStatus::CONNECTED) {
        Serial.println("[WiFi] Already connected");
        return true;
    }

    Serial.printf("[WiFi] Connecting to %s...\n", m_ssid);
    
    m_status = WiFiStatus::CONNECTING;
    m_stats.connectionAttempts++;
    m_connectionStartTime = millis();
    
    // Begin connection
    WiFi.begin(m_ssid, m_password);
    
    // Wait for connection with timeout
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        m_status = WiFiStatus::CONNECTED;
        m_stats.successfulConnections++;
        m_stats.lastConnectTime = millis();
        
        // Reset reconnect backoff on successful connection
        resetReconnectBackoff();
        
        // Update RSSI
        updateRSSI();
        
        Serial.printf("[WiFi] Connected! IP: %s, RSSI: %d dBm\n", 
                     WiFi.localIP().toString().c_str(), 
                     WiFi.RSSI());
        
        // Notify listeners
        notifyEvent(WiFiEvent::CONNECTED, m_stats.currentRSSI);
        
        return true;
    } else {
        m_status = WiFiStatus::FAILED;
        Serial.println("[WiFi] Connection failed!");
        
        // Notify listeners
        notifyEvent(WiFiEvent::CONNECTION_FAILED, 0);
        
        return false;
    }
}

void WiFiConnectionService::disconnect() {
    if (m_status != WiFiStatus::DISCONNECTED) {
        Serial.println("[WiFi] Disconnecting...");
        
        WiFi.disconnect(true);
        m_status = WiFiStatus::DISCONNECTED;
        m_stats.lastDisconnectTime = millis();
        
        // Update uptime
        if (m_stats.lastConnectTime > 0) {
            m_stats.uptimeSeconds += (millis() - m_stats.lastConnectTime) / 1000;
        }
        
        // Notify listeners
        notifyEvent(WiFiEvent::DISCONNECTED, 0);
    }
}

bool WiFiConnectionService::isConnected() const {
    return (m_status == WiFiStatus::CONNECTED) && (WiFi.status() == WL_CONNECTED);
}

WiFiConnectionService::WiFiStatus WiFiConnectionService::getStatus() const {
    return m_status;
}

void WiFiConnectionService::update() {
    // Check if we're connected but WiFi says otherwise
    if (m_status == WiFiStatus::CONNECTED && WiFi.status() != WL_CONNECTED) {
        handleDisconnection();
    }
    
    // Handle auto-reconnect
    if (m_autoReconnect && 
        (m_status == WiFiStatus::DISCONNECTED || m_status == WiFiStatus::FAILED)) {
        attemptReconnect();
    }
    
    // Update RSSI periodically when connected
    if (m_status == WiFiStatus::CONNECTED) {
        static uint32_t lastRSSIUpdate = 0;
        if (millis() - lastRSSIUpdate >= RSSI_UPDATE_INTERVAL) {
            updateRSSI();
            lastRSSIUpdate = millis();
            
            // Check RSSI threshold
            if (m_stats.currentRSSI < m_rssiThreshold) {
                notifyEvent(WiFiEvent::RSSI_LOW, m_stats.currentRSSI);
            }
        }
    }
}

void WiFiConnectionService::onEvent(EventCallback callback) {
    m_callbacks.push_back(callback);
}

int8_t WiFiConnectionService::getRSSI() const {
    return m_stats.currentRSSI;
}

WiFiConnectionService::WiFiStats WiFiConnectionService::getStats() const {
    // Update uptime if currently connected
    WiFiStats stats = m_stats;
    if (m_status == WiFiStatus::CONNECTED && m_stats.lastConnectTime > 0) {
        stats.uptimeSeconds = m_stats.uptimeSeconds + 
                             ((millis() - m_stats.lastConnectTime) / 1000);
    }
    return stats;
}

void WiFiConnectionService::setAutoReconnect(bool enabled) {
    m_autoReconnect = enabled;
    Serial.printf("[WiFi] Auto-reconnect %s\n", enabled ? "enabled" : "disabled");
}

bool WiFiConnectionService::getAutoReconnect() const {
    return m_autoReconnect;
}

void WiFiConnectionService::setRSSIThreshold(int8_t threshold) {
    m_rssiThreshold = threshold;
    Serial.printf("[WiFi] RSSI threshold set to %d dBm\n", threshold);
}

String WiFiConnectionService::getLocalIP() const {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "";
}

String WiFiConnectionService::getMACAddress() const {
    return WiFi.macAddress();
}

void WiFiConnectionService::resetStats() {
    memset(&m_stats, 0, sizeof(WiFiStats));
    Serial.println("[WiFi] Statistics reset");
}

// Private methods

void WiFiConnectionService::handleDisconnection() {
    Serial.println("[WiFi] Connection lost!");
    
    m_status = WiFiStatus::DISCONNECTED;
    m_stats.disconnections++;
    m_stats.lastDisconnectTime = millis();
    
    // Update uptime
    if (m_stats.lastConnectTime > 0) {
        m_stats.uptimeSeconds += (millis() - m_stats.lastConnectTime) / 1000;
    }
    
    // Notify listeners
    notifyEvent(WiFiEvent::DISCONNECTED, 0);
}

void WiFiConnectionService::attemptReconnect() {
    uint32_t now = millis();
    
    // Check if it's time to attempt reconnection
    if (now - m_lastReconnectAttempt >= m_currentReconnectInterval) {
        Serial.printf("[WiFi] Attempting reconnection (interval: %lu ms)...\n", 
                     m_currentReconnectInterval);
        
        m_status = WiFiStatus::RECONNECTING;
        m_stats.reconnectAttempts++;
        m_lastReconnectAttempt = now;
        
        // Notify listeners
        notifyEvent(WiFiEvent::RECONNECTING, 0);
        
        // Attempt connection
        if (connect(10000)) {
            // Connection successful
            Serial.println("[WiFi] Reconnection successful!");
        } else {
            // Connection failed, increase backoff
            increaseReconnectBackoff();
        }
    }
}

void WiFiConnectionService::updateRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        int8_t currentRSSI = WiFi.RSSI();
        
        // Update current RSSI
        m_stats.currentRSSI = currentRSSI;
        
        // Calculate running average (simple moving average)
        if (m_stats.averageRSSI == 0) {
            m_stats.averageRSSI = currentRSSI;
        } else {
            // Exponential moving average (alpha = 0.2)
            m_stats.averageRSSI = (int8_t)((0.8f * m_stats.averageRSSI) + (0.2f * currentRSSI));
        }
    }
}

void WiFiConnectionService::notifyEvent(WiFiEvent event, int8_t rssi) {
    for (auto& callback : m_callbacks) {
        callback(event, rssi);
    }
}

void WiFiConnectionService::resetReconnectBackoff() {
    m_currentReconnectInterval = m_reconnectIntervalMs;
}

void WiFiConnectionService::increaseReconnectBackoff() {
    m_currentReconnectInterval *= m_reconnectBackoffMultiplier;
    if (m_currentReconnectInterval > m_maxReconnectInterval) {
        m_currentReconnectInterval = m_maxReconnectInterval;
    }
    Serial.printf("[WiFi] Reconnect backoff increased to %lu ms\n", 
                 m_currentReconnectInterval);
}
