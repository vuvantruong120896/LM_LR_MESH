#include "mesh_utils.h"
#include "led_control.h"

#define LM_TAG "MeshUtils"

extern LoraMesher& radio;

void printPacket(const dataPacket& data) {
    ESP_LOGI(LM_TAG, "Data Counter: %d, Node: %X, Time: %d", 
                  data.counter, data.nodeId, data.timestamp);
}

void printDataPacket(AppPacket<dataPacket>* packet) {
    // CRITICAL FIX: Validate packet pointer
    if (packet == nullptr) {
        ESP_LOGE(LM_TAG, "printDataPacket called with NULL packet!");
        return;
    }
    
    ESP_LOGI(LM_TAG, "Packet from %X, size %d", packet->src, packet->payloadSize);
    
    // CRITICAL FIX: Calculate number of structs, not bytes!
    dataPacket* dPacket = packet->payload;
    size_t payloadBytes = packet->getPayloadLength();
    size_t numPackets = payloadBytes / sizeof(dataPacket);
    
    // Safety check: ensure payload is valid
    if (payloadBytes % sizeof(dataPacket) != 0) {
        ESP_LOGW(LM_TAG, "Warning: Payload size (%zu bytes) not multiple of dataPacket size (%zu bytes). Data may be corrupted.", 
                 payloadBytes, sizeof(dataPacket));
    }
    
    ESP_LOGV(LM_TAG, "Payload: %zu bytes = %zu dataPacket structs (struct size: %zu)", 
             payloadBytes, numPackets, sizeof(dataPacket));
    
    for (size_t i = 0; i < numPackets; i++) {
        printPacket(dPacket[i]);
    }
}

void printSensorData(const sensorData& data) {
    ESP_LOGI(LM_TAG, "Sensor - Temp: %.2f°C, Hum: %.2f%%, Bat: %.2fV, Time: %d",
                  data.temperature, data.humidity, data.battery, data.timestamp);
}

void processReceivedPackets(void*) {
    
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        led_pattern_message();

        while (radio.getReceivedQueueSize() > 0) {
            ESP_LOGI(LM_TAG, "Processing received packet");
            ESP_LOGI(LM_TAG, "Queue size: %d", radio.getReceivedQueueSize());

            AppPacket<dataPacket>* packet = radio.getNextAppPacket<dataPacket>();
            
            // CRITICAL FIX: Check for NULL packet before processing
            if (packet == nullptr) {
                ESP_LOGW(LM_TAG, "Received NULL packet from queue, skipping");
                continue;
            }
            
            printDataPacket(packet);
            radio.deletePacket(packet);
        }
    }
}

TaskHandle_t createReceiveTask(const char* taskName) {
    TaskHandle_t taskHandle = NULL;

    int res = xTaskCreate(
        processReceivedPackets,
        taskName,
        4096,
        (void*) 1,
        2,
        &taskHandle);
    
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Error: %s creation failed: %d", taskName, res);
        led_pattern_error();
        return NULL;
    }
    return taskHandle;
}

void setupLoRaMesher(LoraMesher::LoraMesherConfig& config) {
    ESP_LOGI(LM_TAG, "[SETUP] Initializing LoRaMesher...");
    radio.begin(config);
    
    TaskHandle_t receiveHandle = createReceiveTask("Mesh Receive Task");
    if (receiveHandle) {
        ESP_LOGI(LM_TAG, "[SETUP] Setting task handle %p for app data", receiveHandle);
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(LM_TAG, "LoRaMesher initialized");
        led_pattern_connected();
    } else {
        ESP_LOGE(LM_TAG, "Failed to initialize LoRaMesher");
        led_pattern_error();
    }
}