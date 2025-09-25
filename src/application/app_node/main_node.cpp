#include <Arduino.h>
#include "node_config.h"
#include "../common/led_control.h"
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"

#define LM_TAG "NodeMAIN"

LoraMesher& radio = LoraMesher::getInstance();

uint32_t dataCounter = 0;
dataPacket* nodePacket = new dataPacket;

// Simulate sensor readings
sensorData simulateSensorData() {
    sensorData data;
    data.temperature = 20.0 + (random(0, 200) / 10.0); // 20-40°C
    data.humidity = 40.0 + (random(0, 600) / 10.0);    // 40-100%
    data.battery = 3.2 + (random(0, 80) / 100.0);      // 3.2-4.0V
    data.timestamp = millis();
    return data;
}

void setupNode() {
    ESP_LOGI(LM_TAG, "=== LoRaMesh Node Application ===");
    ESP_LOGI(LM_TAG, "Node ID: 0x%X", NODE_ID);
    
    // Initialize hardware
    led_init();
    led_pattern_startup();
    
    // Configure LoRaMesher
    LoraMesher::LoraMesherConfig config;
    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;
    config.module = LORA_MODULE;
    
    setupLoRaMesher(config);
    
    // Initialize packet data
    nodePacket->nodeId = NODE_ID;

    ESP_LOGI(LM_TAG, "Node setup complete");
}

void loop() {
    ESP_LOGI(LM_TAG, "=== Node Cycle %d ===", dataCounter);
    
    // Update packet data
    nodePacket->counter = dataCounter++;
    nodePacket->timestamp = millis();
    
    // Send mesh packet
    ESP_LOGI(LM_TAG, "Sending packet %d from node 0x%X", 
                  nodePacket->counter, nodePacket->nodeId);
    radio.createPacketAndSend(BROADCAST_ADDR, nodePacket, 1);
    
    // Optional: Send sensor data simulation
    #if ENABLE_SENSOR_SIMULATION
    if (dataCounter % 5 == 0) {  // Every 5th packet
        sensorData sensor = simulateSensorData();
        ESP_LOGI(LM_TAG, "Simulated sensor data:");
        printSensorData(sensor);
        
        // Could send sensor data to specific gateway node
        // radio.createPacketAndSend(GATEWAY_ADDR, &sensor, 1);
    }
    #endif
    
    // Network status
    ESP_LOGI(LM_TAG, "Routing table size: %d", radio.routingTableSize());
    ESP_LOGI(LM_TAG, "Send queue size: %d", radio.getSendQueueSize());
    ESP_LOGI(LM_TAG, "Stats - Sent: %d, Received: %d", 
             radio.getSendPacketsNum(), radio.getReceivedDataPacketsNum());

    ESP_LOGI(LM_TAG, "Next packet in %d seconds", SEND_INTERVAL_MS / 1000);
    
    // Wait for next cycle
    vTaskDelay(SEND_INTERVAL_MS / portTICK_PERIOD_MS);
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial
    
    setupNode();
}