#include "RouteDiscoveryService.h"
#include "RoutingTableService.h"
#include "PacketService.h"
#include "WiFiService.h"
#include "../core/LoraMesher.h"
#include <esp_log.h>

// Static member definitions
std::map<uint16_t, RouteRequestEntry> RouteDiscoveryService::activeRequests;
std::set<std::pair<uint16_t, uint16_t>> RouteDiscoveryService::seenRequests;
uint16_t RouteDiscoveryService::nextRequestId = 1;
uint32_t RouteDiscoveryService::requestsSent = 0;
uint32_t RouteDiscoveryService::requestsReceived = 0;
uint32_t RouteDiscoveryService::repliesSent = 0;
uint32_t RouteDiscoveryService::repliesReceived = 0;
uint32_t RouteDiscoveryService::routesDiscovered = 0;

void RouteDiscoveryService::initialize() {
    activeRequests.clear();
    seenRequests.clear();
    nextRequestId = 1;
    requestsSent = requestsReceived = repliesSent = repliesReceived = routesDiscovered = 0;
    
    ESP_LOGI(ROUTE_DISCOVERY_TAG, "Route Discovery Service initialized");
}

bool RouteDiscoveryService::initiateRouteDiscovery(uint16_t targetAddress) {
    // Check if already discovering route to this target
    for (const auto& entry : activeRequests) {
        if (entry.second.targetAddress == targetAddress && entry.second.active) {
            ESP_LOGD(ROUTE_DISCOVERY_TAG, "Route discovery already active for 0x%04X", targetAddress);
            return false;
        }
    }

    // Clean up expired requests first
    cleanupExpiredRequests();

    // Check if we have too many active requests
    if (activeRequests.size() >= MAX_ROUTE_REQUESTS) {
        ESP_LOGW(ROUTE_DISCOVERY_TAG, "Too many active route requests, dropping oldest");
        // Remove oldest request
        auto oldestIt = activeRequests.begin();
        uint32_t oldestTime = oldestIt->second.timestamp;
        for (auto it = activeRequests.begin(); it != activeRequests.end(); ++it) {
            if (it->second.timestamp < oldestTime) {
                oldestTime = it->second.timestamp;
                oldestIt = it;
            }
        }
        activeRequests.erase(oldestIt);
    }

    // Generate new request
    uint16_t requestId = generateRequestId();
    uint16_t localAddress = WiFiService::getLocalAddress();
    
    // Store request in active list
    activeRequests[requestId] = RouteRequestEntry(requestId, targetAddress, localAddress, millis());
    
    // Send route request
    bool success = sendRouteRequest(targetAddress, requestId, localAddress, 0, 10);
    
    if (success) {
        requestsSent++;
        ESP_LOGI(ROUTE_DISCOVERY_TAG, "Initiated route discovery for 0x%04X (RequestID: %u)", 
                 targetAddress, requestId);
    } else {
        activeRequests.erase(requestId);
        ESP_LOGE(ROUTE_DISCOVERY_TAG, "Failed to send route request for 0x%04X", targetAddress);
    }
    
    return success;
}

bool RouteDiscoveryService::processRouteRequest(RouteRequestPacket* rreqPacket, uint16_t receivedFrom) {
    if (!rreqPacket) return false;
    
    requestsReceived++;
    
    uint16_t localAddress = WiFiService::getLocalAddress();
    
    ESP_LOGD(ROUTE_DISCOVERY_TAG, "Processing RREQ from 0x%04X for target 0x%04X (ReqID: %u, hops: %u)",
             receivedFrom, rreqPacket->targetAddress, rreqPacket->requestId, rreqPacket->hopCount);

    // Check if we've seen this request before (loop prevention)
    if (isRequestSeen(rreqPacket->originatorAddress, rreqPacket->requestId)) {
        ESP_LOGD(ROUTE_DISCOVERY_TAG, "Already processed RREQ %u from 0x%04X, dropping", 
                 rreqPacket->requestId, rreqPacket->originatorAddress);
        return true; // Processed (dropped)
    }
    
    // Mark as seen
    markRequestSeen(rreqPacket->originatorAddress, rreqPacket->requestId);
    
    // Update route to originator (reverse route setup)
    if (rreqPacket->originatorAddress != localAddress) {
        // Add/update route to originator through receivedFrom
        NetworkNode originatorNode(rreqPacket->originatorAddress, rreqPacket->hopCount + 1, 0);
        RoutingTableService::processRoute(receivedFrom, &originatorNode);
        ESP_LOGD(ROUTE_DISCOVERY_TAG, "Updated reverse route to originator 0x%04X via 0x%04X", 
                 rreqPacket->originatorAddress, receivedFrom);
    }
    
    // Check if we are the target
    if (rreqPacket->targetAddress == localAddress) {
        ESP_LOGI(ROUTE_DISCOVERY_TAG, "We are target for RREQ %u, sending reply", rreqPacket->requestId);
        
        // Send route reply back to originator
        bool success = sendRouteReply(rreqPacket->requestId, rreqPacket->targetAddress,
                                    rreqPacket->originatorAddress, rreqPacket->hopCount + 1);
        if (success) {
            repliesSent++;
        }
        return true;
    }
    
    // Check if we have a route to target
    if (RoutingTableService::hasAddressRoutingTable(rreqPacket->targetAddress)) {
        ESP_LOGI(ROUTE_DISCOVERY_TAG, "Have route to target 0x%04X, sending reply", rreqPacket->targetAddress);
        
        uint8_t hopsToTarget = RoutingTableService::getNumberOfHops(rreqPacket->targetAddress);
        bool success = sendRouteReply(rreqPacket->requestId, rreqPacket->targetAddress,
                                    rreqPacket->originatorAddress, rreqPacket->hopCount + 1 + hopsToTarget);
        if (success) {
            repliesSent++;
        }
        return true;
    }
    
    // Check TTL
    if (rreqPacket->ttl <= 1) {
        ESP_LOGD(ROUTE_DISCOVERY_TAG, "RREQ TTL expired, not forwarding");
        return true;
    }
    
    // Forward the request
    ESP_LOGD(ROUTE_DISCOVERY_TAG, "Forwarding RREQ for target 0x%04X", rreqPacket->targetAddress);
    return sendRouteRequest(rreqPacket->targetAddress, rreqPacket->requestId,
                           rreqPacket->originatorAddress, rreqPacket->hopCount + 1, 
                           rreqPacket->ttl - 1);
}

