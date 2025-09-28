#ifndef _MESH_TYPES_H
#define _MESH_TYPES_H

#include <stdint.h>

// Forward declarations for mesh types
template<typename T> class AppPacket;
template<typename T> class Packet;
class DataPacket;
class RouteNode;
class NetworkNode;

// Mesh network types
typedef uint16_t mesh_addr_t;
typedef uint32_t mesh_time_t;
typedef uint8_t  mesh_seq_t;

// Packet types
enum mesh_packet_type_t {
    HELLO_P = 0x01,
    DATA_P = 0x02,
    ROUTE_P = 0x03,
    CONTROL_P = 0x04,
    RREQ_P = 0x05,    // Route Request (on-demand discovery)
    RREP_P = 0x06,    // Route Reply
    RERR_P = 0x07     // Route Error
};

// Priority levels
enum mesh_priority_t {
    LOW_PRIORITY = 0,
    DEFAULT_PRIORITY = 1,
    HIGH_PRIORITY = 2,
    URGENT_PRIORITY = 3
};

// Node roles
enum mesh_role_t {
    NODE_ROLE = 0,
    GATEWAY_ROLE = 1,
    BRIDGE_ROLE = 2
};

// Status codes
enum mesh_status_t {
    MESH_OK = 0,
    MESH_ERROR = -1,
    MESH_TIMEOUT = -2,
    MESH_NO_ROUTE = -3,
    MESH_BUFFER_FULL = -4
};

#endif // _MESH_TYPES_H