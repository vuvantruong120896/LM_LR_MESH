#include "LoraMesher.h"
#include "../services/NetkeyDistributionService.h"

#ifndef ARDUINO
#include "EspHal.h"
#endif

// Include device-specific config for SPI pin definitions
#ifndef DEVICE_MODE
#define DEVICE_MODE 1 // Default to Node mode
#endif

#if DEVICE_MODE == 2
#include "../../../../application/app_gateway/gateway_config.h"
#else
#include "../../../../application/app_node/node_config.h"
#endif

LoraMesher::LoraMesher() {}

void LoraMesher::begin(LoraMesherConfig config) {
    ESP_LOGV(LM_TAG, "Initializing LoraMesher v%s", LM_VERSION);

    // Set the configuration
    *loraMesherConfig = config;
    initConfiguration();

    // Initialize the radio
    initializeLoRa();

    // Recalculate the max time on air
    recalculateMaxTimeOnAir();

    // Initialize the queues
    initializeSchedulers();
}

void LoraMesher::standby() {
    //Get actual priority
    UBaseType_t prevPriority = uxTaskPriorityGet(NULL);

    //Set max priority
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

    int res = radio->standby();
    if (res != 0)
        ESP_LOGE(LM_TAG, "Standby gave error: %d", res);

    //Clear Dio Actions
    clearDioActions();

    //Suspend all tasks
    vTaskSuspend(ReceivePacket_TaskHandle);
    vTaskSuspend(Hello_TaskHandle);
    vTaskSuspend(ReceiveData_TaskHandle);
    vTaskSuspend(SendData_TaskHandle);
    vTaskSuspend(RoutingTableManager_TaskHandle);
    vTaskSuspend(QueueManager_TaskHandle);

    //Set previous priority
    vTaskPrioritySet(NULL, prevPriority);
}

void LoraMesher::start() {
    // Get actual priority
    UBaseType_t prevPriority = uxTaskPriorityGet(NULL);

    // Set max priority
    vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

    // Resume all tasks
    vTaskResume(ReceivePacket_TaskHandle);
    vTaskResume(Hello_TaskHandle);
    vTaskResume(ReceiveData_TaskHandle);
    vTaskResume(SendData_TaskHandle);
    vTaskResume(RoutingTableManager_TaskHandle);
    vTaskResume(QueueManager_TaskHandle);

    // Start Receiving
    startReceiving();

    // Set previous priority
    vTaskPrioritySet(NULL, prevPriority);
}

LoraMesher::~LoraMesher() {
    vTaskDelete(ReceivePacket_TaskHandle);
    vTaskDelete(Hello_TaskHandle);
    vTaskDelete(ReceiveData_TaskHandle);
    vTaskDelete(SendData_TaskHandle);
    vTaskDelete(RoutingTableManager_TaskHandle);
    vTaskDelete(QueueManager_TaskHandle);

    ToSendPackets->Clear();
    delete ToSendPackets;
    ReceivedPackets->Clear();
    delete ReceivedPackets;
    ReceivedAppPackets->Clear();
    delete ReceivedAppPackets;

    clearDioActions();
    radio->reset();

    delete radio;
}

void LoraMesher::setConfig(LoraMesherConfig config) {
    standby();

    *loraMesherConfig = config;
    initConfiguration();
    recalculateMaxTimeOnAir();

    restartRadio();

    start();
}

void LoraMesher::restartRadio() {
    radio->reset();
    initializeLoRa();

    ESP_LOGI(LM_TAG, "Restarting radio DONE");
}

void LoraMesher::initConfiguration() {
    ESP_LOGV(LM_TAG, "Initializing Configuration");

    PacketFactory::setMaxPacketSize(loraMesherConfig->max_packet_size);
}

void LoraMesher::initializeLoRa() {
    ESP_LOGV(LM_TAG, "Initializing RadioLib");

    LoraMesherConfig config = *loraMesherConfig;

    ESP_LOGI(LM_TAG, "LoRaMesher Configuration:");
    ESP_LOGI(LM_TAG, "LoRa Module: %d", config.module);
    ESP_LOGI(LM_TAG, "LoRa CS: %d", config.loraCs);
    ESP_LOGI(LM_TAG, "LoRa IRQ: %d", config.loraIrq);
    ESP_LOGI(LM_TAG, "LoRa RST: %d", config.loraRst);
    ESP_LOGI(LM_TAG, "LoRa IO1: %d", config.loraIo1);

#ifdef ARDUINO
    if (config.spi == nullptr) {
        #ifdef LORA_MISO
            // SPI.begin which picks up SCK MISO, MOSI, CS rather than LORA_MISO etc
            // ttgo-lora32-v21new defines LORA_SCK the same as MISO, MOSI, CS etc so it works on default
            // lilygo_t3_s3_sx127x howwever defines LORA_MISO etc but defines SCK, MISO etc as the same as SD_SCK instead so LoraMesher fails trying to talk to the SD
                SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
        #else 
                // Use SPI pin definitions from config files (bridge_config.h or node_config.h)
                // SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS are defined in the respective config headers
                #ifdef SPI_SCK
                    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
                    ESP_LOGI(LM_TAG, "SPI.begin(SCK:%d, MISO:%d, MOSI:%d, CS:%d);", SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);
                #else
                    #error "SPI pins not defined! Please define SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS in config file"
                #endif
        #endif
                config.spi = &SPI;
    }

    if (radio == nullptr) {
        switch (config.module) {
            case LoraModules::SX1276_MOD:
                ESP_LOGV(LM_TAG, "Using SX1276 module");
                radio = new LM_SX1276(config.loraCs, config.loraIrq, config.loraRst, config.spi);
                break;
            case LoraModules::SX1262_MOD:
                ESP_LOGV(LM_TAG, "Using SX1262 module");
                radio = new LM_SX1262(config.loraCs, config.loraIrq, config.loraRst, config.loraIo1, config.spi);
                break;
            case LoraModules::SX1278_MOD:
                ESP_LOGV(LM_TAG, "Using SX1278 module");
                radio = new LM_SX1278(config.loraCs, config.loraIrq, config.loraRst, config.loraIo1, config.spi);
                break;
            case LoraModules::SX1268_MOD:
                ESP_LOGV(LM_TAG, "Using SX1268 module");
                radio = new LM_SX1268(config.loraCs, config.loraIrq, config.loraRst, config.loraIo1, config.spi);
                break;
            case LoraModules::SX1280_MOD:
                ESP_LOGV(LM_TAG, "Using SX1280 module");
                radio = new LM_SX1280(config.loraCs, config.loraIrq, config.loraRst, config.loraIo1, config.spi);
                break;
            case LoraModules::RFM95_MOD:
                ESP_LOGV(LM_TAG, "Using RFM95 module");
                radio = new LM_RFM95(config.loraCs, config.loraIrq, config.loraRst, config.spi);
                break;
            default:
                ESP_LOGV(LM_TAG, "Using SX1276 module");
                radio = new LM_SX1276(config.loraCs, config.loraIrq, config.loraRst, config.spi);
                break;
        }
    }

#else
    if (config.hal == nullptr)
        config.hal = new EspHal(SPI_SCK, SPI_MISO, SPI_MOSI);

    if (config.hal == nullptr)
        ESP_LOGE(LM_TAG, "Could not create SPI HAL");

    if (radio == nullptr) {
        Module* mod = new Module(config.hal, config.loraCs, config.loraIrq, config.loraRst, config.loraIo1);

        switch (config.module) {
            case LoraModules::SX1276_MOD:
                ESP_LOGV(LM_TAG, "Using SX1276 module");
                radio = new LM_SX1276(mod);
                break;
            case LoraModules::SX1262_MOD:
                ESP_LOGV(LM_TAG, "Using SX1262 module");
                radio = new LM_SX1262(mod);
                break;
            case LoraModules::SX1278_MOD:
                ESP_LOGV(LM_TAG, "Using SX1278 module");
                radio = new LM_SX1278(mod);
                break;
            case LoraModules::SX1268_MOD:
                ESP_LOGV(LM_TAG, "Using SX1268 module");
                radio = new LM_SX1268(mod);
                break;
            case LoraModules::SX1280_MOD:
                ESP_LOGV(LM_TAG, "Using SX1280 module");
                radio = new LM_SX1280(mod);
                break;
            case LoraModules::RFM95_MOD:
                ESP_LOGV(LM_TAG, "Using RFM95 module");
                radio = new LM_RFM95(mod);
                break;
            default:
                ESP_LOGV(LM_TAG, "Using SX1276 module");
                radio = new LM_SX1276(mod);
                break;
        }
    }

#endif

    if (radio == NULL) {
        ESP_LOGE(LM_TAG, "RadioLib not initialized properly");
    }

    // Set up the radio parameters
    ESP_LOGV(LM_TAG, "Initializing radio");
    int res = radio->begin(config.freq, config.bw, config.sf, config.cr, config.syncWord, config.power, config.preambleLength);
    if (res != 0) {
        ESP_LOGE(LM_TAG, "Radio module gave error: %d", res);
    }

#ifdef LM_ADDCRC_PAYLOAD
    radio->setCRC(true);
#endif
    ESP_LOGI(LM_TAG, "LoRa module initialization DONE");
}

void LoraMesher::setDioActionsForScanChannel() {
    // set the function that will be called
    // when LoRa preamble is detected
    clearDioActions();
    // radio->setDioActionForScanning(onReceive);
}

void LoraMesher::setDioActionsForReceivePacket() {
    clearDioActions();

    radio->setDioActionForReceiving(onReceive);
}

void LoraMesher::clearDioActions() {
    radio->clearDioActions();
}

//TODO: Retry start receiving if it fails
int LoraMesher::startReceiving() {
    setDioActionsForReceivePacket();

    int res = radio->startReceive();
    if (res != 0) {
        ESP_LOGE(LM_TAG, "Starting receiving gave error: %d", res);
        restartRadio();
        return startReceiving();
    }
    return res;
}

