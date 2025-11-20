#ifndef _RS485_CONFIG_H
#define _RS485_CONFIG_H

/**
 * @file rs485_config.h
 * @brief RS485 Modbus RTU Configuration for Soil Sensor
 * 
 * Hardware: SN65HVD78DR RS485 Transceiver
 * Communication: Modbus RTU over RS485 at 9600 bps
 * Soil Sensor: 7-parameter multi-parameter sensor
 * 
 * Shared component for both Node and Gateway applications
 */

// ============================================================================
// UART HARDWARE CONFIGURATION
// ============================================================================

/** UART peripheral to use (UART_NUM_1 or UART_NUM_2) */
#define RS485_UART_NUM          UART_NUM_1

/** RX pin: Receive data from RS485 (R pin of SN65HVD78DR) */
#define RS485_RX_PIN            20

/** TX pin: Transmit data to RS485 (D pin of SN65HVD78DR) */
#define RS485_TX_PIN            21

/** DE/RE pin: Driver Enable / Receiver Enable (DE pin of SN65HVD78DR)
 *  - HIGH (1): Enable transmitter (send mode)
 *  - LOW (0):  Enable receiver (read mode)
 */
#define RS485_DE_PIN            42

// ============================================================================
// MODBUS RTU CONFIGURATION
// ============================================================================

/** Serial baud rate for Modbus RTU communication */
#define MODBUS_BAUD_RATE        4800

/** Modbus slave address of soil sensor */
#define MODBUS_SLAVE_ADDRESS    0x01  // 1 in decimal

/** Modbus function code: Read Holding Registers */
#define MODBUS_FUNC_READ_INPUT  0x03

// ============================================================================
// TIMING & RETRY CONFIGURATION
// ============================================================================

/** Maximum time to wait for Modbus response (milliseconds) */
#define MODBUS_RESPONSE_TIMEOUT_MS  500

/** Number of retry attempts when communication fails */
#define MODBUS_RETRY_COUNT      3

/** Initial retry delay (milliseconds) - will be doubled on each retry */
#define MODBUS_RETRY_DELAY_MS   200

// ============================================================================
// STARTUP/INITIALIZATION REGISTERS (Phase 1)
// ============================================================================

/** Device Version/ID Register Address (read at startup) */
#define REG_DEVICE_VERSION      0x07D0

/** Sensor ID High Word Register Address (High 16-bits of 32-bit ID) */
#define REG_SENSOR_ID_HIGH      0x0023

/** Sensor ID Low Word Register Address (Low 16-bits of 32-bit ID) */
#define REG_SENSOR_ID_LOW       0x0024

/** Measurement Control/Trigger Register Address (Phase 2) */
#define REG_MEASUREMENT_CTRL    0x0009

// ============================================================================
// REGISTER ADDRESS CONFIGURATION (Soil Sensor Parameters)
// ============================================================================

/** Soil Moisture Register Address */
#define REG_SOIL_MOISTURE       0x0000

/** Soil Temperature Register Address */
#define REG_SOIL_TEMPERATURE    0x0001

/** Soil pH Register Address */
#define REG_SOIL_pH             0x0002

/** Electrical Conductivity (EC) Register Address */
#define REG_SOIL_EC             0x0003

/** Nitrogen Content Register Address */
#define REG_SOIL_NITROGEN       0x0004

/** Phosphorus Content Register Address */
#define REG_SOIL_PHOSPHORUS     0x0005

/** Potassium Content Register Address */
#define REG_SOIL_POTASSIUM      0x0006

/** Total number of registers to read for soil sensor */
#define SOIL_SENSOR_REGISTER_COUNT  7

// ============================================================================
// CRC-16 CONFIGURATION
// ============================================================================

/** CRC-16 polynomial used in Modbus RTU (0xA001 = reversed 0x8005) */
#define MODBUS_CRC_POLY         0xA001

// ============================================================================
// LOGGING CONFIGURATION
// ============================================================================

/** Log tag for RS485 module */
#define RS485_TAG               "RS485"

/** Log tag for Modbus driver */
#define MODBUS_TAG              "ModbusRTU"

/** Log tag for soil sensor service */
#define SOIL_SENSOR_TAG         "SoilSensor"

#endif // _RS485_CONFIG_H
