#ifndef _MESH_CONFIG_H
#define _MESH_CONFIG_H

// Include build options for configuration constants
#include "../src/core/BuildOptions.h"

// Default mesh configuration values
#define MESH_DEFAULT_FREQ       868.0f  // MHz
#define MESH_DEFAULT_BW         125.0f  // kHz  
#define MESH_DEFAULT_SF         7       // Spreading Factor
#define MESH_DEFAULT_CR         5       // Coding Rate
#define MESH_DEFAULT_POWER      14      // dBm
#define MESH_DEFAULT_PREAMBLE   8       // symbols

// Component paths for internal includes
#define MESH_CORE_PATH          "src/components/lora_mesh_manager/src/core/"
#define MESH_RADIO_PATH         "src/components/lora_mesh_manager/src/radio/"
#define MESH_NETWORK_PATH       "src/components/lora_mesh_manager/src/network/"
#define MESH_SERVICES_PATH      "src/components/lora_mesh_manager/src/services/"
#define MESH_UTILITIES_PATH     "src/components/lora_mesh_manager/src/utilities/"

#endif // _MESH_CONFIG_H