#ifndef _LORAMESHER_ROUTE_REPLY_PACKET_H
#define _LORAMESHER_ROUTE_REPLY_PACKET_H

#include "PacketHeader.h"

#pragma pack(1)
class RouteReplyPacket final: public PacketHeader {
public:
    /**
     * @brief Request ID - matches the RREQ this is replying to
     */
    uint16_t requestId = 0;

    /**
     * @brief Target address (destination of original request)
     */
    uint16_t targetAddress = 0;

    /**
     * @brief Originator address who initiated the request
     */
    uint16_t originatorAddress = 0;

    /**
     * @brief Total hop count to target
     */
    uint8_t hopCount = 0;

    /**
     * @brief Lifetime of this route in seconds
     */
    uint16_t routeLifetime = 300;

    /**
     * @brief Timestamp when reply was created
     */
    uint32_t timestamp = 0;
};

#pragma pack()

#endif