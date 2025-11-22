#ifndef MODBUS_ASYNC_H
#define MODBUS_ASYNC_H

#include <Arduino.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>
#include "rs485_config.h"

/**
 * @brief Modbus RTU Async Driver
 * 
 * Non-blocking implementation of Modbus RTU Master.
 * Uses a background task to handle UART reception and response processing.
 */
class ModbusAsync {
public:
    struct Transaction {
        uint32_t id;
        uint32_t timestamp;
        uint8_t slaveAddr;
        uint8_t funcCode;
        uint16_t startAddr;
        uint16_t count;
        uint16_t expectedResponseLen;
        
        enum State {
            PENDING,
            COMPLETED,
            TIMEOUT,
            ERROR
        } state;
        
        std::vector<uint8_t> responseData;
        uint8_t errorCode; // Modbus exception code or internal error
    };

    /**
     * @brief Initialize the driver and start receiver task
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Send a Modbus Read Input Registers request (FC 0x04) asynchronously
     * @param slaveAddr Slave address
     * @param startAddr Start register address
     * @param count Number of registers to read
     * @return Transaction ID (0 if failed)
     */
    uint32_t readInputRegistersAsync(uint8_t slaveAddr, uint16_t startAddr, uint16_t count);

    /**
     * @brief Send a Modbus Read Holding Registers request (FC 0x03) asynchronously
     * @param slaveAddr Slave address
     * @param startAddr Start register address
     * @param count Number of registers to read
     * @return Transaction ID (0 if failed)
     */
    uint32_t readHoldingRegistersAsync(uint8_t slaveAddr, uint16_t startAddr, uint16_t count);

    /**
     * @brief Send a Modbus Write Single Register request (FC 0x06) asynchronously
     * @param slaveAddr Slave address
     * @param regAddr Register address
     * @param value Value to write
     * @return Transaction ID (0 if failed)
     */
    uint32_t writeSingleRegisterAsync(uint8_t slaveAddr, uint16_t regAddr, uint16_t value);

    /**
     * @brief Check if a transaction is complete
     * @param transactionId Transaction ID returned by read...Async
     * @return true if complete (success, error, or timeout)
     */
    bool isTransactionComplete(uint32_t transactionId);

    /**
     * @brief Get the result of a completed transaction
     * @param transactionId Transaction ID
     * @param outValues Buffer to store the read values (converted to float)
     * @param maxCount Size of the buffer
     * @return Number of values read, or -1 if error/pending
     */
    int getResult(uint32_t transactionId, float* outValues, uint16_t maxCount);

    /**
     * @brief Get the raw transaction status
     * @param transactionId Transaction ID
     * @return Transaction state
     */
    Transaction::State getTransactionState(uint32_t transactionId);

    // Singleton instance access
    static ModbusAsync& getInstance();

private:
    ModbusAsync();
    ~ModbusAsync();

    // Disable copy
    ModbusAsync(const ModbusAsync&) = delete;
    ModbusAsync& operator=(const ModbusAsync&) = delete;

    // Internal helpers
    uint32_t sendRequestAsync(uint8_t slaveAddr, uint8_t funcCode, uint16_t startAddr, uint16_t count);
    uint16_t calculateExpectedResponseLength(uint8_t funcCode, uint16_t count);
    uint16_t calculateCRC(const uint8_t* data, uint16_t len);
    void sendRawFrame(const uint8_t* frame, uint16_t len);
    
    // Task related
    static void receiverTaskWrapper(void* param);
    void receiverTask();
    void processReceivedFrame(const std::vector<uint8_t>& frame);
    void cleanupOldTransactions();

    // Members
    bool m_initialized;
    TaskHandle_t m_receiverTaskHandle;
    SemaphoreHandle_t m_mutex;
    uint32_t m_nextTransactionId;
    
    std::map<uint32_t, Transaction> m_transactions;
    
    static const char* TAG;
    static const uint32_t TRANSACTION_TIMEOUT_MS = 2000;
};

#endif // MODBUS_ASYNC_H