void LoraMesher::channelScan() {
    setDioActionsForScanChannel();

    int res = radio->scanChannel();

    if (res != RADIOLIB_ERR_NONE) {
        ESP_LOGE(LM_TAG, "Starting new scan failed, code %d", res);
        channelScan();
    }

}

//TODO: Retry start channel scan if it fails
int LoraMesher::startChannelScan() {
    setDioActionsForScanChannel();

    int state = radio->startChannelScan();
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(LM_TAG, "Starting new scan failed, code %d", state);
        startChannelScan();
    }

    return state;
}

void LoraMesher::initializeSchedulers() {
    ESP_LOGV(LM_TAG, "Setting up Schedulers");
    int res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->receivingRoutine(); },
        "Receiving routine",
        4096,
        this,
        6,
        &ReceivePacket_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Receiving routine creation gave error: %d", res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendPackets(); },
        "Sending routine",
        4096,
        this,
        5,
        &SendData_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Sending Task creation gave error: %d", res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->sendHelloPacket(); },
        "Hello routine",
        4096,
        this,
        4,
        &Hello_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Process Task creation gave error: %d", res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->processPackets(); },
        "Process routine",
        4096,
        this,
        3,
        &ReceiveData_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Process Task creation gave error: %d", res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->routingTableManager(); },
        "Routing Table Manager routine",
        4096,
        this,
        2,
        &RoutingTableManager_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Routing Table Manager Task creation gave error: %d", res);
    }
    res = xTaskCreate(
        [](void* o) { static_cast<LoraMesher*>(o)->queueManager(); },
        "Queue Manager routine",
        4096,
        this,
        2,
        &QueueManager_TaskHandle);
    if (res != pdPASS) {
        ESP_LOGE(LM_TAG, "Queue Manager Task creation gave error: %d", res);
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void LoraMesher::onReceive(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    xHigherPriorityTaskWoken = xTaskNotifyFromISR(
        LoraMesher::getInstance().ReceivePacket_TaskHandle,
        0,
        eSetValueWithoutOverwrite,
        &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE)
        portYIELD_FROM_ISR();
}

void LoraMesher::receivingRoutine() {
    ESP_LOGV(LM_TAG, "Receiving routine started");
    vTaskSuspend(NULL);

    BaseType_t TWres;
    size_t packetSize;
    int8_t rssi, snr;
    int16_t state;

    for (;;) {
        TWres = xTaskNotifyWait(
            pdTRUE,
            pdFALSE,
            NULL,
            portMAX_DELAY);

        if (TWres == pdPASS) {
            //truongvv
            // ESP_LOGV(LM_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
            // ESP_LOGV(LM_TAG, "Free heap: %d", getFreeHeap());

            hasReceivedMessage = true;

            packetSize = radio->getPacketLength();
            
            // CRITICAL FIX: REJECT oversized packets completely to prevent buffer overflow
            // RadioLib may ignore the size parameter in readData() and read the entire FIFO
            // If we receive 247 bytes but only allocate 150 bytes buffer → CRASH
            size_t max_packet_size = PacketFactory::getMaxPacketSize();
            if (packetSize > max_packet_size) {
                ESP_LOGE(LM_TAG, "CRITICAL: Received packet size (%d bytes) exceeds MAX_PACKET_SIZE (%d bytes)!", 
                         packetSize, max_packet_size);
                ESP_LOGE(LM_TAG, "DROPPING oversized packet to prevent buffer overflow. Check sender configuration!");
                
                // Skip this packet entirely - restart receiving without processing
                // This clears the radio FIFO buffer safely
                startReceiving();
            }
            else if (packetSize == 0) {
                ESP_LOGW(LM_TAG, "Empty packet received, skipping");
                startReceiving();
            }
            else {
                // Now safe: packetSize <= max_packet_size
                Packet<uint8_t>* rx = PacketService::createEmptyPacket(packetSize);
                
                // CRITICAL FIX: Check if packet allocation failed
                if (rx == nullptr) {
                    ESP_LOGE(LM_TAG, "Failed to allocate RX packet, dropping. Free heap: %d", esp_get_free_heap_size());
                    startReceiving();
                }
                else {
                    rssi = (int8_t) round(radio->getRSSI());
                    snr = (int8_t) round(radio->getSNR());

                    if (rssi >= 10) {   
                        deletePacket(rx);
                        startReceiving();
                    }
                    else {
                        ESP_LOGI(LM_TAG, "📧 Receiving LoRa packet: Size: %d bytes RSSI: %d SNR: %d", packetSize, rssi, snr);

                        state = radio->readData(reinterpret_cast<uint8_t*>(rx), packetSize);

                        if (state != RADIOLIB_ERR_NONE) {
                            ESP_LOGW(LM_TAG, "Reading packet data gave error: %d", state);
                            if (state == RADIOLIB_ERR_SPI_WRITE_FAILED) {
                                ESP_LOGW(LM_TAG, "SPI Write failed, restarting radio");
                                restartRadio();
                            }

                            // TODO: Set a count to get the number of CRC errors
                            deletePacket(rx);
                        }
                        else if (packetSize != rx->packetSize) {
                            ESP_LOGW(LM_TAG, "Packet size is different from the size read");
                            deletePacket(rx);
                        }
                        else {
                            //Create a Packet Queue element containing the Packet
                            QueuePacket<Packet<uint8_t>>* pq = PacketQueueService::createQueuePacket(rx, 0, 0, rssi, snr);

                            //Add the Packet Queue element created into the ReceivedPackets List
                            ReceivedPackets->Append(pq);

                            //Notify that a packet needs to be process
                            TWres = xTaskNotifyFromISR(
                                ReceiveData_TaskHandle,
                                0,
                                eSetValueWithoutOverwrite,
                                &TWres);
                        }

                        startReceiving();
                    }
                }
            }
        }
    }
}

uint16_t LoraMesher::getLocalAddress() {
    return WiFiService::getLocalAddress();
}

/**
 *  Region Packet Service
**/

void LoraMesher::waitBeforeSend(uint8_t repeatedDetectPreambles) {
    // TODO: Why did I set this if?
    if (repeatedDetectPreambles > RoutingTableService::routingTableSize())
        return;

    hasReceivedMessage = false;

    //Random delay, to avoid some collisions.
    uint32_t randomDelay = getPropagationTimeWithRandom(repeatedDetectPreambles);

    //truongvv
    // ESP_LOGV(LM_TAG, "RandomDelay %d ms", (int) randomDelay);

    //Set a random delay, to avoid some collisions.
    vTaskDelay(randomDelay / portTICK_PERIOD_MS);

    if (hasReceivedMessage) {
        startReceiving();
        ESP_LOGV(LM_TAG, "Preamble detected while waiting %d", repeatedDetectPreambles);
        waitBeforeSend(repeatedDetectPreambles + 1);
    }
}

uint32_t LoraMesher::getMaxPropagationTime() {
    return maxTimeOnAir;
}

bool LoraMesher::sendPacket(Packet<uint8_t>* p) {
    waitBeforeSend(1);

    clearDioActions();

    // Print the packet to be sent
    printHeaderPacket(p, "send");

    //Blocking transmit, it is necessary due to deleting the packet after sending it. 
    int resT = radio->transmit(reinterpret_cast<uint8_t*>(p), p->packetSize);

    //Start receiving again after sending a packet
    startReceiving();

    if (resT != RADIOLIB_ERR_NONE) {
        ESP_LOGE(LM_TAG, "Transmit gave error: %d", resT);
        return false;
    }
    return true;
}

void LoraMesher::sendPackets() {
    ESP_LOGV(LM_TAG, "Send routine started");
    vTaskSuspend(NULL);

    int sendCounter = 0;
    uint8_t sendId = 0;
    uint8_t resendMessage = 0;

#ifdef ARDUINO
    randomSeed(getLocalAddress());
#else
    srand(getLocalAddress());
#endif
    const uint8_t dutyCycleEvery = (100 - LM_DUTY_CYCLE) / portTICK_PERIOD_MS;

    for (;;) {
        /* Wait for the notification of new packet has to be sent and enter blocking */
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);

        //truongvv
        // ESP_LOGV(LM_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
        // ESP_LOGV(LM_TAG, "Free heap: %d", getFreeHeap());

        while (ToSendPackets->getLength() > 0) {

            ToSendPackets->setInUse();

            ESP_LOGV(LM_TAG, "Size of Send Packets Queue: %d", ToSendPackets->getLength());

            QueuePacket<Packet<uint8_t>>* tx = ToSendPackets->Pop();

            ToSendPackets->releaseInUse();

            if (tx) {
                ESP_LOGV(LM_TAG, "Send n. %d", sendCounter);

                // Set packet ID only for non-secure packets
                // Secure packets should not have their headers modified after MAC calculation
                if (tx->packet->src == getLocalAddress() && !SecurePacketService::isSecurePacket(tx->packet->type))
                    tx->packet->id = sendId++;

                //If the packet has a data packet and its destination is not broadcast add the via to the packet and forward the packet
                // CRITICAL FIX: Skip via modification for secure packets - via is already set during wrapPacket()
                if (PacketService::isDataPacket(tx->packet->type) && 
                    tx->packet->dst != BROADCAST_ADDR && 
                    !SecurePacketService::isSecurePacket(tx->packet->type)) {
                    
                    uint16_t nextHop = RoutingTableService::getNextHop(tx->packet->dst);

                    //Next hop not found
                    if (nextHop == 0) {
                        ESP_LOGE(LM_TAG, "NextHop Not found from %X, destination %X", tx->packet->src, tx->packet->dst);
                        PacketQueueService::deleteQueuePacketAndPacket(tx);
                        incDestinyUnreachable();
                        continue;
                    }

                    (reinterpret_cast<DataPacket*>(tx->packet))->via = nextHop;
                }

                recordState(LM_StateType::STATE_TYPE_SENT, tx->packet);

                //Send packet
                bool hasSend = sendPacket(tx->packet);

                sendCounter++;

                if (hasSend) {
                    incSendPackets();
                    incSentPayloadBytes(PacketService::getPacketPayloadLengthWithoutControl(tx->packet));
                    incSentControlBytes(PacketService::getControlLength(tx->packet));
                    if (tx->packet->src != getLocalAddress())
                        incForwardedPackets();
                }

                //TODO: If the packet has not been send, add it to the queue and send it again
                if (!hasSend && resendMessage < MAX_RESEND_PACKET) {
                    tx->priority = MAX_PRIORITY;
                    PacketQueueService::addOrdered(ToSendPackets, tx);

                    resendMessage++;
                    continue;
                }

                resendMessage = 0;

                uint32_t timeOnAir = radio->getTimeOnAir(tx->packet->packetSize) / 1000;

                TickType_t delayBetweenSend = timeOnAir * dutyCycleEvery;

                //truongvv
                // ESP_LOGV(LM_TAG, "TimeOnAir %d ms, next message in %d ms", (int) timeOnAir, (int) delayBetweenSend);

                PacketQueueService::deleteQueuePacketAndPacket(tx);

                vTaskDelay(delayBetweenSend / portTICK_PERIOD_MS);
            }
        }
    }
}

