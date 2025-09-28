#ifndef _LORAMESHER_ROUTE_REQUEST_PACKET_H
#define _LORAMESHER_ROUTE_REQUEST_PACKET_H

#include "PacketHeader.h"

#pragma pack(1)
class RouteRequestPacket final: public PacketHeader {
public:
    /**
     * @brief Request ID - unique identifier for this route request
     */
    uint16_t requestId = 0;

    /**
     * @brief Target address to find route to
     */
    uint16_t targetAddress = 0;

    /**
     * @brief Originator address who initiated the request
     */
    uint16_t originatorAddress = 0;

    /**
     * @brief Hop count from originator
     */
    uint8_t hopCount = 0;

    /**
     * @brief TTL (Time To Live) - maximum hops allowed
     */
    uint8_t ttl = 10;

    /**
     * @brief Timestamp when request was created
     */
    uint32_t timestamp = 0;
};

#pragma pack()

#endif