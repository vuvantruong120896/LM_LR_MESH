#include "uart_protocol.h"
#include "bridge_config.h"
#include <esp_log.h>

static const char* TAG = "UART_PROTO";

UartProtocol::UartProtocol(HardwareSerial* serialPort) 
    : uart(serialPort), sequenceNumber(0), rxBufferIndex(0) {
}

void UartProtocol::begin(uint32_t baudRate) {
    uart->begin(baudRate, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    uart->setTimeout(UART_TIMEOUT_MS);
    
    ESP_LOGI(TAG, "Protocol initialized on pins RX:%d TX:%d at %d baud", 
             UART_RX_PIN, UART_TX_PIN, baudRate);
    
    // Send initial heartbeat to signal readiness
    sendHeartbeat();
}

uint8_t UartProtocol::calculateChecksum(const UartPacket* packet) {
    uint8_t checksum = 0;
    checksum ^= packet->packetType;
    checksum ^= packet->payloadLength;
    checksum ^= packet->sequenceNumber;
    
    for (int i = 0; i < packet->payloadLength; i++) {
        checksum ^= packet->payload[i];
    }
    
    return checksum;
}

uint8_t UartProtocol::calculateChecksumFields(uint8_t packetType, uint8_t payloadLength, uint8_t sequenceNumber, const uint8_t* payload) {
    uint8_t checksum = 0;
    checksum ^= packetType;
    checksum ^= payloadLength;
    checksum ^= sequenceNumber;
    for (int i = 0; i < payloadLength; ++i) {
        checksum ^= payload[i];
    }
    return checksum;
}

bool UartProtocol::sendRawPacket(uint8_t packetType, const uint8_t* payload, uint8_t payloadLength) {
    if (payloadLength > UART_MAX_PAYLOAD_SIZE) return false;

    uint8_t header[5];
    header[0] = UART_PACKET_START_BYTE1;
    header[1] = UART_PACKET_START_BYTE2;
    header[2] = packetType;
    header[3] = payloadLength;
    header[4] = ++sequenceNumber;

    uint8_t checksum = calculateChecksumFields(packetType, payloadLength, header[4], payload);

    // Write header (two start bytes + type/len/seq)
    if (uart->write(header, sizeof(header)) != sizeof(header)) {
        return false;
    }

    // Write payload if any
    if (payloadLength > 0 && payload != nullptr) {
        if (uart->write(payload, payloadLength) != payloadLength) {
            return false;
        }
    }

    // Write checksum and end byte
    uint8_t tail[2] = { checksum, UART_PACKET_END_BYTE };
    if (uart->write(tail, sizeof(tail)) != sizeof(tail)) {
        return false;
    }

    return true;
}

bool UartProtocol::validatePacket(const UartPacket* packet) {
    if (packet->startBytes[0] != UART_PACKET_START_BYTE1) return false;
    if (packet->startBytes[1] != UART_PACKET_START_BYTE2) return false;
    if (packet->endByte != UART_PACKET_END_BYTE) return false;
    if (packet->payloadLength > UART_MAX_PAYLOAD_SIZE) return false;
    
    uint8_t expectedChecksum = calculateChecksum(packet);
    return (packet->checksum == expectedChecksum);
}

bool UartProtocol::sendDataPacket(const dataPacket& data, uint16_t sourceNode) {
    // Build payload in a small on-stack buffer
    uint8_t smallPayload[2 + sizeof(dataPacket)];
    smallPayload[0] = (uint8_t)(sourceNode & 0xFF);
    smallPayload[1] = (uint8_t)((sourceNode >> 8) & 0xFF);
    memcpy(&smallPayload[2], &data, sizeof(dataPacket));

    bool ok = sendRawPacket(UART_PACKET_DATA, smallPayload, sizeof(smallPayload));
    // ESP_LOGI(TAG, "Sent data packet from node 0x%04X, seq=%d", sourceNode, sequenceNumber);
    return ok;
}

bool UartProtocol::sendStatusPacket(const UartBridgeStatus& status) {
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(&status);
    bool ok = sendRawPacket(UART_PACKET_STATUS, payload, sizeof(UartBridgeStatus));
    // ESP_LOGI(TAG, "Sent status packet, nodes=%d, uptime=%ds, seq=%d", status.connectedNodes, status.uptime, sequenceNumber);
    return ok;
}

bool UartProtocol::sendHeartbeat() {
    uint32_t timestamp = millis();
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(&timestamp);
    return sendRawPacket(UART_PACKET_HEARTBEAT, payload, sizeof(timestamp));
}

bool UartProtocol::sendAck(uint8_t sequenceNum) {
    uint8_t p = sequenceNum;
    return sendRawPacket(UART_PACKET_ACK, &p, 1);
}

bool UartProtocol::sendError(uint8_t errorCode) {
    uint8_t p = errorCode;
    bool ok = sendRawPacket(UART_PACKET_ERROR, &p, 1);
    ESP_LOGW(TAG, "Sent error packet, code=0x%02X", errorCode);
    return ok;
}

bool UartProtocol::sendNetkeyUpdateConfirm(bool success) {
    uint8_t p[2];
    p[0] = UART_CMD_SET_NETKEY;
    p[1] = success ? 0x01 : 0x00;
    bool ok = sendRawPacket(UART_PACKET_ACK, p, 2);
    ESP_LOGI(TAG, "Sent netkey update confirmation: %s", success ? "SUCCESS" : "FAILED");
    return ok;
}

bool UartProtocol::sendProvisioningStatus(const UartProvisioningStatus& status) {
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(&status);
    bool ok = sendRawPacket(UART_PACKET_STATUS, payload, sizeof(UartProvisioningStatus));
    ESP_LOGI(TAG, "Sent provisioning status - Active: %s, Sessions: %d/%d", status.active ? "YES" : "NO", status.activeSessions, status.maxSessions);
    return ok;
}

void UartProtocol::setNetkeyCallback(void (*callback)(const UartNetworkKey& netkey)) {
    netkeyCallback = callback;
}

void UartProtocol::setProvisioningCallback(void (*callback)(const UartProvisioningControl& control)) {
    provisioningCallback = callback;
}

bool UartProtocol::receivePacket(UartPacket& packet) {
    if (!uart->available()) return false;
    
    while (uart->available()) {
        uint8_t byte = uart->read();

        // Only start buffering when we see the start byte
        if (rxBufferIndex == 0 && byte != UART_PACKET_START_BYTE1) {
            // drop until first start byte
            continue; // Wait for start byte
        }

        rxBuffer[rxBufferIndex++] = byte;
        
        // Check if we have a complete packet
        if (rxBufferIndex >= 5) { // At least two-start + type+len+seq
            uint8_t payloadLength = rxBuffer[3];
            size_t expectedPacketSize = 7 + payloadLength; // 2 start + type+len+seq + payload + checksum + end
            
            if (rxBufferIndex >= expectedPacketSize) {
                // We have a complete packet
                ESP_LOGD(TAG, "Header parsed: payloadLength=%d expectedSize=%d", payloadLength, expectedPacketSize);
                ESP_LOG_BUFFER_HEXDUMP(TAG, rxBuffer, expectedPacketSize < sizeof(rxBuffer) ? expectedPacketSize : sizeof(rxBuffer), ESP_LOG_DEBUG);

                // Manually parse fields into packet to avoid struct packing/size issues when memcpy'ing variable lengths
                packet.startBytes[0] = rxBuffer[0];
                packet.startBytes[1] = rxBuffer[1];
                packet.packetType = rxBuffer[2];
                packet.payloadLength = rxBuffer[3];
                packet.sequenceNumber = rxBuffer[4];

                // Ensure we don't read past the packet payload buffer
                if (packet.payloadLength > sizeof(packet.payload)) {
                    ESP_LOGW(TAG, "Payload length too large (%d), resetting buffer", packet.payloadLength);
                    rxBufferIndex = 0;
                    return false;
                }

                // Copy payload bytes
                if (packet.payloadLength > 0) {
                    memcpy(packet.payload, &rxBuffer[5], packet.payloadLength);
                }

                // Tail: checksum is immediately after payload, end byte follows
                size_t checksumIndex = 5 + packet.payloadLength;
                packet.checksum = rxBuffer[checksumIndex];
                packet.endByte = rxBuffer[checksumIndex + 1];

                if (validatePacket(&packet)) {
                    rxBufferIndex = 0; // Reset buffer
                    return true;
                } else {
                    // Detailed debug dump to help diagnose framing/checksum issues
                    uint8_t expected = calculateChecksum(&packet);
                    ESP_LOGW(TAG, "Invalid packet received, dumping details:");
                    ESP_LOGW(TAG, " start=0x%02X%02X type=0x%02X len=%d seq=0x%02X", packet.startBytes[0], packet.startBytes[1], packet.packetType, packet.payloadLength, packet.sequenceNumber);
                    ESP_LOGW(TAG, " recv-checksum=0x%02X expected-checksum=0x%02X end=0x%02X", packet.checksum, expected, packet.endByte);
                    // Print raw payload bytes using buffer hex
                    if (packet.payloadLength > 0) {
                        ESP_LOG_BUFFER_HEXDUMP(TAG, packet.payload, packet.payloadLength, ESP_LOG_WARN);
                    }
                    // Also print the full raw rxBuffer for the packet length we saw
                    size_t rawLen = 6 + packet.payloadLength;
                    ESP_LOG_BUFFER_HEXDUMP(TAG, rxBuffer, rawLen < sizeof(rxBuffer) ? rawLen : sizeof(rxBuffer), ESP_LOG_WARN);

                    rxBufferIndex = 0;
                    return false;
                }
            }
        }
        
        // Prevent buffer overflow
        if (rxBufferIndex >= sizeof(rxBuffer)) {
                    ESP_LOGW(TAG, "Buffer overflow, resetting");
            rxBufferIndex = 0;
        }
    }
    
    return false;
}

void UartProtocol::processReceivedPacket(const UartPacket& packet) {
    ESP_LOGI(TAG, "Received packet type=0x%02X, seq=%d, len=%d", 
             packet.packetType, packet.sequenceNumber, packet.payloadLength);
    
    // Send ACK for reliable packets
    if (packet.packetType == UART_PACKET_COMMAND) {
        sendAck(packet.sequenceNumber);
    }
    
    switch (packet.packetType) {
        case UART_PACKET_COMMAND: {
            if (packet.payloadLength > 0) {
                UartCommand cmd = (UartCommand)packet.payload[0];
                ESP_LOGI(TAG, "Received command: 0x%02X", cmd);
                
                switch (cmd) {
                    case UART_CMD_GET_STATUS:
                        // Bridge will send status in next cycle
                        ESP_LOGI(TAG, "Status request received");
                        break;
                        
                    case UART_CMD_SET_NETKEY: {
                        if (packet.payloadLength >= (1 + sizeof(UartNetworkKey))) {
                            UartNetworkKey netkey;
                            memcpy(&netkey, &packet.payload[1], sizeof(UartNetworkKey));
                            
                            // Log key details for verification (avoid printing full key for security)
                            ESP_LOGI(TAG, "Key version: %d, Network ID: 0x%04X", netkey.keyVersion, netkey.networkId);
                            
                            // Print first 8 bytes of the network key for verification (manual hex print)
                            ESP_LOGI(TAG, "Network Key (first 8 bytes): %02X%02X%02X%02X%02X%02X%02X%02X", 
                                     netkey.networkKey[0], netkey.networkKey[1], netkey.networkKey[2], netkey.networkKey[3],
                                     netkey.networkKey[4], netkey.networkKey[5], netkey.networkKey[6], netkey.networkKey[7]);
                            // Print token for verification
                            ESP_LOGI(TAG, "Auth Token: %02X%02X%02X%02X%02X%02X%02X%02X", 
                                     netkey.authToken[0], netkey.authToken[1], netkey.authToken[2], netkey.authToken[3],
                                     netkey.authToken[4], netkey.authToken[5], netkey.authToken[6], netkey.authToken[7]);

                            // Call callback to handle netkey update
                            if (netkeyCallback) {
                                netkeyCallback(netkey);
                                sendNetkeyUpdateConfirm(true);
                            } else {
                                ESP_LOGW(TAG, "No netkey callback registered");
                                sendNetkeyUpdateConfirm(false);
                            }
                        } else {
                            ESP_LOGW(TAG, "Invalid netkey packet size");
                            sendError(0x02); // Invalid payload size
                        }
                        break;
                    }
                    
                    case UART_CMD_START_PROVISIONING: {
                        if (packet.payloadLength >= (1 + sizeof(UartProvisioningControl))) {
                            UartProvisioningControl control;
                            memcpy(&control, &packet.payload[1], sizeof(UartProvisioningControl));
                            control.action = 1; // Force start action
                            
                            ESP_LOGI(TAG, "Start provisioning command - Duration: %dms, MaxSessions: %d", control.durationMs, control.maxSessions);
                            
                            if (provisioningCallback) {
                                provisioningCallback(control);
                                sendAck(packet.sequenceNumber);
                            } else {
                                ESP_LOGW(TAG, "No provisioning callback registered");
                                sendError(0x03); // No handler error
                            }
                        } else {
                            ESP_LOGW(TAG, "Invalid start provisioning packet size");
                            sendError(0x02); // Invalid payload size
                        }
                        break;
                    }
                    
                    case UART_CMD_STOP_PROVISIONING: {
                        UartProvisioningControl control;
                        control.action = 0; // Stop action
                        control.durationMs = 0;
                        control.maxSessions = 0;
                        control.authMethod = 0;
                        
                        ESP_LOGI(TAG, "Stop provisioning command received");
                        
                        if (provisioningCallback) {
                            provisioningCallback(control);
                            sendAck(packet.sequenceNumber);
                        } else {
                            ESP_LOGW(TAG, "No provisioning callback registered");
                            sendError(0x03); // No handler error
                        }
                        break;
                    }
                    
                    case UART_CMD_GET_PROVISIONING_STATUS: {
                        UartProvisioningControl control;
                        control.action = 2; // Get status action
                        control.durationMs = 0;
                        control.maxSessions = 0;
                        control.authMethod = 0;
                        
                        ESP_LOGI(TAG, "Get provisioning status command received");
                        
                        if (provisioningCallback) {
                            provisioningCallback(control);
                            sendAck(packet.sequenceNumber);
                        } else {
                            ESP_LOGW(TAG, "No provisioning callback registered");
                            sendError(0x03); // No handler error
                        }
                        break;
                    }
                        
                    case UART_CMD_RESET:
                        ESP_LOGW(TAG, "Reset command received");
                        ESP.restart();
                        break;
                        
                    default:
                        ESP_LOGW(TAG, "Unknown command: 0x%02X", cmd);
                        sendError(0x01); // Unknown command error
                        break;
                }
            }
            break;
        }
        
        case UART_PACKET_ACK:
            ESP_LOGI(TAG, "ACK received for seq=%d", packet.payloadLength > 0 ? packet.payload[0] : 0);
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled packet type: 0x%02X", packet.packetType);
            break;
    }
}

void UartProtocol::update() {
    UartPacket packet;
    if (receivePacket(packet)) {
        processReceivedPacket(packet);
    }
}

void UartProtocol::flush() {
    uart->flush();
    rxBufferIndex = 0;
}

bool UartProtocol::isConnected() {
    // Simple connectivity check - could be enhanced with timeout tracking
    return uart->availableForWrite() > 0;
}