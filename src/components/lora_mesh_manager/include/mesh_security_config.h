#ifndef _MESH_SECURITY_CONFIG_H
#define _MESH_SECURITY_CONFIG_H

#include "mesh_security.h"

// Include device-specific configurations
#ifdef DEVICE_MODE
    #if DEVICE_MODE == 1
        #include "../../application/app_node/node_config.h"
    #elif DEVICE_MODE == 2
        #include "../../application/app_bridge/bridge_config.h"
    #endif
#endif

// Security initialization functions
bool initializeMeshSecurity();
MeshSecurityConfig getCurrentSecurityConfig();
bool isMeshSecurityEnabled();
uint8_t getCurrentSecurityLevel();
void logSecurityStatus();

// Default fallback values if not defined in device configs
#ifndef ENABLE_MESH_SECURITY
#define ENABLE_MESH_SECURITY false
#endif

#ifndef MESH_NETWORK_KEY
#define MESH_NETWORK_KEY {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, \
                          0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}
#endif

#ifndef MESH_AUTH_TOKEN
#define MESH_AUTH_TOKEN {0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7}
#endif

// Default security levels
#ifndef NODE_SECURITY_LEVEL
#define NODE_SECURITY_LEVEL 2
#endif

#ifndef BRIDGE_SECURITY_LEVEL
#define BRIDGE_SECURITY_LEVEL 2
#endif

#endif // _MESH_SECURITY_CONFIG_H