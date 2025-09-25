#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "bridge_config.h"
#include "../common/led_control.h"  
#include "../common/mesh_utils.h"
#include "components/lora_mesh_manager/include/LoraMesher.h"

LoraMesher& radio = LoraMesher::getInstance();

// WiFi and MQTT clients
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Bridge state
struct BridgeState {
    bool wifiConnected = false;
    bool mqttConnected = false;
    uint32_t packetsForwarded = 0;
    uint32_t lastMqttReconnect = 0;
    uint32_t totalMeshPackets = 0;
} bridgeState;

uint32_t statusCounter = 0;
bridgeStatus* statusPacket = new bridgeStatus;

// Forward declarations
void connectWiFi();
void connectMQTT();
void forwardToMQTT(AppPacket<dataPacket>* packet);
void publishBridgeStatus();

void connectWiFi() {
    ESP_LOGI(LM_TAG, "Connecting to WiFi: %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - startTime) < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print(".");
        led_toggle();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        bridgeState.wifiConnected = true;
        ESP_LOGI(LM_TAG, "WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
        led_pattern_connected();
    } else {
        bridgeState.wifiConnected = false;
        ESP_LOGI(LM_TAG, "\nWiFi connection failed!");
        led_pattern_error();
    }
}

void connectMQTT() {
    if (!bridgeState.wifiConnected) return;
    
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    
    String clientId = "LoRaBridge_" + String(BRIDGE_ID, HEX);
    
    if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
        bridgeState.mqttConnected = true;
        ESP_LOGI(LM_TAG, "MQTT connected!");
        
        // Subscribe to downlink topics
        String subTopic = String(MQTT_TOPIC_BASE) + "/downlink/" + String(BRIDGE_ID, HEX);
        mqttClient.subscribe(subTopic.c_str());
        ESP_LOGI(LM_TAG, "Subscribed to: %s", subTopic.c_str());
        
        led_pattern_connected();
    } else {
        bridgeState.mqttConnected = false;
        ESP_LOGI(LM_TAG, "MQTT connection failed, rc=%d", mqttClient.state());
        led_pattern_error();
    }
}

void forwardToMQTT(AppPacket<dataPacket>* packet) {
    if (!bridgeState.mqttConnected) return;
    
    // Create JSON payload
    String topic = String(MQTT_TOPIC_BASE) + "/uplink/" + String(packet->src, HEX);
    String payload = "{";
    payload += "\"src\":\"" + String(packet->src, HEX) + "\",";
    payload += "\"dst\":\"" + String(packet->dst, HEX) + "\",";
    payload += "\"bridge\":\"" + String(BRIDGE_ID, HEX) + "\",";
    payload += "\"timestamp\":" + String(millis()) + ",";
    payload += "\"data\":{";
    
    // Add packet data
    if (packet->payloadSize > 0) {
        dataPacket* data = packet->payload;
        payload += "\"counter\":" + String(data->counter) + ",";
        payload += "\"nodeId\":\"" + String(data->nodeId, HEX) + "\",";
        payload += "\"nodeTimestamp\":" + String(data->timestamp);
    }
    
    payload += "}}";
    
    if (mqttClient.publish(topic.c_str(), payload.c_str())) {
        bridgeState.packetsForwarded++;
        ESP_LOGI(LM_TAG, "Forwarded to MQTT: %s", topic.c_str());
        led_flash(1, 25);  // Quick blink for forwarded packet
    } else {
        ESP_LOGI(LM_TAG, "MQTT publish failed");
    }
}

void publishBridgeStatus() {
    if (!bridgeState.mqttConnected) return;
    
    String topic = String(MQTT_TOPIC_BASE) + "/status/" + String(BRIDGE_ID, HEX);
    String payload = "{";
    payload += "\"bridgeId\":\"" + String(BRIDGE_ID, HEX) + "\",";
    payload += "\"wifiConnected\":" + String(bridgeState.wifiConnected ? "true" : "false") + ",";
    payload += "\"mqttConnected\":" + String(bridgeState.mqttConnected ? "true" : "false") + ",";
    payload += "\"connectedNodes\":" + String(radio.routingTableSize()) + ",";
    payload += "\"packetsForwarded\":" + String(bridgeState.packetsForwarded) + ",";
    payload += "\"totalMeshPackets\":" + String(bridgeState.totalMeshPackets) + ",";
    payload += "\"uptime\":" + String(millis()) + ",";
    payload += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    payload += "\"rssi\":" + String(WiFi.RSSI()) + "";
    payload += "}";
    
    mqttClient.publish(topic.c_str(), payload.c_str());
    ESP_LOGI(LM_TAG, "Bridge status published to MQTT");
}

