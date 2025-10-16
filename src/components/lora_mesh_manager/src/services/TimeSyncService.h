#ifndef _LORAMESHER_TIME_SYNC_SERVICE_H
#define _LORAMESHER_TIME_SYNC_SERVICE_H

#include <Arduino.h>
#include <time.h>

/**
 * @brief Time Synchronization Service
 * 
 * Provides time synchronization between Gateway (with NTP access) and Nodes (offline).
 * Gateway periodically broadcasts current Unix timestamp to all nodes in the mesh.
 * Nodes maintain local RTC using received timestamps.
 */
class TimeSyncService {
public:
    /**
     * @brief Time sync packet structure (8 bytes)
     */
    struct TimeSyncPacket {
        uint32_t timestamp;      // Unix timestamp (seconds since 1970-01-01)
        uint16_t milliseconds;   // Millisecond component (0-999)
        uint8_t  flags;          // Bit 0: NTP synced, Bit 1-7: Reserved
        uint8_t  reserved;       // Reserved for future use
    };

    /**
     * @brief Initialize time sync service
     * @param isGateway True if this is a gateway (will sync with NTP)
     * @return true if initialization successful
     */
    static bool initialize(bool isGateway);

    /**
     * @brief Update time from NTP server (Gateway only)
     * @param ntpServer NTP server hostname (default: "pool.ntp.org")
     * @param gmtOffsetSec GMT offset in seconds (default: 0 for UTC)
     * @param daylightOffsetSec Daylight saving offset (default: 0)
     * @return true if NTP sync successful
     */
    static bool syncWithNTP(const char* ntpServer = "pool.ntp.org", 
                            long gmtOffsetSec = 0, 
                            int daylightOffsetSec = 0);

    /**
     * @brief Process received time sync packet (Node side)
     * @param packet Time sync packet from gateway
     */
    static void processTimeSyncPacket(const TimeSyncPacket& packet);

    /**
     * @brief Create time sync packet for broadcasting (Gateway side)
     * @param packet Output packet to populate
     */
    static void createTimeSyncPacket(TimeSyncPacket& packet);

    /**
     * @brief Get current Unix timestamp
     * @return Unix timestamp in seconds (0 if not synchronized)
     */
    static uint32_t getCurrentTimestamp();

    /**
     * @brief Get current time with millisecond precision
     * @param timestamp Output: Unix timestamp in seconds
     * @param milliseconds Output: Millisecond component (0-999)
     */
    static void getCurrentTime(uint32_t& timestamp, uint16_t& milliseconds);

    /**
     * @brief Check if time is synchronized
     * @return true if RTC has valid time
     */
    static bool isTimeSynced();

    /**
     * @brief Get time since last sync (seconds)
     * @return Seconds since last successful time sync (0 if never synced)
     */
    static uint32_t getTimeSinceLastSync();

    /**
     * @brief Set manual timestamp (for testing or manual sync)
     * @param timestamp Unix timestamp in seconds
     */
    static void setManualTimestamp(uint32_t timestamp);

private:
    static bool m_isGateway;
    static bool m_timeSynced;
    static uint32_t m_lastSyncTimestamp;   // Unix timestamp of last sync
    static uint32_t m_lastSyncMillis;      // millis() at last sync
    static uint32_t m_bootOffset;          // Offset to convert millis() to Unix time
    static bool m_ntpSynced;               // True if synced via NTP
};

#endif // _LORAMESHER_TIME_SYNC_SERVICE_H