void LoraMesher::sendHelloPacket() {
    ESP_LOGV(LM_TAG, "Send Hello Packet routine started");

    vTaskSuspend(NULL);

    size_t maxNodesPerPacket = (PacketFactory::getMaxPacketSize() - sizeof(RoutePacket)) / sizeof(NetworkNode);

    ESP_LOGV(LM_TAG, "Max routing nodes per packet: %d", maxNodesPerPacket);

    //Wait an initial 2 second
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    for (;;) {
        ESP_LOGV(LM_TAG, "Creating Routing Packet");
        ESP_LOGV(LM_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
        ESP_LOGV(LM_TAG, "Free heap: %d", getFreeHeap());

        incSentHelloPackets();

        NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
        size_t numOfNodes = RoutingTableService::routingTableSize();

        size_t numPackets = (numOfNodes + maxNodesPerPacket - 1) / maxNodesPerPacket;
        numPackets = (numPackets == 0) ? 1 : numPackets;

        for (size_t i = 0; i < numPackets; ++i) {
            size_t startIndex = i * maxNodesPerPacket;
            size_t endIndex = startIndex + maxNodesPerPacket;
            if (endIndex > numOfNodes) {
                endIndex = numOfNodes;
            }

            size_t nodesInThisPacket = endIndex - startIndex;

            // Create and send the packet
            RoutePacket* tx = PacketService::createRoutingPacket(
                getLocalAddress(), &nodes[startIndex], nodesInThisPacket, RoleService::getRole()
            );

            // CRITICAL FIX: Check if packet creation failed
            if (tx != nullptr) {
                setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(tx), DEFAULT_PRIORITY + 1);
            } else {
                ESP_LOGE(LM_TAG, "Failed to create Hello packet %d/%d, skipping", i + 1, numPackets);
                // Continue to next packet or cleanup if this is critical
            }
        }

        // Delete the nodes array
        if (numOfNodes > 0)
            delete[] nodes;

        // Wait for HELLO_PACKETS_DELAY seconds or until notified to restart with new interval
        uint32_t helloDelayMs = getCurrentHelloDelay() * 1000;
        ESP_LOGD(LM_TAG, "Hello task waiting %d ms (mode: %d)", helloDelayMs, currentHelloMode);
        
        // Use ulTaskNotifyTake with timeout to allow interruption when mode changes
        ulTaskNotifyTake(pdTRUE, helloDelayMs / portTICK_PERIOD_MS);
    }
}

