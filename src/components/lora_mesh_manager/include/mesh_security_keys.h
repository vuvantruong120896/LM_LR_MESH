#ifndef _MESH_SECURITY_KEYS_H
#define _MESH_SECURITY_KEYS_H

// Centralized security keys - ALL DEVICES MUST USE THE SAME KEYS
// WARNING: Change these keys for production deployment!

// **BOOTSTRAP KEY** - Used for secure netkey distribution
// This key is pre-shared between Bridge and all Nodes for MAC validation during netkey distribution
#define MESH_BOOTSTRAP_KEY      {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6, 0x07, 0x18, \
                                 0x29, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f, 0x90}

// Master network key - used for encryption and MAC generation
#define MESH_MASTER_NETWORK_KEY {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, \
                                 0xab, 0xf7, 0x97, 0x75, 0x46, 0xcf, 0x26, 0xa8}

// Authentication token - used for network join authentication
#define MESH_MASTER_AUTH_TOKEN  {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0}

// Network ID - ensures packets only accepted from same network
#define MESH_NETWORK_ID         0x1234

// Key version - for future key rotation support
#define MESH_KEY_VERSION        1

#endif // _MESH_SECURITY_KEYS_H