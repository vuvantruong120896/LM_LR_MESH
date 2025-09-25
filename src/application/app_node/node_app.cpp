#include "node_app.h"

#define LM_TAG "NodeApp"

NodeApp::NodeApp() 
    : radio(LoraMesher::getInstance()), dataCounter(0), nodePacket(new dataPacket) {
}

NodeApp::~NodeApp() {
    delete nodePacket;
}

void NodeApp::setup() {
    ESP_LOGI(LM_TAG, "=== LoRaMesh Node Application ===");
    ESP_LOGI(LM_TAG, "Node ID: 0x%X", NODE_ID);
    
    led_init();
    led_pattern_startup();
    
    setupLoRaMesher();

    ESP_LOGI(LM_TAG, "Node setup complete. Send interval: %d ms", SEND_INTERVAL_MS);
}

void NodeApp::loop() {
    static uint32_t lastSendTime = 0;
    
    if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
        sensorData sensor = simulateSensorData();
        
        nodePacket->counter = dataCounter++;
        nodePacket->nodeId = NODE_ID;
        nodePacket->timestamp = millis();

        ESP_LOGI(LM_TAG, "=== Sending packet #%d ===", nodePacket->counter);
        ESP_LOGI(LM_TAG, "Node: 0x%X, Time: %d", nodePacket->nodeId, nodePacket->timestamp);
        ESP_LOGI(LM_TAG, "Simulated sensors - Temp: %.1f°C, Humidity: %.1f%%, Battery: %.2fV",
                 sensor.temperature, sensor.humidity, sensor.battery);

        radio.createPacketAndSend(BROADCAST_ADDR, nodePacket, 1);
        led_flash(2, 100);

        ESP_LOGI(LM_TAG, "Routing table size: %d nodes", radio.routingTableSize());
        ESP_LOGI(LM_TAG, "Free heap: %d bytes", ESP.getFreeHeap());
        
        lastSendTime = millis();
    }
    
    delay(1000);
}

sensorData NodeApp::simulateSensorData() {
    sensorData data;
    data.temperature = 20.0 + (random(0, 200) / 10.0); // 20-40°C
    data.humidity = 40.0 + (random(0, 600) / 10.0);    // 40-100%
    data.battery = 3.2 + (random(0, 80) / 100.0);      // 3.2-4.0V
    data.timestamp = millis();
    return data;
}

void NodeApp::setupLoRaMesher() {
    LoraMesher::LoraMesherConfig config;
    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;
    config.module = LORA_MODULE;
    
    radio.begin(config);
    
    TaskHandle_t receiveHandle = createReceiveTask("Node Receive Task");
    if (receiveHandle) {
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(LM_TAG, "LoRaMesher initialized");
        led_pattern_connected();
    } else {
        ESP_LOGE(LM_TAG, "Failed to initialize LoRaMesher");
        led_pattern_error();
    }
}