#ifndef _MESH_SECURITY_CONFIG_H
#define _MESH_SECURITY_CONFIG_H

#include "mesh_security.h"
#include "mesh_security_keys.h"  // Include centralized keys

// Include device-specific configurations
#ifdef DEVICE_MODE
    #if DEVICE_MODE == 1
        #include "../../application/app_node/node_config.h"
    #elif DEVICE_MODE == 2
        #include "../../application/app_bridge/bridge_config.h"
    #elif DEVICE_MODE == 3
         #include "../../application/app_node/node_config.h"
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

// Use centralized keys instead of local definitions
#ifndef MESH_NETWORK_KEY
#define MESH_NETWORK_KEY MESH_MASTER_NETWORK_KEY
#endif

#ifndef MESH_AUTH_TOKEN
#define MESH_AUTH_TOKEN MESH_MASTER_AUTH_TOKEN
#endif

// Default security levels
#ifndef NODE_SECURITY_LEVEL
#define NODE_SECURITY_LEVEL 2
#endif

#ifndef BRIDGE_SECURITY_LEVEL
#define BRIDGE_SECURITY_LEVEL 2
#endif

#endif // _MESH_SECURITY_CONFIG_H