void LoraMesher::processPackets() {
    ESP_LOGV(LM_TAG, "Process routine started");
    vTaskSuspend(NULL);

    for (;;) {
        //truongvv
        // ESP_LOGV(LM_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
        // ESP_LOGV(LM_TAG, "Free heap: %d", getFreeHeap());

        /* Wait for the notification of receivingRoutine and enter blocking */
        ulTaskNotifyTake(pdPASS, portMAX_DELAY);

        //truongvv
        // ESP_LOGV(LM_TAG, "Size of Received Packets Queue: %d", ReceivedPackets->getLength());

        while (ReceivedPackets->getLength() > 0) {
            QueuePacket<Packet<uint8_t>>* rx = ReceivedPackets->Pop();

            if (rx) {
                uint8_t type = rx->packet->type;

#ifdef LM_TESTING
                if (!shouldProcessPacket(rx->packet)) {
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                    ESP_LOGV(LM_TAG, "TESTING: Packet not for me, deleting it");
                    continue;
                }
#endif

                printHeaderPacket(rx->packet, "received");


                recordState(LM_StateType::STATE_TYPE_RECEIVED, rx->packet);

                incReceivedPayloadBytes(PacketService::getPacketPayloadLengthWithoutControl(rx->packet));
                incReceivedControlBytes(PacketService::getControlLength(rx->packet));

                if (PacketService::isHelloPacket(type)) {
                    incRecHelloPackets();

                    RoutingTableService::processRoute(reinterpret_cast<RoutePacket*>(rx->packet), rx->snr, rx->rssi);
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                else if (PacketService::isDataPacket(type)) {
                    // Handle secure packets
                    QueuePacket<DataPacket>* dataPacketQueue = reinterpret_cast<QueuePacket<DataPacket>*>(rx);
                    
#ifdef ENABLE_MESH_SECURITY
                    // Check if this is a secure packet
                    if (PacketService::isSecurePacket(type)) {
                        // CRITICAL SECURITY FIX: End-to-End Encryption
                        // Only decrypt if this node is the destination
                        // Intermediate nodes must forward encrypted packets unchanged
                        
                        uint16_t dst = rx->packet->dst;
                        uint16_t localAddr = getLocalAddress();
                        
                        if (dst == localAddr || dst == BROADCAST_ADDR) {
                            // This packet is for me - decrypt it
                            ESP_LOGD(LM_TAG, "Secure packet for me (dst=0x%04X), decrypting...", dst);
                            
                            // Convert to secure packet and unwrap
                            SecureDataPacket* securePacket = reinterpret_cast<SecureDataPacket*>(rx->packet);
                            size_t originalSize;
                            
                            DataPacket* decryptedPacket = SecurePacketService::unwrapPacket(securePacket, &originalSize);
                            if (decryptedPacket) {
                                ESP_LOGI(LM_TAG, "Packet decrypted successfully");
                                
                                // CRITICAL FIX: Log heap status before queue packet creation
                                ESP_LOGD(LM_TAG, "Free heap before createQueuePacket: %d bytes", esp_get_free_heap_size());
                                
                                // CRITICAL FIX: Pass RSSI and SNR when creating queue packet
                                // This avoids NULL pointer issues later when accessing these fields
                                QueuePacket<DataPacket>* decryptedQueue = PacketQueueService::createQueuePacket(
                                    decryptedPacket,    // No need for reinterpret_cast, it's already DataPacket*
                                    rx->priority,       // Priority
                                    0,                  // Number (not used for single packets)
                                    rx->rssi,           // RSSI from original packet
                                    rx->snr             // SNR from original packet
                                );
                                
                                // CRITICAL FIX: Check if createQueuePacket failed
                                if (decryptedQueue == nullptr) {
                                    ESP_LOGE(LM_TAG, "Failed to create queue packet for decrypted data (free heap: %d)",
                                             esp_get_free_heap_size());
                                    // Clean up decrypted packet to avoid memory leak
                                    vPortFree(decryptedPacket);
                                    // Clean up original secure packet
                                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                                } else {
                                    // Log to debug
                                    ESP_LOGD(LM_TAG, "Decrypted packet queued successfully (free heap: %d)",
                                             esp_get_free_heap_size());
                                    // No need to set SNR again - already set in createQueuePacket
                                    
                                    // Process decrypted packet
                                    processDataPacket(decryptedQueue);
                                    
                                    // Clean up original secure packet
                                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                                }
                            } else {
                                ESP_LOGW(LM_TAG, "Failed to decrypt packet, dropping");
                                PacketQueueService::deleteQueuePacketAndPacket(rx);
                            }
                        } else {
                            // This packet is NOT for me - forward encrypted unchanged
                            // CRITICAL: Do NOT decrypt! This ensures end-to-end encryption
                            ESP_LOGI(LM_TAG, "🔒 Secure packet for 0x%04X (not for me), forwarding ENCRYPTED", dst);
                            
                            // Forward encrypted packet directly using processDataPacket
                            // The packet will be routed based on via field (set during wrapPacket)
                            processDataPacket(dataPacketQueue);
                        }
                    } else {
                        // Regular unencrypted packet
                        // SIMPLIFIED: No complex provisioning payload detection
                        // All provisioning is handled via netkey distribution
                        processDataPacket(dataPacketQueue);
                    }
#else
                    // No security, process normally
                    processDataPacket(dataPacketQueue);
#endif
                }
                // Layer 2: Handle RESYNC security packets
                // Security packets are detected by examining packet content rather than type
                else if (isSecurityResyncPacket(rx->packet)) {
                    ESP_LOGI(LM_TAG, "Security resync packet received from 0x%04X", rx->packet->src);
                    
                    // Process security resync packet 
                    bool handled = processSecurityResyncPacket((uint8_t*)rx->packet, rx->packet->packetSize, rx->packet->src);
                    
                    if (handled) {
                        ESP_LOGI(LM_TAG, "Security resync packet processed successfully");
                    } else {
                        ESP_LOGW(LM_TAG, "Failed to process security resync packet");
                    }
                    
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                else if (NetkeyDistributionService::isNetkeyPacket(type)) {
                    ESP_LOGI(LM_TAG, "Netkey packet received from 0x%04X", rx->packet->src);
                    
                    // Process netkey packet
                    bool handled = NetkeyDistributionService::processNetkeyPacket(
                        (uint8_t*)rx->packet,
                        rx->packet->packetSize,
                        rx->packet->src
                    );
                    
                    if (handled) {
                        ESP_LOGD(LM_TAG, "Netkey packet processed successfully");
                    } else {
                        ESP_LOGW(LM_TAG, "Failed to process netkey packet");
                    }
                    
                    // Clean up packet
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                else if (PacketService::isRouteRequestPacket(type)) {
                    // SIMPLIFIED: No longer using route discovery service
                    // Bridge uses routing table directly from HELLO packets
                    ESP_LOGD(LM_TAG, "Route request packet ignored - using HELLO-based routing only");
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                else if (PacketService::isRouteReplyPacket(type)) {
                    // SIMPLIFIED: No longer using route discovery service  
                    // Bridge uses routing table directly from HELLO packets
                    ESP_LOGD(LM_TAG, "Route reply packet ignored - using HELLO-based routing only");
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                // Handle hello mode control packets from Bridge
                // Purpose: Bridge can remotely trigger fast discovery mode during provisioning
                else if (type == HELLO_MODE_CONTROL_P) {
                    ESP_LOGI(LM_TAG, "Hello mode control packet received from 0x%04X", rx->packet->src);
                    
                    ControlPacket* controlPacket = reinterpret_cast<ControlPacket*>(rx->packet);
                    
                    // Calculate payload size: packetSize - header_size
                    size_t headerSize = sizeof(ControlPacket);
                    size_t payloadSize = (controlPacket->packetSize > headerSize) ? 
                                        (controlPacket->packetSize - headerSize) : 0;
                    
                    // Validate payload size
                    if (payloadSize < sizeof(HelloModeControlPayload)) {
                        ESP_LOGW(LM_TAG, "Invalid hello mode control payload size: %zu (expected %zu)", 
                                 payloadSize, sizeof(HelloModeControlPayload));
                        PacketQueueService::deleteQueuePacketAndPacket(rx);
                        continue;
                    }
                    
                    HelloModeControlPayload* payload = (HelloModeControlPayload*)controlPacket->payload;
                    HelloMode targetMode = payload->targetMode;
                    uint32_t durationMs = payload->durationMs;
                    
                    ESP_LOGI(LM_TAG, "Applying hello mode change: mode=%d, duration=%dms", targetMode, durationMs);
                    
                    // Apply the mode change
                    if (targetMode == HELLO_MODE_FAST_DISCOVERY) {
                        startFastDiscoveryMode(durationMs);
                    } else if (targetMode == HELLO_MODE_NORMAL) {
                        stopFastDiscoveryMode();
                    } else {
                        ESP_LOGW(LM_TAG, "Unknown hello mode: %d", targetMode);
                    }
                    
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
                // SIMPLIFIED: No provisioning packet handling
                // All provisioning replaced by direct netkey distribution
                else {
                    ESP_LOGV(LM_TAG, "Packet not identified, deleting it");
                    incReceivedNotForMe();
                    PacketQueueService::deleteQueuePacketAndPacket(rx);
                }
            }
        }
    }
}

void LoraMesher::routingTableManager() {
    ESP_LOGV(LM_TAG, "Routing Table Manager routine started");
    vTaskSuspend(NULL);

    unsigned long lastPrintTime = 0;
    unsigned long lastTimeoutCheckTime = 0;
    const unsigned long PRINT_INTERVAL_MS = 30000;  // Print routing table every 30 seconds
    // CRITICAL FIX: Check timeout frequently (every 60s) instead of every DEFAULT_TIMEOUT (3000s = 50min!)
    // This ensures expired nodes are removed promptly when TTL reaches 0
    const unsigned long TIMEOUT_CHECK_INTERVAL_MS = 60000;  // Check timeout every 60 seconds

    for (;;) {
        unsigned long currentTime = millis();

        // Print routing table every 30 seconds (independent of timeout check)
        if (currentTime - lastPrintTime >= PRINT_INTERVAL_MS) {
            // CRITICAL DEBUG: Log stack and heap BEFORE operations that might crash
            UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(NULL);
            uint32_t freeHeap = esp_get_free_heap_size();
            
            ESP_LOGI(LM_TAG, "=== Periodic Routing Table Display (every 30s) ===");
            ESP_LOGD(LM_TAG, "Stack free: %u bytes, Heap free: %u bytes", stackHighWater, freeHeap);
            
            // CRITICAL: Check for stack overflow danger
            if (stackHighWater < 512) {
                ESP_LOGE(LM_TAG, "⚠️ STACK OVERFLOW DANGER! Only %u bytes free!", stackHighWater);
            }
            if (freeHeap < 10000) {
                ESP_LOGW(LM_TAG, "⚠️ LOW HEAP! Only %u bytes free", freeHeap);
            }
            
            RoutingTableService::printRoutingTable();
            lastPrintTime = currentTime;
            
            // CRITICAL DEBUG: Log after print to detect if crash happens during print
            ESP_LOGD(LM_TAG, "Routing table print completed successfully");
        }

        // Check for timeout and remove inactive nodes every DEFAULT_TIMEOUT
        if (currentTime - lastTimeoutCheckTime >= TIMEOUT_CHECK_INTERVAL_MS) {
            // TODO: If the routing table removes a node, remove the nodes from the Q_WSP and Q_WRP
            RoutingTableService::manageTimeoutRoutingTable();
            lastTimeoutCheckTime = currentTime;
        }
        
        // SIMPLIFIED: No longer using route discovery service, routing is purely HELLO-based
        
        // Record the state for the simulation (with safety checks)
        recordState(LM_StateType::STATE_TYPE_MANAGER);

        // CRITICAL DEBUG: Verify task is still healthy before delay
        ESP_LOGV(LM_TAG, "Stack free: %u", uxTaskGetStackHighWaterMark(NULL));

        // Use shorter delay to allow more frequent checks (1 second)
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    // Should never reach here
    ESP_LOGE(LM_TAG, "RoutingTableManager exited loop - THIS SHOULD NOT HAPPEN!");
}

void LoraMesher::queueManager() {
    ESP_LOGV(LM_TAG, "Queue Manager routine started");
    vTaskSuspend(NULL);

    for (;;) {
        //truongvv
        // ESP_LOGV(LM_TAG, "Stack space unused after entering the task: %d", uxTaskGetStackHighWaterMark(NULL));
        // ESP_LOGV(LM_TAG, "Free heap: %d", getFreeHeap());

        // Record the state for the simulation
        recordState(LM_StateType::STATE_TYPE_MANAGER);

        if (q_WSP->getLength() == 0 && q_WRP->getLength() == 0) {
            ESP_LOGV(LM_TAG, "No packets to send or received");

            // Wait for the notification of send or receive reliable message and enter blocking
            ulTaskNotifyTake(
                pdTRUE,
                portMAX_DELAY);
            continue;
        }

        managerReceivedQueue();
        managerSendQueue();

        // TODO: Calculate the min timeout for the queue manager, get the min timeout from the queues
        vTaskDelay(MIN_TIMEOUT * 1000 / portTICK_PERIOD_MS);
    }
}

void LoraMesher::printHeaderPacket(Packet<uint8_t>* p, String title) {
    bool isDataPacket = PacketService::isDataPacket(p->type);
    bool isControlPacket = PacketService::isControlPacket(p->type);

    ESP_LOGV(LM_TAG, "Packet %s -- Size: %d Src: %X Dst: %X Id: %d Type: %d Via: %X Seq_Id: %d Num: %d",
        title.c_str(),
        p->packetSize,
        p->src,
        p->dst,
        p->id,
        p->type,
        isDataPacket ? (reinterpret_cast<DataPacket*>(p))->via : 0,
        isControlPacket ? (reinterpret_cast<ControlPacket*>(p))->seq_id : 0,
        isControlPacket ? (reinterpret_cast<ControlPacket*>(p))->number : 0);
}

void LoraMesher::sendReliablePacket(uint16_t dst, uint8_t* payload, uint32_t payloadSize) {
    // Cannot send an empty packet
    if (payloadSize == 0)
        return;
    if (dst == BROADCAST_ADDR) {
        ESP_LOGW(LM_TAG, "Be aware of sending a reliable packet to the broadcast address");
        size_t numOfNodes = RoutingTableService::routingTableSize();
        if (numOfNodes > 0) {
            NetworkNode* nodes = RoutingTableService::getAllNetworkNodes();
            for (size_t i = 0; i < numOfNodes; i++) {
                NetworkNode* node = &nodes[i];
                sendReliablePacket(node->address, payload, payloadSize);
            }
            delete[] nodes;
        }
        return;
    }
    ESP_LOGV(LM_TAG, "Sending reliable payload with %d bytes to %X", (int) payloadSize, dst);

    // Get the Routing Table node of the destination
    RouteNode* node = RoutingTableService::findNode(dst);

    if (node == NULL) {
        ESP_LOGV(LM_TAG, "Destination not found in the routing table");
        return;
    }

    //Generate a sequence Id for this list of packets
    uint8_t seq_id = getSequenceId();

    //Get the Type of the packet
    uint8_t type = NEED_ACK_P | XL_DATA_P;

    //Max payload size per packet
    size_t maxPayloadSize = PacketService::getMaximumPayloadLength(type);

    //Number of packets
    uint16_t numOfPackets = payloadSize / maxPayloadSize + (payloadSize % maxPayloadSize > 0);

    //Create a new Linked list to store the QueuePackets and the payload
    LM_LinkedList<QueuePacket<ControlPacket>>* packetList = new LM_LinkedList<QueuePacket<ControlPacket>>();

    //Add the SYNC configuration packet
    packetList->Append(getStartSequencePacketQueue(dst, seq_id, numOfPackets));


    for (uint16_t i = 1; i <= numOfPackets; i++) {
        //Get the position of the payload
        uint8_t* payloadToSend = reinterpret_cast<uint8_t*>((unsigned long) payload + ((i - 1) * maxPayloadSize));

        //Get the payload Size in bytes
        size_t payloadSizeToSend = maxPayloadSize;
        if (i == numOfPackets)
            payloadSizeToSend = payloadSize - (maxPayloadSize * (numOfPackets - 1));

        ESP_LOGV(LM_TAG, "Payload Size: %d", payloadSizeToSend);

        //Create a new packet with the previous payload
        ControlPacket* cPacket = PacketService::createControlPacket(dst, getLocalAddress(), type, payloadToSend, payloadSizeToSend);
        cPacket->number = i;
        cPacket->seq_id = seq_id;

        //Create a packet queue
        QueuePacket<ControlPacket>* pq = PacketQueueService::createQueuePacket(cPacket, DEFAULT_PRIORITY + 1, i);

        //Append the packet queue in the linked list
        packetList->Append(pq);
    }

    //Create the pair of configuration
    listConfiguration* listConfig = new listConfiguration();
    listConfig->config = new sequencePacketConfig(seq_id, dst, numOfPackets, node);
    listConfig->list = packetList;

    // Set the RTT of the first packet of the sequence
    listConfig->config->calculatingRTT = millis();

    // Set the timeout of the first packet of the sequence
    addTimeout(listConfig->config);

    //Add dataList pair to the waiting send packets queue
    q_WSP->setInUse();
    q_WSP->Append(listConfig);
    q_WSP->releaseInUse();

    //Send the first packet of the sequence (SYNC packet)
    sendPacketSequence(listConfig, 0);

    // Notify the queueManager that a new sequence has been started
    notifyNewSequenceStarted();
}

void LoraMesher::processDataPacket(QueuePacket<DataPacket>* pq) {
    DataPacket* packet = pq->packet;

    incReceivedDataPackets();

    //truongvv
    // ESP_LOGI(LM_TAG, "Data packet from %X, destination %X, via %X", packet->src, packet->dst, packet->via);

    if (packet->dst == getLocalAddress()) {
        ESP_LOGV(LM_TAG, "Data packet from %X for me", packet->src);
        incDataPacketForMe();

        processDataPacketForMe(pq);
        return;

    }
    else if (packet->dst == BROADCAST_ADDR) {
        ESP_LOGV(LM_TAG, "Data packet from %X BROADCAST", packet->src);
        incReceivedBroadcast();
        processDataPacketForMe(pq);
        return;

    }
    else if (packet->via == getLocalAddress()) {
        ESP_LOGV(LM_TAG, "Data Packet from %X for %X. Via is me. Forwarding it", packet->src, packet->dst);
        incReceivedIAmVia();
        addToSendOrderedAndNotify(reinterpret_cast<QueuePacket<Packet<uint8_t>>*>(pq));
        return;
    }

    ESP_LOGV(LM_TAG, "Packet not for me, deleting it");
    incReceivedNotForMe();
    PacketQueueService::deleteQueuePacketAndPacket(pq);
}

void LoraMesher::processDataPacketForMe(QueuePacket<DataPacket>* pq) {
    DataPacket* p = pq->packet;
    ControlPacket* cPacket = reinterpret_cast<ControlPacket*>(p);

    //By default, delete the packet queue at the finish of this function
    bool deleteQueuePacket = true;

    bool needAck = PacketService::isNeedAckPacket(p->type);

    if (PacketService::isOnlyDataPacket(p->type)) {
        ESP_LOGV(LM_TAG, "Data Packet received");
        //Convert the packet into a user packet
        AppPacket<uint8_t>* appPacket = PacketService::convertPacket(p);

        //Add and notify the user of this packet
        notifyUserReceivedPacket(appPacket);
    }
    else if (PacketService::isAckPacket(p->type)) {
        ESP_LOGV(LM_TAG, "ACK Packet received");
        addAck(p->src, cPacket->seq_id, cPacket->number);
    }
    else if (PacketService::isLostPacket(p->type)) {
        ESP_LOGV(LM_TAG, "Lost Packet received");
        processLostPacket(p->src, cPacket->seq_id, cPacket->number);
    }
    else if (PacketService::isSyncPacket(p->type)) {
        ESP_LOGV(LM_TAG, "Synchronization Packet received");
        processSyncPacket(p->src, cPacket->seq_id, cPacket->number);

        needAck = false;
    }
    else if (PacketService::isXLPacket(p->type)) {
        ESP_LOGV(LM_TAG, "Large payload Packet received");
        processLargePayloadPacket(reinterpret_cast<QueuePacket<ControlPacket>*>(pq));
        needAck = false;
        deleteQueuePacket = false;
    }

    //Need ack
    if (needAck) {
        //TODO: All packets with this ack will ack?
        ESP_LOGV(LM_TAG, "Previous packet need an ACK");
        sendAckPacket(p->src, cPacket->seq_id, cPacket->number);
    }

    if (deleteQueuePacket)
        //Delete packet queue
        PacketQueueService::deleteQueuePacketAndPacket(pq);
}

void LoraMesher::notifyUserReceivedPacket(AppPacket<uint8_t>* appPacket) {
    if (ReceiveAppData_TaskHandle) {
        ReceivedAppPackets->setInUse();
        //Add the packet inside the receivedUsers Queue
        ReceivedAppPackets->Append(appPacket);

        ReceivedAppPackets->releaseInUse();

        // Notify the received user task handle by incrementing its notification value.
        // Previously the code used xTaskNotify(..., 0, eSetValueWithOverwrite) which
        // sets the task notification value to 0 and prevents ulTaskNotifyTake from
        // unblocking (it waits for a non-zero value). Use xTaskNotifyGive to increment
        // the notification count so ulTaskNotifyTake() will return.
        xTaskNotifyGive(ReceiveAppData_TaskHandle);

    }
    else
        deletePacket(appPacket);
}

uint32_t LoraMesher::getPropagationTimeWithRandom(uint8_t multiplayer) {
    // TODO: Use the RTT or other congestion metrics to calculate the time, timeouts...
    uint32_t time = getMaxPropagationTime();
    uint32_t randomTime = random(time, time * 3 + (multiplayer + routingTableSize()) * 100);
    return randomTime;
}

void LoraMesher::recalculateMaxTimeOnAir() {
    maxTimeOnAir = radio->getTimeOnAir(PacketFactory::getMaxPacketSize()) / 1000;
    ESP_LOGV(LM_TAG, "Max Time on Air changed %d ms", (int) maxTimeOnAir);
}

void LoraMesher::recordState(LM_StateType type, Packet<uint8_t>* packet) {
    // CRITICAL FIX: Validate simulatorService pointer before use
    if (simulatorService == nullptr)
        return;
    
    // CRITICAL FIX: Validate all queue pointers before accessing
    if (ReceivedPackets == nullptr || q_WRP == nullptr || q_WSP == nullptr) {
        ESP_LOGW(LM_TAG, "recordState: Queue pointer is NULL, skipping");
        return;
    }

    // Safe to call now
    simulatorService->addState(ReceivedPackets->getLength(), getSendQueueSize(),
        getReceivedQueueSize(), routingTableSize(), q_WRP->getLength(), q_WSP->getLength(),
        type, packet);
}

#ifdef LM_TESTING
bool LoraMesher::canReceivePacket(uint16_t source) {
    return true;
}
#endif

#ifdef LM_TESTING
bool LoraMesher::isDataPacketAndLocal(DataPacket* packet, uint16_t localAddress) {
    return PacketService::isDataPacket(packet->type) && packet->via == localAddress;
}

bool LoraMesher::shouldProcessPacket(Packet<uint8_t>* packet) {
    return isDataPacketAndLocal(reinterpret_cast<DataPacket*>(packet), getLocalAddress()) || canReceivePacket(packet->src);
}
#endif

/**
 *  End Region Packet Service
**/


/**
 *  Region Routing Table
**/

size_t LoraMesher::routingTableSize() {
    return RoutingTableService::routingTableSize();
}

/**
 *  End Region Routing Table
**/


/**
 *  Region PacketQueue
**/

size_t LoraMesher::getReceivedQueueSize() {
    return ReceivedAppPackets->getLength();
}

size_t LoraMesher::getSendQueueSize() {
    return ToSendPackets->getLength();
}

void LoraMesher::addToSendOrderedAndNotify(QueuePacket<Packet<uint8_t>>* qp) {
    PacketQueueService::addOrdered(ToSendPackets, qp);
    ESP_LOGI(LM_TAG, "Added packet to Q_SP, notifying sender task");

    //Notify the sendData task handle
    xTaskNotify(SendData_TaskHandle, 0, eSetValueWithOverwrite);
}

void LoraMesher::notifyNewSequenceStarted() {
    //Notify the sendData task handle
    xTaskNotify(QueueManager_TaskHandle, 0, eSetValueWithOverwrite);
}

/**
 *  End Region PacketQueue
**/


/**
 * Large and Reliable payloads
 */

QueuePacket<ControlPacket>* LoraMesher::getStartSequencePacketQueue(uint16_t destination, uint8_t seq_id, uint16_t num_packets) {
    uint8_t type = SYNC_P | NEED_ACK_P | XL_DATA_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, num_packets);

    //Create a packet queue
    return PacketQueueService::createQueuePacket(cPacket, DEFAULT_PRIORITY, 0);
}

void LoraMesher::sendAckPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = ACK_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, seq_num);

    setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(cPacket), DEFAULT_PRIORITY + 3);
}

void LoraMesher::sendLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    uint8_t type = LOST_P;

    //Create the packet
    ControlPacket* cPacket = PacketService::createEmptyControlPacket(destination, getLocalAddress(), type, seq_id, seq_num);

    setPackedForSend(reinterpret_cast<Packet<uint8_t>*>(cPacket), DEFAULT_PRIORITY + 2);
}

bool LoraMesher::sendPacketSequence(listConfiguration* lstConfig, uint16_t seq_num) {
    // Check if the sequence number requested is valid
    if (lstConfig->config->lastAck > seq_num) {
        ESP_LOGE(LM_TAG, "Trying to send packet sequence previously acknowledged Seq_id: %d, Num: %d", lstConfig->config->seq_id, seq_num);
        return false;
    }

    //Get the packet queue with the sequence number
    QueuePacket<ControlPacket>* pq = PacketQueueService::findPacketQueue(lstConfig->list, seq_num);

    if (pq == nullptr) {
        ESP_LOGE(LM_TAG, "NOT FOUND the packet queue with Seq_id: %d, Num: %d", lstConfig->config->seq_id, seq_num);
        return false;
    }

    //Create the packet
    Packet<uint8_t>* p = PacketService::copyPacket(pq->packet, pq->packet->getPacketLength());

    //Add the packet to the send queue
    setPackedForSend(p, DEFAULT_PRIORITY);

    return true;
}

void LoraMesher::addAck(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        ESP_LOGE(LM_TAG, "NOT FOUND the sequence packet config in add ack with Seq_id: %d, Source: %d", seq_id, source);
        return;
    }

    //If all packets has been arrived to the destiny
    //Delete this sequence
    if (config->config->number == seq_num) {
        ESP_LOGI(LM_TAG, "All the packets has been arrived to the seq_Id: %d", seq_id);
        findAndClearLinkedList(q_WSP, config);
        return;
    }

    if (config->config->lastAck > seq_num) {
        ESP_LOGE(LM_TAG, "ACK received that has been yet acknowledged Seq_id: %d, Num: %d", config->config->seq_id, seq_num);
        return;
    }

    //Set has been received some ACK
    config->config->firstAckReceived = 1;

    // TODO: Check for repeated ACKs and packets.

    //Add the last ack to the config packet
    config->config->lastAck = seq_num;

    // Recalculate the RTT
    actualizeRTT(config->config);

    //Reset the timeouts
    resetTimeout(config->config);

    ESP_LOGV(LM_TAG, "Sending next packet after receiving an ACK");

    //Send the next packet sequence
    sendPacketSequence(config, seq_num + 1);
}

bool LoraMesher::processLargePayloadPacket(QueuePacket<ControlPacket>* pq) {
    ControlPacket* cPacket = pq->packet;

    listConfiguration* configList = findSequenceList(q_WRP, cPacket->seq_id, cPacket->src);
    if (configList == nullptr) {
        ESP_LOGE(LM_TAG, "NOT FOUND the sequence packet config in Process Large Payload with Seq_id: %d, Source: %d", cPacket->seq_id, cPacket->src);
        PacketQueueService::deleteQueuePacketAndPacket(pq);
        return false;
    }

    if (configList->config->lastAck + 1 != cPacket->number) {
        ESP_LOGE(LM_TAG, "Sequence number received in bad order in seq_Id: %d, received: %d expected: %d", cPacket->seq_id, cPacket->number, configList->config->lastAck + 1);
        sendLostPacket(cPacket->src, cPacket->seq_id, configList->config->lastAck + 1);

        PacketQueueService::deleteQueuePacketAndPacket(pq);
        return false;
    }

    configList->config->lastAck++;

    configList->list->setInUse();
    configList->list->Append(pq);
    configList->list->releaseInUse();

    //Send ACK
    sendAckPacket(cPacket->src, cPacket->seq_id, cPacket->number);

    // Recalculate the RTT
    actualizeRTT(configList->config);

    // Reset the timeouts
    resetTimeout(configList->config);

    //All packets has been arrived, join them and send to the user
    if (configList->config->lastAck == configList->config->number) {
        joinPacketsAndNotifyUser(configList);
        return true;
    }

    return true;
}

void LoraMesher::joinPacketsAndNotifyUser(listConfiguration* listConfig) {
    ESP_LOGV(LM_TAG, "Joining packets seq_Id: %d Src: %X", listConfig->config->seq_id, listConfig->config->source);

    LM_LinkedList<QueuePacket<ControlPacket>>* list = listConfig->list;

    list->setInUse();
    if (!list->moveToStart()) {
        ESP_LOGE(LM_TAG, "CRITICAL: List is empty in joinPacketsAndNotifyUser");
        list->releaseInUse();
        return;
    }

    //TODO: getPacketPayloadLength could be done when adding the packets inside the list
    size_t payloadSize = 0;
    size_t number = 1;

    do {
        // CRITICAL FIX: Check if getCurrent() returns NULL before dereferencing
        QueuePacket<ControlPacket>* currentQueuePacket = list->getCurrent();
        if (currentQueuePacket == nullptr) {
            ESP_LOGE(LM_TAG, "CRITICAL: NULL queue packet in joinPacketsAndNotifyUser at position %d", number);
            list->releaseInUse();
            return;
        }
        
        ControlPacket* currentP = currentQueuePacket->packet;
        if (currentP == nullptr) {
            ESP_LOGE(LM_TAG, "CRITICAL: NULL control packet in joinPacketsAndNotifyUser at position %d", number);
            list->releaseInUse();
            return;
        }

        if (number != (currentP->number))
            //TODO: ORDER THE PACKETS if they are not ordered?
            ESP_LOGE(LM_TAG, "Wrong packet order");

        number++;
        payloadSize += PacketService::getPacketPayloadLength(currentP);
    } while (list->next());

    //Move to start again
    list->moveToStart();

    // CRITICAL FIX: Check getCurrent() again after moveToStart()
    QueuePacket<ControlPacket>* firstQueuePacket = list->getCurrent();
    if (firstQueuePacket == nullptr || firstQueuePacket->packet == nullptr) {
        ESP_LOGE(LM_TAG, "CRITICAL: NULL packet after moveToStart in joinPacketsAndNotifyUser");
        list->releaseInUse();
        return;
    }

    uint32_t appPacketLength = sizeof(AppPacket<uint8_t>);

    //Packet length = size of the packet + size of the payload
    uint32_t packetLength = appPacketLength + payloadSize;

    AppPacket<uint8_t>* p = static_cast<AppPacket<uint8_t>*>(pvPortMalloc(packetLength));

    // CRITICAL FIX: Check if malloc failed BEFORE using p
    if (p == nullptr) {
        ESP_LOGE(LM_TAG, "CRITICAL: Failed to allocate %d bytes for joined packet (free heap: %d)",
                 packetLength, esp_get_free_heap_size());
        list->releaseInUse();
        return;
    }

    ESP_LOGV(LM_TAG, "Large Packet Packet length: %d Payload Size: %d", (int) packetLength, payloadSize);

    //Copy the payload into the packet
    unsigned long actualPayloadSizeDst = appPacketLength;

    do {
        QueuePacket<ControlPacket>* currentQueuePacket = list->getCurrent();
        if (currentQueuePacket == nullptr || currentQueuePacket->packet == nullptr) {
            ESP_LOGE(LM_TAG, "CRITICAL: NULL packet during copy in joinPacketsAndNotifyUser");
            vPortFree(p);
            list->releaseInUse();
            return;
        }
        
        ControlPacket* currentP = currentQueuePacket->packet;

        size_t actualPayloadSizeSrc = PacketService::getPacketPayloadLength(currentP);

        memcpy(reinterpret_cast<void*>((unsigned long) p + (actualPayloadSizeDst)), currentP->payload, actualPayloadSizeSrc);
        actualPayloadSizeDst += actualPayloadSizeSrc;
    } while (list->next());

    list->releaseInUse();

    //Set values to the AppPacket
    p->payloadSize = payloadSize;
    p->src = listConfig->config->source;
    p->dst = getLocalAddress();

    //TODO: When finished, clear everything? Or maintain the config until timeout?
    findAndClearLinkedList(q_WRP, listConfig);

    notifyUserReceivedPacket(p);
}

void LoraMesher::processSyncPacket(uint16_t source, uint8_t seq_id, uint16_t seq_num) {
    //Check for repeated sequence lists
    listConfiguration* listConfig = findSequenceList(q_WRP, seq_id, source);

    if (listConfig == nullptr) {
        // Get the Routing Table node of the destination
        RouteNode* node = RoutingTableService::findNode(source);

        if (node == nullptr) {
            ESP_LOGW(LM_TAG, "Node not found in the routing table");
            return;
        }

        //Create the pair of configuration
        listConfig = new listConfiguration();
        listConfig->config = new sequencePacketConfig(seq_id, source, seq_num, node);
        listConfig->list = new LM_LinkedList<QueuePacket<ControlPacket>>();

        // Starting to calculate RTT
        actualizeRTT(listConfig->config);

        //Add list configuration to the waiting received packets queue
        q_WRP->setInUse();
        q_WRP->Append(listConfig);
        q_WRP->releaseInUse();

        // Reset the timeout
        addTimeout(listConfig->config);

        // Notify the queueManager that a new sequence has been started
        notifyNewSequenceStarted();

        //Change the number to send the ack to the correct one
        //cPacket->number in SYNC_P specify the number of packets and it needs to ACK the 0
        sendAckPacket(source, seq_id, 0);
    }
}

void LoraMesher::processLostPacket(uint16_t destination, uint8_t seq_id, uint16_t seq_num) {
    //Find the list config
    listConfiguration* listConfig = findSequenceList(q_WSP, seq_id, destination);

    if (listConfig == nullptr) {
        ESP_LOGE(LM_TAG, "NOT FOUND the sequence packet config in ost packet with Seq_id: %d, Source: %d", seq_id, destination);
        return;
    }

    //TODO: Check for duplicate consecutive lost packets, set a timeout to resend the lost packet.
    // Recalculate the RTT
    actualizeRTT(listConfig->config);

    // Reset the timeout
    resetTimeout(listConfig->config);

    // First ack received is set to 1, this counts as a ack received. 
    // If the first sync is received but the first ack is not, then the receiver will send a first lost packet.
    listConfig->config->firstAckReceived = 1;

    //Send the packet sequence that has been lost
    if (sendPacketSequence(listConfig, seq_num)) {
        listConfig->config->numberOfTimeouts++;
        //Reset the timeout of this sequence packets inside the q_WSP
        recalculateTimeoutAfterTimeout(listConfig->config);
    }
}

void LoraMesher::addTimeout(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    listConfiguration* config = findSequenceList(q_WSP, seq_id, source);
    if (config == nullptr) {
        ESP_LOGE(LM_TAG, "NOT FOUND the sequence packet config in add timeout with Seq_id: %d, Source: %d", seq_id, source);
        return;
    }

    addTimeout(config->config);
}

void LoraMesher::resetTimeout(sequencePacketConfig* configPacket) {
    configPacket->numberOfTimeouts = 0;
    addTimeout(configPacket);
}

void LoraMesher::actualizeRTT(sequencePacketConfig* config) {
    if (config->calculatingRTT == 0) {
        //Set the first RTT received time
        config->calculatingRTT = millis();
        ESP_LOGV(LM_TAG, "Starting to calculate RTT seq_Id: %d Src: %X",
            config->seq_id, config->source);
        return;
    }

    RouteNode* node = config->node;

    if (node == nullptr) {
        ESP_LOGW(LM_TAG, "Node not found in the routing table");
        return;
    }

    unsigned long actualRTT = millis() - config->calculatingRTT;

    // First time RTT is calculated for this node (RFC 6298)
    if (node->SRTT == 0) {
        node->SRTT = actualRTT;
        node->RTTVAR = actualRTT / 2;
    }
    else {
        unsigned long absRTT = (node->SRTT > actualRTT) ? (node->SRTT - actualRTT) : (actualRTT - node->SRTT);
        node->RTTVAR = std::min((node->RTTVAR * 3 + absRTT) / 4, 100000UL);
        node->SRTT = std::min((node->SRTT * 7 + actualRTT) / 8, 100000UL);
    }

    config->calculatingRTT = millis();

    ESP_LOGV(LM_TAG, "Updating RTT (%u ms), SRTT (%u), RTTVAR (%u) seq_Id: %d Src: %X",
        (unsigned int) actualRTT, (unsigned int) node->SRTT, (unsigned int) node->RTTVAR, config->seq_id, config->source);
}

void LoraMesher::clearLinkedList(listConfiguration* listConfig) {
    LM_LinkedList<QueuePacket<ControlPacket>>* list = listConfig->list;
    list->setInUse();
    ESP_LOGI(LM_TAG, "Clearing list configuration Seq_Id: %d Src: %X", listConfig->config->seq_id, listConfig->config->source);

    size_t listSize = list->getLength();

    ESP_LOGV(LM_TAG, "List size: %d", listSize);

    for (int i = 0; i < listSize; i++) {
        QueuePacket<ControlPacket>* current = list->getCurrent();
        
        // CRITICAL FIX: Check if getCurrent() returned NULL
        if (current == nullptr) {
            ESP_LOGW(LM_TAG, "NULL queue packet at position %d during clearLinkedList, skipping", i);
            list->DeleteCurrent(); // Still need to advance
            continue;
        }
        
        PacketQueueService::deleteQueuePacketAndPacket(current);
        list->DeleteCurrent();
    }

    delete list;
    delete listConfig->config;
    delete listConfig;
}

void LoraMesher::findAndClearLinkedList(LM_LinkedList<listConfiguration>* queue, listConfiguration* listConfig) {
    queue->setInUse();

    if (!queue->Search(listConfig)) {
        ESP_LOGE(LM_TAG, "Not found list config");
        queue->releaseInUse();
    }

    clearLinkedList(listConfig);

    queue->DeleteCurrent();

    queue->releaseInUse();
}

LoraMesher::listConfiguration* LoraMesher::findSequenceList(LM_LinkedList<listConfiguration>* queue, uint8_t seq_id, uint16_t source) {
    queue->setInUse();

    if (queue->moveToStart()) {
        do {
            listConfiguration* current = queue->getCurrent();

            if (current->config->seq_id == seq_id && current->config->source == source) {
                queue->releaseInUse();
                return current;
            }

        } while (queue->next());
    }

    queue->releaseInUse();

    return nullptr;

}

void LoraMesher::managerReceivedQueue() {
    managerTimeouts(q_WRP, QueueType::WRP);
}

void LoraMesher::managerSendQueue() {
    managerTimeouts(q_WSP, QueueType::WSP);
}

void LoraMesher::managerTimeouts(LM_LinkedList<listConfiguration>* queue, QueueType type) {
    String queueName;
    if (type == QueueType::WRP) {
        queueName = F("Waiting Received Queue");
    }
    else {
        queueName = F("Waiting Send Queue");
    }

    ESP_LOGV(LM_TAG, "Checking %s timeouts. Open connections %d", queueName.c_str(), queue->getLength());

    queue->setInUse();

    if (queue->moveToStart()) {
        do {
            listConfiguration* current = queue->getCurrent();

            // Get Config packet
            sequencePacketConfig* configPacket = current->config;

            // If Config packet has reached timeout
            if (configPacket->timeout < millis()) {
                // Increment number of timeouts
                configPacket->numberOfTimeouts++;

                // Description of the timeout:
                // The number of the packet would be the following: 
                // If it is a sender it starts from 0 to n + 1 packets, that includes the sync packet: If num = 0, it is that the sync packet has been lost, if num > 0, it is that the packet num - 1 has been lost
                // For the the receiver it starts from 0 to n packets
                ESP_LOGW(LM_TAG, "%s timeout reached, Src: %X, Seq_Id: %d, Num: %d, N.TimeOuts %d",
                    queueName.c_str(), configPacket->source, configPacket->seq_id, configPacket->lastAck + configPacket->firstAckReceived, configPacket->numberOfTimeouts);

                // If number of timeouts is greater than Max timeouts, erase it
                if (configPacket->numberOfTimeouts >= MAX_TIMEOUTS) {
                    ESP_LOGE(LM_TAG, "%s, MAX TIMEOUTS reached, erasing Id: %d", queueName.c_str(), configPacket->seq_id);
                    clearLinkedList(current);
                    queue->DeleteCurrent();
                    continue;
                }

                // Recalculate the timeout
                recalculateTimeoutAfterTimeout(configPacket);

                if (type == QueueType::WRP) {
                    // Send Last ACK + 1 (Request this packet)
                    sendLostPacket(configPacket->source, configPacket->seq_id, configPacket->lastAck + 1);
                }
                else {
                    // Repeat the configPacket ACK
                    if (configPacket->firstAckReceived == 0)
                        // Send the first packet of the sequence (SYNC packet)
                        sendPacketSequence(current, 0);
                }
            }

            vTaskDelay(1);

        } while (queue->next());
    }

    queue->releaseInUse();
}

unsigned long LoraMesher::getMaximumTimeout(sequencePacketConfig* configPacket) {
    uint8_t hops = configPacket->node->networkNode.metric;
    if (hops == 0) {
        ESP_LOGE(LM_TAG, "Find next hop in add timeout");
        return 100000;
    }

    return 60000 + hops * 5000;//DEFAULT_TIMEOUT * 1000 * hops;
}

unsigned long LoraMesher::calculateTimeout(sequencePacketConfig* configPacket) {
    //TODO: This timeout should account for the number of send packets waiting to send + how many time between send packets?
    //TODO: This timeout should be a little variable depending on the duty cycle. 
    //TODO: Account for how many hops the packet needs to do
    //TODO: Account for how many packets are inside the Q_SP
    uint8_t hops = configPacket->node->networkNode.metric;
    if (hops == 0) {
        ESP_LOGE(LM_TAG, "Find next hop in add timeout");
        return MIN_TIMEOUT * 1000;
    }

    if (configPacket->node->SRTT == 0)
        // TODO: The default timeout should be enough smaller to prevent unnecessary timeouts.
        // TODO: Testing the default value
        return MIN_TIMEOUT * 1000 + hops * 5000;

    unsigned long calculatedTimeout = configPacket->node->SRTT + 4 * configPacket->node->RTTVAR;
    unsigned long maxTimeout = getMaximumTimeout(configPacket);

    if (calculatedTimeout > maxTimeout)
        return maxTimeout;

    return (unsigned long) max((int) calculatedTimeout, (int) MIN_TIMEOUT * 1000 + hops * 5000);
}

void LoraMesher::addTimeout(sequencePacketConfig* configPacket) {
    unsigned long timeout = calculateTimeout(configPacket);

    configPacket->timeout = millis() + timeout;
    configPacket->previousTimeout = timeout;

    ESP_LOGV(LM_TAG, "Timeout set to %u s", (unsigned int) (timeout / 1000));
}

void LoraMesher::recalculateTimeoutAfterTimeout(sequencePacketConfig* configPacket) {
    unsigned long timeout = calculateTimeout(configPacket);
    // TODO: The following prevTimeout function should be tested
    unsigned long prevTimeout = log(configPacket->numberOfTimeouts + 1) * 50000 + ToSendPackets->getLength() * 3000;

    if (prevTimeout > timeout)
        timeout = prevTimeout;

    unsigned long maxTimeout = getMaximumTimeout(configPacket);
    if (timeout > maxTimeout)
        timeout = maxTimeout;

    // if (timeout < configPacket->previousTimeout) {
    //     timeout = configPacket->previousTimeout * 2;

    //     unsigned long maxTimeout = getMaximumTimeout(configPacket);
    //     if (timeout > maxTimeout)
    //         timeout = maxTimeout;
    // }

    configPacket->timeout = millis() + timeout;
    configPacket->previousTimeout = timeout;

    ESP_LOGV(LM_TAG, "Timeout recalculated to %u s", (unsigned int) (timeout / 1000));
}

uint8_t LoraMesher::getSequenceId() {
    if (sequence_id == 255) {
        sequence_id = 0;
        return 255;
    }

    uint8_t seqId = sequence_id;
    sequence_id++;

    return seqId;
}

// Provisioning mode methods
void LoraMesher::enableProvisioningMode(uint32_t durationMs) {
    provisioningModeActive = true;
    if (durationMs > 0) {
        provisioningModeEndTime = millis() + durationMs;
    } else {
        provisioningModeEndTime = 0; // Indefinite
    }
    
    ESP_LOGI(LM_TAG, "Provisioning mode enabled with %s duration", 
             durationMs > 0 ? "finite" : "indefinite");
}

void LoraMesher::disableProvisioningMode() {
    provisioningModeActive = false;
    provisioningModeEndTime = 0;
    ESP_LOGI(LM_TAG, "Provisioning mode disabled");
}

bool LoraMesher::isProvisioningModeActive() {
    if (!provisioningModeActive) {
        return false;
    }
    
    // Check if timed provisioning mode has expired
    if (provisioningModeEndTime > 0 && millis() > provisioningModeEndTime) {
        disableProvisioningMode();
        return false;
    }
    
    return true;
}

uint16_t LoraMesher::getCurrentHelloDelay() {
    // SIMPLIFIED: 2-phase system - update mode based on timeout first
    updateHelloMode();
    
    // Return delay based on current mode
    switch (currentHelloMode) {
        case HELLO_MODE_FAST_DISCOVERY:
            return HELLO_FAST_INTERVAL;      // 30s - for provisioning/discovery
        case HELLO_MODE_NORMAL:
        default:
            return HELLO_NORMAL_INTERVAL;    // 120s - for normal operation
    }
}

bool LoraMesher::ensureRouteToTarget(uint16_t targetAddress) {
    // SIMPLIFIED: Only check if route exists in routing table from HELLO packets
    // No active route discovery - rely purely on HELLO-based routing
    if (RoutingTableService::hasAddressRoutingTable(targetAddress)) {
        ESP_LOGD(LM_TAG, "Route to 0x%04X exists in routing table", targetAddress);
        return true;
    }
    
    ESP_LOGW(LM_TAG, "No route to 0x%04X in routing table - waiting for HELLO packets", targetAddress);
    return false;
}

// ======================
// Phase 1: Dynamic Hello Mode Implementation
// ======================

void LoraMesher::setHelloMode(uint8_t mode, uint32_t durationMs) {
    if (mode == currentHelloMode) {
        return; // Already in target mode
    }
    
    ESP_LOGI(LM_TAG, "Switching hello mode: %d -> %d (duration: %dms)", 
             currentHelloMode, mode, durationMs);
    
    currentHelloMode = mode;
    helloModeStartTime = millis();
    helloModeDuration = durationMs;
    
    // Notify Hello task to restart with new interval immediately
    if (Hello_TaskHandle != nullptr) {
        xTaskNotify(Hello_TaskHandle, 0, eNoAction);
        ESP_LOGD(LM_TAG, "Notified Hello task to apply new interval: %d seconds", getCurrentHelloDelay());
    }
}

void LoraMesher::updateHelloMode() {
    // SIMPLIFIED: 2-phase system with simple timeout-based transition
    // Removed: Complex route quality checks, stabilizing phase, transition phase
    // Reasoning: Bellman-Ford converges fast, timeout mechanism handles instability
    
    if (helloModeDuration == 0) {
        return; // No timeout configured (normal mode runs indefinitely)
    }
    
    uint32_t currentTime = millis();
    uint32_t elapsed = currentTime - helloModeStartTime;
    
    // Simple timeout check for fast discovery mode
    if (elapsed >= helloModeDuration && currentHelloMode == HELLO_MODE_FAST_DISCOVERY) {
        // Fast discovery timeout - switch directly to normal mode
        ESP_LOGI(LM_TAG, "Fast discovery complete (5min) - switching to normal mode (120s intervals)");
        setHelloMode(HELLO_MODE_NORMAL, 0);
    }
}

// REMOVED: broadcastHelloModeChange()
// Reasoning: Each node manages its own hello timing independently
//            Provisioning mode is typically triggered only on bridge/gateway via UART
//            Broadcast hello mode control adds complexity without benefit
//            Nodes will naturally adjust their routing tables based on received hello packets

void LoraMesher::startFastDiscoveryMode(uint32_t durationMs) {
    ESP_LOGI(LM_TAG, "Starting fast discovery mode (30s intervals) for %dms", durationMs);
    
    // SIMPLIFIED: Direct mode change without broadcast
    // Note: Broadcast removed - each node manages its own hello timing
    //       Provisioning is typically triggered only on bridge/gateway
    setHelloMode(HELLO_MODE_FAST_DISCOVERY, durationMs);
}

void LoraMesher::stopFastDiscoveryMode() {
    ESP_LOGI(LM_TAG, "Stopping fast discovery mode - switching to normal mode (120s intervals)");
    
    // SIMPLIFIED: Direct transition to normal mode
    // Removed: Complex quality checks, stabilizing phase, transition mode
    // Reasoning: 5 minutes of fast discovery is sufficient for convergence
    setHelloMode(HELLO_MODE_NORMAL, 0);
}

/**
 * @brief Broadcast hello mode change command to all nodes
 * @param targetMode Hello mode to broadcast (HELLO_MODE_FAST_DISCOVERY or HELLO_MODE_NORMAL)
 * @param durationMs Duration for the mode (0 for permanent)
 * 
 * Purpose: Allow Bridge to remotely control hello mode of all nodes
 * Use case: When Bridge receives "Start Provisioning" command from ESP32,
 *           it broadcasts FAST_DISCOVERY mode to help discover new nodes faster
 * 
 * Note: This is NOT automatic synchronization - it's manual control triggered
 *       by Bridge when provisioning starts
 */
void LoraMesher::broadcastHelloModeChange(HelloMode targetMode, uint32_t durationMs) {
    ESP_LOGI(LM_TAG, "Broadcasting hello mode change: mode=%d, duration=%dms", targetMode, durationMs);
    
    // Create control packet
    ControlPacket* controlPacket = PacketFactory::createControlPacket(
        getLocalAddress(),
        BROADCAST_ADDR,
        HELLO_MODE_CONTROL_P,
        sizeof(HelloModeControlPayload)
    );
    
    if (!controlPacket) {
        ESP_LOGE(LM_TAG, "Failed to create hello mode control packet");
        return;
    }
    
    // Prepare payload
    HelloModeControlPayload* payload = (HelloModeControlPayload*)controlPacket->payload;
    payload->targetMode = targetMode;
    payload->durationMs = durationMs;
    payload->timestamp = millis();
    
    // Send packet directly (broadcast)
    bool sent = sendPacket(reinterpret_cast<Packet<uint8_t>*>(controlPacket));
    
    // Clean up
    vPortFree(controlPacket);
    
    if (sent) {
        ESP_LOGI(LM_TAG, "Hello mode control packet broadcasted successfully");
    } else {
        ESP_LOGE(LM_TAG, "Failed to broadcast hello mode control packet");
    }
}

// ======================
// REMOVED: Route Quality Implementation
// ======================
// Removed hasQualityRoutes(), getQualityRouteCount(), areRoutesStable()
// Reasoning: Timeout mechanism is sufficient for route management
//            Complex quality checks add overhead without significant benefit
//            Simple timeout-based pruning works reliably for mesh networks

#ifdef ENABLE_MESH_SECURITY
bool LoraMesher::isSecurityResyncPacket(uint8_t type) {
    // Security resync packets are detected by examining raw packet content
    // We need to check the packet structure more carefully
    return false; // TODO: Implement proper detection logic
}

bool LoraMesher::isSecurityResyncPacket(Packet<uint8_t>* packet) {
    if (!packet || packet->packetSize < sizeof(SecurityPacketHeader)) {
        return false;
    }
    
    // Try to cast to security packet header and check type
    const SecurityPacketHeader* secHeader = (const SecurityPacketHeader*)((uint8_t*)packet + sizeof(PacketHeader));
    
    return (secHeader->securityType == SECURITY_RESYNC_REQUEST || 
            secHeader->securityType == SECURITY_RESYNC_RESPONSE);
}

bool LoraMesher::processSecurityResyncPacket(const uint8_t* packet, size_t packetSize, uint16_t senderAddress) {
    if (!packet || packetSize < sizeof(SecurityPacketHeader)) {
        ESP_LOGW(LM_TAG, "Invalid security packet parameters");
        return false;
    }
    
    // Cast to security packet header to check type
    const SecurityPacketHeader* secHeader = (const SecurityPacketHeader*)packet;
    
    ESP_LOGI(LM_TAG, "Processing security packet type 0x%02X from 0x%04X", 
             secHeader->securityType, senderAddress);
    
    switch (secHeader->securityType) {
        case SECURITY_RESYNC_REQUEST: {
            if (packetSize < sizeof(ResyncRequestPacket)) {
                ESP_LOGW(LM_TAG, "RESYNC_REQUEST packet too small");
                return false;
            }
            
            const ResyncRequestPacket* request = (const ResyncRequestPacket*)packet;
            ESP_LOGI(LM_TAG, "Processing RESYNC_REQUEST from 0x%04X (reason: %d, seq: %lu)", 
                     senderAddress, request->reason, request->currentSequence);
            
            return MeshSecurityService::processResyncRequest(request, senderAddress);
        }
        
        case SECURITY_RESYNC_RESPONSE: {
            if (packetSize < sizeof(ResyncResponsePacket)) {
                ESP_LOGW(LM_TAG, "RESYNC_RESPONSE packet too small");
                return false;
            }
            
            const ResyncResponsePacket* response = (const ResyncResponsePacket*)packet;
            ESP_LOGI(LM_TAG, "Processing RESYNC_RESPONSE from 0x%04X (accepted: %s, seq: %lu)", 
                     senderAddress, response->accepted ? "YES" : "NO", response->allowedSequence);
            
            return MeshSecurityService::processResyncResponse(response, senderAddress);
        }
        
        default:
            ESP_LOGW(LM_TAG, "Unknown security packet type: 0x%02X", secHeader->securityType);
            return false;
    }
}
#endif

/**
 * End Large and Reliable payloads
 */