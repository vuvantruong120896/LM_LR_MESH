/**
 ******************************************************************************
 * @file    main.c
 * @author  LM Co.,LTD.
 *          truongvv.lm@gmail.com
 * @date    April 30, 2025
 ******************************************************************************/

// Mode Configuration:
#ifndef DEVICE_MODE
#define DEVICE_MODE 1 // Default to Node mode
#endif

#include <Arduino.h>
#include "components/lora_mesh_manager/include/LoraMesher.h"

// Global radio reference for common utilities
LoraMesher &radio = LoraMesher::getInstance();

#if DEVICE_MODE == 2
// Bridge mode
#include "application/app_bridge/bridge_app.h"
static BridgeApp app;
#define LM_TAG "BridgeMAIN"
#else
// Node mode (default)
#include "application/app_node/node_app.h"
static NodeApp app;
#define LM_TAG "NodeMAIN"
#endif

// ===== MAIN ARDUINO FUNCTIONS =====

void setup()
{
    Serial.begin(115200);
    delay(2000);

#if DEVICE_MODE == 2
    ESP_LOGI(LM_TAG, "Starting in BRIDGE MODE");
#else
    ESP_LOGI(LM_TAG, "Starting in NODE MODE");
#endif

    app.setup();
}

void loop()
{
    app.loop();
}