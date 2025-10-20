#include "mesh_utils.h"
#include "led_control.h"
#include "../app_node/node_app.h"
#include "components/lora_mesh_manager/src/services/TimeSyncService.h"

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
    ESP_LOGI(LM_TAG, "Device Type: %s, Battery: %.2fV, Counter: %d, Time: %d",
                  deviceTypeToString(data.deviceType), data.battery, data.counter, data.timestamp);
    
    // Print sensor-specific data based on device type
    switch (data.deviceType) {
        case DeviceType::SOIL_SENSOR:
            ESP_LOGI(LM_TAG, "  Soil - Moisture: %.1f%%, Temp: %.1f°C, pH: %.2f, EC: %.2f mS/cm",
                     data.data.soil.soilMoisture, data.data.soil.soilTemperature, 
                     data.data.soil.pH, data.data.soil.ec);
            ESP_LOGI(LM_TAG, "  NPK - N: %.1f, P: %.1f, K: %.1f mg/kg",
                     data.data.soil.nitrogen, data.data.soil.phosphorus, data.data.soil.potassium);
            break;
            
        case DeviceType::ENV_SENSOR:
            ESP_LOGI(LM_TAG, "  Environment - Temp: %.1f°C, Humidity: %.1f%%, Pressure: %.1f hPa, Light: %.1f lux",
                     data.data.environment.temperature, data.data.environment.humidity,
                     data.data.environment.pressure, data.data.environment.lightIntensity);
            break;
            
        case DeviceType::WATER_SENSOR:
            ESP_LOGI(LM_TAG, "  Water - Temp: %.1f°C, pH: %.2f, TDS: %.1f ppm, Turbidity: %.1f NTU",
                     data.data.water.waterTemp, data.data.water.pH,
                     data.data.water.tds, data.data.water.turbidity);
            break;
            
        default:
            ESP_LOGI(LM_TAG, "  Generic values: [0]=%.2f [1]=%.2f [2]=%.2f",
                     data.data.values[0], data.data.values[1], data.data.values[2]);
            break;
    }
}

void processReceivedPackets(void*) {
    
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        led_pattern_message();

        while (radio.getReceivedQueueSize() > 0) {
            ESP_LOGI(LM_TAG, "Processing received packet");
            ESP_LOGI(LM_TAG, "Queue size: %d", radio.getReceivedQueueSize());

            AppPacket<uint8_t>* packet = radio.getNextAppPacket<uint8_t>();
            
            // CRITICAL FIX: Check for NULL packet before processing
            if (packet == nullptr) {
                ESP_LOGW(LM_TAG, "Received NULL packet from queue, skipping");
                continue;
            }
            
            // Check if this is a time sync packet (8 bytes)
            if (packet->payloadSize == 8) {
                ESP_LOGI(LM_TAG, "⏰ Time sync packet detected (8 bytes from 0x%04X) - processing...", packet->src);
                
                // Process time sync packet directly
                if (NodeApp::instance) {
                    AppPacket<TimeSyncService::TimeSyncPacket>* timeSyncPacket = 
                        reinterpret_cast<AppPacket<TimeSyncService::TimeSyncPacket>*>(packet);
                    NodeApp::instance->handleTimeSyncPacket(timeSyncPacket);
                    ESP_LOGI(LM_TAG, "✅ Time sync packet processed successfully");
                } else {
                    ESP_LOGW(LM_TAG, "NodeApp instance not available, discarding time sync packet");
                }
                
                radio.deletePacket(packet);
                continue;
            }
            
            // Cast to dataPacket for sensor data processing
            AppPacket<dataPacket>* sensorPacket = reinterpret_cast<AppPacket<dataPacket>*>(packet);
            printDataPacket(sensorPacket);
            radio.deletePacket(sensorPacket);
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