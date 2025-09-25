#ifndef NODE_APP_H
#define NODE_APP_H

#include <Arduino.h>
#include "node_config.h"
#include "led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"

class NodeApp {
public:
    NodeApp();
    ~NodeApp();
    
    void setup();
    void loop();

private:
    LoraMesher& radio;
    uint32_t dataCounter;
    dataPacket* nodePacket;
    
    // Private methods
    sensorData simulateSensorData();
    void setupLoRaMesher();
};

#endif // NODE_APP_H