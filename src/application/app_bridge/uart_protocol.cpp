#include "uart_protocol.h"
#include "bridge_config.h"

UartProtocol::UartProtocol(HardwareSerial* serialPort) 
    : uart(serialPort), sequenceNumber(0), rxBufferIndex(0) {
}

void UartProtocol::begin(uint32_t baudRate) {
    uart->begin(baudRate, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    uart->setTimeout(UART_TIMEOUT_MS);
    
    Serial.printf("[UART] Protocol initialized on pins RX:%d TX:%d at %d baud\n", 
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

bool UartProtocol::validatePacket(const UartPacket* packet) {
    if (packet->startByte != UART_PACKET_START_BYTE) return false;
    if (packet->endByte != UART_PACKET_END_BYTE) return false;
    if (packet->payloadLength > UART_MAX_PAYLOAD_SIZE) return false;
    
    uint8_t expectedChecksum = calculateChecksum(packet);
    return (packet->checksum == expectedChecksum);
}

bool UartProtocol::sendDataPacket(const dataPacket& data, uint16_t sourceNode) {
    UartPacket packet;
    packet.startByte = UART_PACKET_START_BYTE;
    packet.packetType = UART_PACKET_DATA;
    packet.sequenceNumber = ++sequenceNumber;
    
    // Pack mesh data into payload
    struct {
        uint16_t sourceNode;
        dataPacket meshData;
    } payload = {sourceNode, data};
    
    packet.payloadLength = sizeof(payload);
    memcpy(packet.payload, &payload, sizeof(payload));
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = UART_PACKET_END_BYTE;
    
    size_t written = uart->write((uint8_t*)&packet, sizeof(UartPacket) - (200 - packet.payloadLength));
    
    Serial.printf("[UART] Sent data packet from node 0x%04X, seq=%d, bytes=%d\n", 
                  sourceNode, packet.sequenceNumber, written);
    
    return written > 0;
}

bool UartProtocol::sendStatusPacket(const UartBridgeStatus& status) {
    UartPacket packet;
    packet.startByte = UART_PACKET_START_BYTE;
    packet.packetType = UART_PACKET_STATUS;
    packet.sequenceNumber = ++sequenceNumber;
    packet.payloadLength = sizeof(UartBridgeStatus);
    
    memcpy(packet.payload, &status, sizeof(UartBridgeStatus));
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = UART_PACKET_END_BYTE;
    
    size_t written = uart->write((uint8_t*)&packet, sizeof(UartPacket) - (200 - packet.payloadLength));
    
    Serial.printf("[UART] Sent status packet, nodes=%d, uptime=%ds, seq=%d\n", 
                  status.connectedNodes, status.uptime, packet.sequenceNumber);
    
    return written > 0;
}

bool UartProtocol::sendHeartbeat() {
    UartPacket packet;
    packet.startByte = UART_PACKET_START_BYTE;
    packet.packetType = UART_PACKET_HEARTBEAT;
    packet.sequenceNumber = ++sequenceNumber;
    packet.payloadLength = 4;
    
    // Include timestamp in heartbeat
    uint32_t timestamp = millis();
    memcpy(packet.payload, &timestamp, sizeof(timestamp));
    
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = UART_PACKET_END_BYTE;
    
    size_t written = uart->write((uint8_t*)&packet, sizeof(UartPacket) - (200 - packet.payloadLength));
    
    return written > 0;
}

bool UartProtocol::sendAck(uint8_t sequenceNum) {
    UartPacket packet;
    packet.startByte = UART_PACKET_START_BYTE;
    packet.packetType = UART_PACKET_ACK;
    packet.sequenceNumber = ++sequenceNumber;
    packet.payloadLength = 1;
    packet.payload[0] = sequenceNum;  // ACK for which sequence number
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = UART_PACKET_END_BYTE;
    
    size_t written = uart->write((uint8_t*)&packet, sizeof(UartPacket) - (200 - packet.payloadLength));
    return written > 0;
}

bool UartProtocol::sendError(uint8_t errorCode) {
    UartPacket packet;
    packet.startByte = UART_PACKET_START_BYTE;
    packet.packetType = UART_PACKET_ERROR;
    packet.sequenceNumber = ++sequenceNumber;
    packet.payloadLength = 1;
    packet.payload[0] = errorCode;
    packet.checksum = calculateChecksum(&packet);
    packet.endByte = UART_PACKET_END_BYTE;
    
    size_t written = uart->write((uint8_t*)&packet, sizeof(UartPacket) - (200 - packet.payloadLength));
    
    Serial.printf("[UART] Sent error packet, code=0x%02X\n", errorCode);
    return written > 0;
}

bool UartProtocol::receivePacket(UartPacket& packet) {
    if (!uart->available()) return false;
    
    while (uart->available()) {
        uint8_t byte = uart->read();
        
        if (rxBufferIndex == 0 && byte != UART_PACKET_START_BYTE) {
            continue; // Wait for start byte
        }
        
        rxBuffer[rxBufferIndex++] = byte;
        
        // Check if we have a complete packet
        if (rxBufferIndex >= 4) { // At least header
            uint8_t payloadLength = rxBuffer[2];
            size_t expectedPacketSize = 6 + payloadLength; // Header + payload + checksum + end
            
            if (rxBufferIndex >= expectedPacketSize) {
                // We have a complete packet
                memcpy(&packet, rxBuffer, expectedPacketSize);
                
                if (validatePacket(&packet)) {
                    rxBufferIndex = 0; // Reset buffer
                    return true;
                } else {
                    Serial.println("[UART] Invalid packet received, discarding");
                    rxBufferIndex = 0;
                    return false;
                }
            }
        }
        
        // Prevent buffer overflow
        if (rxBufferIndex >= sizeof(rxBuffer)) {
            Serial.println("[UART] Buffer overflow, resetting");
            rxBufferIndex = 0;
        }
    }
    
    return false;
}

void UartProtocol::processReceivedPacket(const UartPacket& packet) {
    Serial.printf("[UART] Received packet type=0x%02X, seq=%d, len=%d\n", 
                  packet.packetType, packet.sequenceNumber, packet.payloadLength);
    
    // Send ACK for reliable packets
    if (packet.packetType == UART_PACKET_COMMAND) {
        sendAck(packet.sequenceNumber);
    }
    
    switch (packet.packetType) {
        case UART_PACKET_COMMAND: {
            if (packet.payloadLength > 0) {
                UartCommand cmd = (UartCommand)packet.payload[0];
                Serial.printf("[UART] Received command: 0x%02X\n", cmd);
                
                switch (cmd) {
                    case UART_CMD_GET_STATUS:
                        // Bridge will send status in next cycle
                        Serial.println("[UART] Status request received");
                        break;
                        
                    case UART_CMD_RESET:
                        Serial.println("[UART] Reset command received");
                        ESP.restart();
                        break;
                        
                    default:
                        Serial.printf("[UART] Unknown command: 0x%02X\n", cmd);
                        sendError(0x01); // Unknown command error
                        break;
                }
            }
            break;
        }
        
        case UART_PACKET_ACK:
            Serial.printf("[UART] ACK received for seq=%d\n", 
                         packet.payloadLength > 0 ? packet.payload[0] : 0);
            break;
            
        default:
            Serial.printf("[UART] Unhandled packet type: 0x%02X\n", packet.packetType);
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