bool RouteDiscoveryService::processRouteReply(RouteReplyPacket* rrepPacket, uint16_t receivedFrom) {
    if (!rrepPacket) return false;
    
    repliesReceived++;
    
    uint16_t localAddress = WiFiService::getLocalAddress();
    
    ESP_LOGI(ROUTE_DISCOVERY_TAG, "Processing RREP from 0x%04X for target 0x%04X (ReqID: %u, hops: %u)",
             receivedFrom, rrepPacket->targetAddress, rrepPacket->requestId, rrepPacket->hopCount);
    
    // Add/update route to target through receivedFrom
    NetworkNode targetNode(rrepPacket->targetAddress, rrepPacket->hopCount, 0);
    RoutingTableService::processRoute(receivedFrom, &targetNode);
    
    ESP_LOGI(ROUTE_DISCOVERY_TAG, "Updated route to target 0x%04X via 0x%04X (%u hops)",
             rrepPacket->targetAddress, receivedFrom, rrepPacket->hopCount);
    
    // Check if this reply is for us
    if (rrepPacket->originatorAddress == localAddress) {
        ESP_LOGI(ROUTE_DISCOVERY_TAG, "Route reply for our request %u received, route established",
                 rrepPacket->requestId);
        
        // Remove from active requests
        auto it = activeRequests.find(rrepPacket->requestId);
        if (it != activeRequests.end()) {
            activeRequests.erase(it);
            routesDiscovered++;
        }
        
        return true;
    }
    
    // Forward to originator if we have route
    if (RoutingTableService::hasAddressRoutingTable(rrepPacket->originatorAddress)) {
        ESP_LOGD(ROUTE_DISCOVERY_TAG, "Forwarding RREP to originator 0x%04X", rrepPacket->originatorAddress);
        
        // Create reply packet and send towards originator
        RouteReplyPacket* forwardPacket = PacketFactory::createPacket<RouteReplyPacket>(nullptr, 0);
        if (forwardPacket) {
            *forwardPacket = *rrepPacket; // Copy original reply
            forwardPacket->src = localAddress;
            forwardPacket->dst = RoutingTableService::getNextHop(rrepPacket->originatorAddress);
            
            int result = LoraMesher::getInstance().sendPacket((uint8_t*)forwardPacket, sizeof(RouteReplyPacket));
            delete forwardPacket;
            ESP_LOGD(ROUTE_DISCOVERY_TAG, "Forward RREP result: %d", result);
        }
    } else {
        ESP_LOGW(ROUTE_DISCOVERY_TAG, "No route to originator 0x%04X, cannot forward RREP",
                 rrepPacket->originatorAddress);
    }
    
    return true;
}

bool RouteDiscoveryService::isDiscoveryActive(uint16_t targetAddress) {
    cleanupExpiredRequests();
    
    for (const auto& entry : activeRequests) {
        if (entry.second.targetAddress == targetAddress && entry.second.active) {
            return true;
        }
    }
    return false;
}

