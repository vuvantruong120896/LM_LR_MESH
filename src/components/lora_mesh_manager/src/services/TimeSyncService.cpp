#include "TimeSyncService.h"
#include <esp_log.h>
#include <sys/time.h>

static const char* TAG = "TimeSync";

// Static member initialization
bool TimeSyncService::m_isGateway = false;
bool TimeSyncService::m_timeSynced = false;
uint32_t TimeSyncService::m_lastSyncTimestamp = 0;
uint32_t TimeSyncService::m_lastSyncMillis = 0;
uint32_t TimeSyncService::m_bootOffset = 0;
bool TimeSyncService::m_ntpSynced = false;

bool TimeSyncService::initialize(bool isGateway) {
    m_isGateway = isGateway;
    m_timeSynced = false;
    m_lastSyncTimestamp = 0;
    m_lastSyncMillis = millis();
    m_bootOffset = 0;
    m_ntpSynced = false;

    ESP_LOGI(TAG, "Time Sync Service initialized (Mode: %s)", 
             isGateway ? "Gateway" : "Node");
    
    return true;
}

bool TimeSyncService::syncWithNTP(const char* ntpServer, long gmtOffsetSec, int daylightOffsetSec) {
    if (!m_isGateway) {
        ESP_LOGW(TAG, "NTP sync only available on Gateway");
        return false;
    }

    ESP_LOGI(TAG, "Syncing time with NTP server: %s", ntpServer);
    
    // Configure NTP
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    
    // Wait for time to be set (max 10 seconds)
    int retry = 0;
    const int retry_count = 50; // 50 * 200ms = 10 seconds
    struct tm timeinfo;
    
    while (!getLocalTime(&timeinfo) && retry < retry_count) {
        delay(200);
        retry++;
    }
    
    if (retry >= retry_count) {
        ESP_LOGE(TAG, "Failed to sync with NTP server");
        return false;
    }
    
    // Get current timestamp
    time_t now;
    time(&now);
    
    m_lastSyncTimestamp = (uint32_t)now;
    m_lastSyncMillis = millis();
    m_bootOffset = m_lastSyncTimestamp - (m_lastSyncMillis / 1000);
    m_timeSynced = true;
    m_ntpSynced = true;
    
    ESP_LOGI(TAG, "✅ NTP sync successful! Current time: %s", asctime(&timeinfo));
    ESP_LOGI(TAG, "Unix timestamp: %u, Boot offset: %u", m_lastSyncTimestamp, m_bootOffset);
    
    return true;
}

void TimeSyncService::processTimeSyncPacket(const TimeSyncPacket& packet) {
    if (m_isGateway) {
        ESP_LOGD(TAG, "Gateway ignoring time sync packet (already has NTP)");
        return;
    }
    
    // Extract timestamp from packet
    uint32_t receivedTimestamp = packet.timestamp;
    uint16_t receivedMillis = packet.milliseconds;
    bool ntpSynced = (packet.flags & 0x01) != 0;
    
    // Calculate boot offset
    uint32_t currentMillis = millis();
    m_bootOffset = receivedTimestamp - (currentMillis / 1000);
    
    m_lastSyncTimestamp = receivedTimestamp;
    m_lastSyncMillis = currentMillis;
    m_timeSynced = true;
    m_ntpSynced = ntpSynced;
    
    ESP_LOGI(TAG, "⏰ Time synced from Gateway: %u.%03u (NTP: %s)", 
             receivedTimestamp, receivedMillis, ntpSynced ? "Yes" : "No");
    ESP_LOGI(TAG, "Boot offset: %u, Time since boot: %u seconds", 
             m_bootOffset, currentMillis / 1000);
}

void TimeSyncService::createTimeSyncPacket(TimeSyncPacket& packet) {
    uint32_t timestamp;
    uint16_t milliseconds;
    
    getCurrentTime(timestamp, milliseconds);
    
    packet.timestamp = timestamp;
    packet.milliseconds = milliseconds;
    packet.flags = m_ntpSynced ? 0x01 : 0x00;
    packet.reserved = 0;
    
    ESP_LOGD(TAG, "Created time sync packet: %u.%03u (NTP: %s)", 
             timestamp, milliseconds, m_ntpSynced ? "Yes" : "No");
}

uint32_t TimeSyncService::getCurrentTimestamp() {
    if (!m_timeSynced) {
        return 0; // Not synchronized
    }
    
    // Calculate current timestamp based on boot offset
    uint32_t currentMillis = millis();
    uint32_t elapsedSeconds = (currentMillis - m_lastSyncMillis) / 1000;
    
    return m_lastSyncTimestamp + elapsedSeconds;
}

void TimeSyncService::getCurrentTime(uint32_t& timestamp, uint16_t& milliseconds) {
    if (!m_timeSynced) {
        timestamp = 0;
        milliseconds = 0;
        return;
    }
    
    uint32_t currentMillis = millis();
    uint32_t elapsedMs = currentMillis - m_lastSyncMillis;
    
    timestamp = m_lastSyncTimestamp + (elapsedMs / 1000);
    milliseconds = (uint16_t)(elapsedMs % 1000);
}

bool TimeSyncService::isTimeSynced() {
    return m_timeSynced;
}

uint32_t TimeSyncService::getTimeSinceLastSync() {
    if (!m_timeSynced) {
        return 0;
    }
    
    return (millis() - m_lastSyncMillis) / 1000;
}

void TimeSyncService::setManualTimestamp(uint32_t timestamp, bool fromNetwork) {
    m_lastSyncTimestamp = timestamp;
    m_lastSyncMillis = millis();
    m_bootOffset = timestamp - (m_lastSyncMillis / 1000);
    m_timeSynced = true;
    m_ntpSynced = fromNetwork;
    
    ESP_LOGI(TAG, "Manual timestamp set: %u (Boot offset: %u, source=%s)", 
             timestamp, m_bootOffset, fromNetwork ? "network" : "manual");
}
