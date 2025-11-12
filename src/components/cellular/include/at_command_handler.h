#ifndef AT_COMMAND_HANDLER_H
#define AT_COMMAND_HANDLER_H

#include <Arduino.h>
#include "cellular_uart.h"
#include <functional>
#include <vector>

/**
 * @brief AT Command Handler for A7682S Module
 * 
 * Features:
 * - Send AT commands with automatic response parsing
 * - Handle OK/ERROR responses
 * - Parse custom responses (+CPIN, +CSQ, etc.)
 * - Handle URC (Unsolicited Result Codes)
 * - Multi-line response handling
 * - Timeout management
 * 
 * @note A7682S uses standard AT command format with \r\n line endings
 */
class ATCommandHandler {
public:
    /**
     * @brief AT command response
     */
    struct Response {
        bool success;               ///< Command succeeded (got OK)
        String data;                ///< Response data (before OK/ERROR)
        String errorMessage;        ///< Error message (if failed)
        int errorCode;              ///< CME/CMS error code (-1 if none)
        uint32_t responseTimeMs;    ///< Response time in milliseconds
        
        Response()
            : success(false)
            , errorCode(-1)
            , responseTimeMs(0)
        {}
    };

    /**
     * @brief URC (Unsolicited Result Code) callback
     * @param urc URC string (e.g., "+CREG: 0,1")
     */
    using URCCallback = std::function<void(const String& urc)>;

    /**
     * @brief Constructor
     * @param uart Pointer to CellularUART instance
     */
    explicit ATCommandHandler(CellularUART* uart);

    /**
     * @brief Destructor
     */
    ~ATCommandHandler();

    /**
     * @brief Send AT command and wait for response
     * @param command AT command (without "AT" prefix, e.g., "+CPIN?")
     *                Use "" for simple "AT" command
     * @param timeoutMs Timeout in milliseconds (default: 1000ms)
     * @param expectOK Expect "OK" response (default: true)
     * @return Response object with result
     */
    Response sendCommand(const String& command, 
                        uint32_t timeoutMs = 1000,
                        bool expectOK = true);

    /**
     * @brief Send raw command (with full control)
     * @param rawCommand Full command string (e.g., "AT+CPIN?\r\n")
     * @param timeoutMs Timeout in milliseconds
     * @return Response object with result
     */
    Response sendRawCommand(const String& rawCommand, uint32_t timeoutMs = 1000);

    /**
     * @brief Send data command (multi-step: command -> prompt -> data)
     * 
     * Example for AT+CIPSEND:
     *   1. Send "AT+CIPSEND=0,100"
     *   2. Wait for ">" prompt
     *   3. Send data (100 bytes)
     *   4. Wait for "SEND OK"
     * 
     * @param command Initial AT command
     * @param data Data to send after prompt
     * @param promptChar Expected prompt character (default: '>')
     * @param timeoutMs Timeout for each step
     * @return Response object with result
     */
    Response sendDataCommand(const String& command,
                            const String& data,
                            char promptChar = '>',
                            uint32_t timeoutMs = 5000);

    /**
     * @brief Test if module is responsive
     * @param retries Number of retries (default: 3)
     * @return true if module responds to "AT"
     */
    bool testAT(uint8_t retries = 3);

    /**
     * @brief Wait for specific response
     * @param expectedResponse Expected string in response
     * @param timeoutMs Timeout in milliseconds
     * @return true if response received
     */
    bool waitForResponse(const String& expectedResponse, uint32_t timeoutMs = 1000);

    /**
     * @brief Register URC callback
     * @param callback Callback function for URCs
     */
    void registerURCCallback(URCCallback callback);

    /**
     * @brief Process URCs (call in main loop)
     * 
     * This should be called periodically to handle unsolicited messages
     * like +CREG, +CSQ, +CIPRCV, etc.
     */
    void processURCs();

    /**
     * @brief Enable/disable command echo
     * @param enable true to enable echo
     * @return Response object
     */
    Response setEcho(bool enable);

    /**
     * @brief Parse +CME ERROR response
     * @param response Response string
     * @return Error code (-1 if not a CME error)
     */
    static int parseCMEError(const String& response);

    /**
     * @brief Parse +CMS ERROR response
     * @param response Response string
     * @return Error code (-1 if not a CMS error)
     */
    static int parseCMSError(const String& response);

    /**
     * @brief Extract value from response
     * 
     * Example: "+CPIN: READY" -> "READY"
     *          "+CSQ: 25,0" -> "25,0"
     * 
     * @param response Full response string
     * @param prefix Prefix to remove (e.g., "+CPIN:")
     * @return Extracted value
     */
    static String extractValue(const String& response, const String& prefix);

    /**
     * @brief Split comma-separated values
     * @param value Comma-separated string (e.g., "25,0")
     * @return Vector of values
     */
    static std::vector<String> splitValues(const String& value);

    /**
     * @brief Get UART instance for direct access (advanced usage)
     * @return Pointer to UART (do not delete!)
     */
    CellularUART* getUART() const { return m_uart; }

private:
    CellularUART* m_uart;
    URCCallback m_urcCallback;
    
    static const char* TAG;
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 1000;
    static constexpr uint32_t LONG_TIMEOUT_MS = 10000;

    /**
     * @brief Read response from module
     * @param timeoutMs Timeout in milliseconds
     * @param expectOK Expect "OK" at end
     * @return Response object
     */
    Response readResponse(uint32_t timeoutMs, bool expectOK);

    /**
     * @brief Check if line is a final response (OK/ERROR)
     * @param line Line to check
     * @return true if final response
     */
    bool isFinalResponse(const String& line) const;

    /**
     * @brief Check if line is an URC (unsolicited result code)
     * @param line Line to check
     * @return true if URC
     */
    bool isURC(const String& line) const;
};

#endif // AT_COMMAND_HANDLER_H
