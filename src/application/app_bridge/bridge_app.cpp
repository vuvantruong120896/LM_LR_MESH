#include "bridge_app.h"
#include "mesh_security_config.h"

// Static member initialization
BridgeApp* BridgeApp::instance = nullptr;

BridgeApp::BridgeApp() 
    : radio(LoraMesher::getInstance()), 
      uartProtocol(nullptr),
      statusCounter(0),
      statusPacket(new bridgeStatus) {
    instance = this;
}

BridgeApp::~BridgeApp() {
    delete statusPacket;
    delete uartProtocol;
}

void BridgeApp::setup() {
    Serial.println("=== LoRaMesh Bridge/Gateway Application ===");
    Serial.printf("Bridge ID: 0x%X\n", BRIDGE_ID);
    
    led_init();
    led_pattern_startup();
    
    // Initialize mesh security first
    if (!initializeMeshSecurity()) {
        Serial.println("Failed to initialize mesh security");
        led_pattern_error();
        return;
    }
    
    // Log security status
    logSecurityStatus();
    
    setupLoRaMesher();
    setupUART();

    Serial.println("Bridge setup complete");
    led_pattern_connected();
}

void BridgeApp::loop() {
    uint32_t currentTime = millis();
    
    // Handle UART communication
    if (uartProtocol) {
        uartProtocol->update();
        updateUARTConnection();
    }
    
    // Send periodic heartbeat
    if (currentTime - bridgeState.lastHeartbeat >= BRIDGE_HEARTBEAT_INTERVAL) {
        if (uartProtocol && bridgeState.uartConnected) {
            uartProtocol->sendHeartbeat();
        }
        bridgeState.lastHeartbeat = currentTime;
    }
    
    // Send periodic status
    if (currentTime - bridgeState.lastStatusSent >= BRIDGE_STATUS_INTERVAL) {
        sendBridgeStatus();
        bridgeState.lastStatusSent = currentTime;
    }
    
    // Simple status LED indication
    if (statusCounter++ % 100 == 0) {
        if (bridgeState.uartConnected) {
            led_pattern_message(); // Quick flash for active
        } else {
            led_pattern_error();   // Error pattern for disconnected
        }
    }
    
    delay(100); // Main loop delay
}

void BridgeApp::setupLoRaMesher() {
    Serial.println("[BRIDGE] Setting up LoRaMesher...");
    
    LoraMesher::LoraMesherConfig config;
    config.loraCs = LORA_CS;
    config.loraRst = LORA_RST;
    config.loraIrq = LORA_IRQ;
    config.loraIo1 = LORA_IO1;
    config.module = LORA_MODULE;
    
    radio.begin(config);
    
    TaskHandle_t receiveHandle = createBridgeReceiveTask();
    if (receiveHandle) {
        Serial.printf("[BRIDGE] Setting task handle %p for bridge data\n", receiveHandle);
        radio.setReceiveAppDataTaskHandle(receiveHandle);
        radio.start();
        Serial.println("LoRaMesher initialized for Bridge");
    } else {
        Serial.println("Failed to create bridge receive task");
        led_pattern_error();
    }
}

void BridgeApp::setupUART() {
    Serial.println("[BRIDGE] Setting up UART communication...");
    
    // Create UART protocol instance
    uartProtocol = new UartProtocol(&Serial1);
    uartProtocol->begin(UART_BAUD_RATE);
    
    bridgeState.uartConnected = true;
    
    Serial.printf("[BRIDGE] UART initialized on Serial1, baud: %d\n", UART_BAUD_RATE);
    Serial.printf("[BRIDGE] RX pin: %d, TX pin: %d\n", UART_RX_PIN, UART_TX_PIN);
}

