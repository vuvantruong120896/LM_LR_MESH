#ifndef _ROUTE_DISCOVERY_SERVICE_H
#define _ROUTE_DISCOVERY_SERVICE_H

#include <stdint.h>
#include <map>
#include <set>
#include "../network/entities/packets/RouteRequestPacket.h"
#include "../network/entities/packets/RouteReplyPacket.h"
#include "../utilities/LinkedQueue.hpp"

#define ROUTE_DISCOVERY_TAG "RouteDiscovery"
#define MAX_ROUTE_REQUESTS 32
#define ROUTE_REQUEST_TIMEOUT 10000  // 10 seconds
#define RREQ_RETRY_LIMIT 3

/**
 * @brief Route request cache entry
 */
struct RouteRequestEntry {
    uint16_t requestId;
    uint16_t targetAddress;
    uint16_t originatorAddress;
    uint32_t timestamp;
    uint8_t retryCount;
    bool active;
    
    RouteRequestEntry() : requestId(0), targetAddress(0), originatorAddress(0), 
                         timestamp(0), retryCount(0), active(false) {}
    
    RouteRequestEntry(uint16_t reqId, uint16_t target, uint16_t originator, uint32_t ts)
        : requestId(reqId), targetAddress(target), originatorAddress(originator),
          timestamp(ts), retryCount(0), active(true) {}
};

/**
 * @brief Route Discovery Service - handles on-demand route discovery
 * Implements AODV-like route request/reply mechanism for efficient routing
 */
class RouteDiscoveryService {
private:
    // Route request cache to track ongoing requests
    static std::map<uint16_t, RouteRequestEntry> activeRequests;
    
    // Recently seen RREQ IDs to prevent loops
    static std::set<std::pair<uint16_t, uint16_t>> seenRequests; // <originator, requestId>
    
    // Request ID counter
    static uint16_t nextRequestId;
    
    // Statistics
    static uint32_t requestsSent;
    static uint32_t requestsReceived;
    static uint32_t repliesSent;
    static uint32_t repliesReceived;
    static uint32_t routesDiscovered;

public:
    /**
     * @brief Initialize route discovery service
     */
    static void initialize();

    /**
     * @brief Initiate route discovery for a target address
     * @param targetAddress Address to find route to
     * @return true if request was initiated, false if already in progress
     */
    static bool initiateRouteDiscovery(uint16_t targetAddress);

    /**
     * @brief Process incoming route request packet
     * @param rreqPacket Route request packet
     * @param receivedFrom Address of sender
     * @return true if packet was processed
     */
    static bool processRouteRequest(RouteRequestPacket* rreqPacket, uint16_t receivedFrom);

    /**
     * @brief Process incoming route reply packet
     * @param rrepPacket Route reply packet
     * @param receivedFrom Address of sender
     * @return true if packet was processed
     */
    static bool processRouteReply(RouteReplyPacket* rrepPacket, uint16_t receivedFrom);

    /**
     * @brief Check if route discovery is in progress for target
     * @param targetAddress Target address to check
     * @return true if discovery is active
     */
    static bool isDiscoveryActive(uint16_t targetAddress);

    /**
     * @brief Clean up expired route requests
     */
    static void cleanupExpiredRequests();

    /**
     * @brief Get discovery statistics
     */
    static void getStatistics(uint32_t& reqSent, uint32_t& reqRcv, 
                             uint32_t& repSent, uint32_t& repRcv, uint32_t& routes);

    /**
     * @brief Reset all discovery state (for testing)
     */
    static void reset();

private:
    /**
     * @brief Generate unique request ID
     */
    static uint16_t generateRequestId();

    /**
     * @brief Check if request was recently seen (loop prevention)
     */
    static bool isRequestSeen(uint16_t originator, uint16_t requestId);

    /**
     * @brief Mark request as seen
     */
    static void markRequestSeen(uint16_t originator, uint16_t requestId);

    /**
     * @brief Create and send route request packet
     */
    static bool sendRouteRequest(uint16_t targetAddress, uint16_t requestId, 
                                uint16_t originator, uint8_t hopCount, uint8_t ttl);

    /**
     * @brief Create and send route reply packet
     */
    static bool sendRouteReply(uint16_t requestId, uint16_t targetAddress,
                              uint16_t originatorAddress, uint8_t hopCount);
};

#endif // _ROUTE_DISCOVERY_SERVICE_H