// Override the common receive function for bridge-specific handling
void processBridgePackets(void*) {
    for (;;) {
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        led_pattern_message();
        
        while (radio.getReceivedQueueSize() > 0) {
            bridgeState.totalMeshPackets++;
            ESP_LOGI(LM_TAG, "Bridge processing packet #%d", bridgeState.totalMeshPackets);
            
            AppPacket<dataPacket>* packet = radio.getNextAppPacket<dataPacket>();
            
            // Print to serial
            printDataPacket(packet);
            
            // Forward to MQTT
            forwardToMQTT(packet);
            
            radio.deletePacket(packet);
        }
    }
}

TaskHandle_t createBridgeReceiveTask() {
    TaskHandle_t taskHandle = NULL;
    int res = xTaskCreate(
        processBridgePackets,
        "Bridge Receive Task",
        8192,  // Larger stack for WiFi/MQTT operations
        (void*) 1,
        2,
        &taskHandle);
    
    if (res != pdPASS) {
        ESP_LOGI(LM_TAG, "Error: Bridge receive task creation failed: %d", res);
        led_pattern_error();
        return NULL;
    }
    
    return taskHandle;
}

void setupBridge() {
    ESP_LOGI(LM_TAG, "=== LoRaMesh Bridge/Gateway Application ===");
    ESP_LOGI(LM_TAG, "Bridge ID: 0x%X", BRIDGE_ID);
    
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
    
    radio.begin(config);
    
    TaskHandle_t receiveHandle = createBridgeReceiveTask();
    if (receiveHandle) {
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        ESP_LOGI(LM_TAG, "LoRaMesher initialized");
        led_pattern_connected();
    } else {
        ESP_LOGI(LM_TAG, "Failed to initialize LoRaMesher");
        led_pattern_error();
        return;
    }
    
    // Connect WiFi and MQTT
    connectWiFi();
    if (bridgeState.wifiConnected) {
        connectMQTT();
    }
    
    ESP_LOGI(LM_TAG, "Bridge setup complete");
}

void loop() {
    // Maintain MQTT connection
    if (bridgeState.wifiConnected) {
        if (!mqttClient.connected()) {
            if (millis() - bridgeState.lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
                ESP_LOGI(LM_TAG, "Attempting MQTT reconnection...");
                connectMQTT();
                bridgeState.lastMqttReconnect = millis();
            }
        } else {
            mqttClient.loop();
        }
        
        // Publish status every 60 seconds
        if (statusCounter % 60 == 0) {
            publishBridgeStatus();
        }
    } else {
        // Try to reconnect WiFi every 30 seconds
        if (statusCounter % 30 == 0) {
            connectWiFi();
            if (bridgeState.wifiConnected) {
                connectMQTT();
            }
        }
    }
    
    // Send periodic bridge status to mesh (every 120 seconds)
    if (statusCounter % 120 == 0) {
        statusPacket->connectedNodes = radio.routingTableSize();
        statusPacket->totalPackets = bridgeState.totalMeshPackets;
        statusPacket->wifiConnected = bridgeState.wifiConnected;
        statusPacket->mqttConnected = bridgeState.mqttConnected;
        
        ESP_LOGI("Broadcasting bridge status - Nodes: %d, Packets: %d\n",
                      statusPacket->connectedNodes, statusPacket->totalPackets);
        radio.createPacketAndSend(BROADCAST_ADDR, statusPacket, 1);
    }
    
    // Status logging
    if (statusCounter % 30 == 0) {
        ESP_LOGI(LM_TAG, "=== Bridge Status ===");
        ESP_LOGI(LM_TAG, "WiFi: %s, MQTT: %s",
                  bridgeState.wifiConnected ? "Connected" : "Disconnected",
                  bridgeState.mqttConnected ? "Connected" : "Disconnected");
        ESP_LOGI(LM_TAG, "Mesh nodes: %d, Packets forwarded: %d",
                  radio.routingTableSize(), bridgeState.packetsForwarded);
        ESP_LOGI(LM_TAG, "Free heap: %d bytes",
                  ESP.getFreeHeap());
        ESP_LOGI(LM_TAG, "Uptime: %d seconds", millis() / 1000);
    }
    
    statusCounter++;
    delay(1000);  // 1 second loop
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Wait for serial
    
    setupBridge();
}