void RouteDiscoveryService::cleanupExpiredRequests() {
    uint32_t now = millis();
    auto it = activeRequests.begin();
    
    while (it != activeRequests.end()) {
        if (now - it->second.timestamp > ROUTE_REQUEST_TIMEOUT) {
            ESP_LOGD(ROUTE_DISCOVERY_TAG, "Cleaning up expired route request %u for target 0x%04X",
                     it->second.requestId, it->second.targetAddress);
            it = activeRequests.erase(it);
        } else {
            ++it;
        }
    }
    
    // Also cleanup seen requests (keep only recent ones)
    // This is simplified - in production would use time-based cleanup
    if (seenRequests.size() > 100) {
        seenRequests.clear();
        ESP_LOGD(ROUTE_DISCOVERY_TAG, "Cleared seen requests cache");
    }
}

void RouteDiscoveryService::getStatistics(uint32_t& reqSent, uint32_t& reqRcv, 
                                        uint32_t& repSent, uint32_t& repRcv, uint32_t& routes) {
    reqSent = requestsSent;
    reqRcv = requestsReceived;
    repSent = repliesSent;
    repRcv = repliesReceived;
    routes = routesDiscovered;
}

void RouteDiscoveryService::reset() {
    initialize();
}

uint16_t RouteDiscoveryService::generateRequestId() {
    uint16_t id = nextRequestId++;
    if (nextRequestId == 0) nextRequestId = 1; // Avoid 0
    return id;
}

bool RouteDiscoveryService::isRequestSeen(uint16_t originator, uint16_t requestId) {
    return seenRequests.find(std::make_pair(originator, requestId)) != seenRequests.end();
}

void RouteDiscoveryService::markRequestSeen(uint16_t originator, uint16_t requestId) {
    seenRequests.insert(std::make_pair(originator, requestId));
}

bool RouteDiscoveryService::sendRouteRequest(uint16_t targetAddress, uint16_t requestId, 
                                           uint16_t originator, uint8_t hopCount, uint8_t ttl) {
    RouteRequestPacket* rreqPacket = PacketFactory::createPacket<RouteRequestPacket>(nullptr, 0);
    if (!rreqPacket) {
        ESP_LOGE(ROUTE_DISCOVERY_TAG, "Failed to create RREQ packet");
        return false;
    }
    
    // Fill packet
    rreqPacket->src = WiFiService::getLocalAddress();
    rreqPacket->dst = BROADCAST_ADDR;
    rreqPacket->type = RREQ_P;
    rreqPacket->requestId = requestId;
    rreqPacket->targetAddress = targetAddress;
    rreqPacket->originatorAddress = originator;
    rreqPacket->hopCount = hopCount;
    rreqPacket->ttl = ttl;
    rreqPacket->timestamp = millis();
    rreqPacket->packetSize = sizeof(RouteRequestPacket);
    
    // Send with high priority
    int result = LoraMesher::getInstance().sendPacket((uint8_t*)rreqPacket, sizeof(RouteRequestPacket));
    bool success = (result == 0);
    delete rreqPacket;
    
    ESP_LOGD(ROUTE_DISCOVERY_TAG, "Sent RREQ for target 0x%04X (ReqID: %u, hops: %u, TTL: %u)",
             targetAddress, requestId, hopCount, ttl);
    
    return success;
}

bool RouteDiscoveryService::sendRouteReply(uint16_t requestId, uint16_t targetAddress,
                                         uint16_t originatorAddress, uint8_t hopCount) {
    // Check if we have route to originator
    if (!RoutingTableService::hasAddressRoutingTable(originatorAddress)) {
        ESP_LOGW(ROUTE_DISCOVERY_TAG, "No route to originator 0x%04X, cannot send RREP", originatorAddress);
        return false;
    }
    
    RouteReplyPacket* rrepPacket = PacketFactory::createPacket<RouteReplyPacket>(nullptr, 0);
    if (!rrepPacket) {
        ESP_LOGE(ROUTE_DISCOVERY_TAG, "Failed to create RREP packet");
        return false;
    }
    
    // Fill packet
    rrepPacket->src = WiFiService::getLocalAddress();
    rrepPacket->dst = RoutingTableService::getNextHop(originatorAddress);
    rrepPacket->type = RREP_P;
    rrepPacket->requestId = requestId;
    rrepPacket->targetAddress = targetAddress;
    rrepPacket->originatorAddress = originatorAddress;
    rrepPacket->hopCount = hopCount;
    rrepPacket->routeLifetime = 300; // 5 minutes
    rrepPacket->timestamp = millis();
    rrepPacket->packetSize = sizeof(RouteReplyPacket);
    
    // Send with high priority
    int result = LoraMesher::getInstance().sendPacket((uint8_t*)rrepPacket, sizeof(RouteReplyPacket));
    bool success = (result == 0);
    delete rrepPacket;
    
    ESP_LOGD(ROUTE_DISCOVERY_TAG, "Sent RREP for target 0x%04X to originator 0x%04X (ReqID: %u, hops: %u)",
             targetAddress, originatorAddress, requestId, hopCount);
    
    return success;
}