void BridgeApp::forwardToUART(AppPacket<dataPacket>* packet) {
    if (!uartProtocol || !bridgeState.uartConnected) {
        Serial.println("[BRIDGE] UART not available for forwarding");
        return;
    }
    
    bridgeState.totalMeshPackets++;
    
    // Extract data from packet
    dataPacket* data = packet->payload;
    uint16_t sourceNode = packet->src;
    
    Serial.printf("[BRIDGE] Forwarding packet from node 0x%04X to UART\n", sourceNode);
    Serial.printf("[BRIDGE] Data - Counter: %d, Timestamp: %d\n", 
                  data->counter, data->timestamp);
    
    // Send via UART
    if (uartProtocol->sendDataPacket(*data, sourceNode)) {
        bridgeState.packetsForwarded++;
        led_pattern_message(); // Flash LED on successful forward
    } else {
        bridgeState.uartErrors++;
        Serial.println("[BRIDGE] Failed to send packet via UART");
    }
}

void BridgeApp::sendBridgeStatus() {
    if (!uartProtocol || !bridgeState.uartConnected) {
        return;
    }
    
    // Prepare status packet
    UartBridgeStatus status;
    status.bridgeId = BRIDGE_ID;
    status.uptime = millis() / 1000; // Convert to seconds
    status.connectedNodes = radio.routingTableSize();
    status.totalPacketsReceived = bridgeState.totalMeshPackets;
    status.totalPacketsSent = bridgeState.packetsForwarded;
    status.freeHeap = ESP.getFreeHeap() / 1024; // Convert to KB
    status.lastRSSI = -99; // TODO: Get from last received packet
    status.lastSNR = 10;   // TODO: Get from last received packet
    status.meshHealth = (status.connectedNodes > 0) ? 100 : 0; // Simple health metric
    
    Serial.printf("[BRIDGE] Sending status - Nodes: %d, Packets: %d/%d, Heap: %dKB\n",
                  status.connectedNodes, status.totalPacketsReceived, 
                  status.totalPacketsSent, status.freeHeap);
    
    uartProtocol->sendStatusPacket(status);
}

void BridgeApp::updateUARTConnection() {
    static uint32_t lastCheck = 0;
    uint32_t currentTime = millis();
    
    if (currentTime - lastCheck >= 5000) { // Check every 5 seconds
        bool wasConnected = bridgeState.uartConnected;
        bridgeState.uartConnected = uartProtocol && uartProtocol->isConnected();
        
        if (wasConnected != bridgeState.uartConnected) {
            if (bridgeState.uartConnected) {
                Serial.println("[BRIDGE] UART connection established");
                led_pattern_connected();
            } else {
                Serial.println("[BRIDGE] UART connection lost");
                led_pattern_error();
            }
        }
        
        lastCheck = currentTime;
    }
}

// Static callback for processing bridge packets
void BridgeApp::processBridgePackets(void* parameter) {
    Serial.println("[BRIDGE-TASK] Bridge packet processing task started");
    
    for (;;) {
        Serial.println("[BRIDGE-TASK] Waiting for mesh packet notification...");
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);
        
        Serial.println("[BRIDGE-TASK] GOT NOTIFICATION! Processing bridge packets...");
        led_pattern_message();

        while (BridgeApp::instance->radio.getReceivedQueueSize() > 0) {
            Serial.println("[BRIDGE-TASK] Processing received mesh packet for bridge");
            Serial.printf("[BRIDGE-TASK] Queue size: %d\n", 
                         BridgeApp::instance->radio.getReceivedQueueSize());

            AppPacket<dataPacket>* packet = BridgeApp::instance->radio.getNextAppPacket<dataPacket>();
            
            // Forward to UART instead of MQTT
            BridgeApp::instance->forwardToUART(packet);
            
            BridgeApp::instance->radio.deletePacket(packet);
        }
    }
}

TaskHandle_t BridgeApp::createBridgeReceiveTask() {
    TaskHandle_t taskHandle = NULL;
    
    Serial.println("[BRIDGE] Creating bridge receive task...");
    
    int res = xTaskCreate(
        processBridgePackets,
        "Bridge Receive Task",
        4096,
        (void*) 1,
        2,
        &taskHandle);
    
    if (res != pdPASS) {
        Serial.printf("[BRIDGE] Error: Bridge task creation failed: %d\n", res);
        led_pattern_error();
        return NULL;
    }
    
    Serial.printf("[BRIDGE] Bridge task created successfully, handle: %p\n", taskHandle);
    return taskHandle;
}
