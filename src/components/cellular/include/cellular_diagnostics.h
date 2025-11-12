#pragma once

#include <string>
#include "cellular_connection_service.h"

/**
 * @file cellular_diagnostics.h
 * @brief Diagnostic utilities for A7682S modem HTTPS connectivity issues
 * 
 * Purpose: Test modem receive capability with simple HTTP echo service
 * to isolate whether the problem is:
 * - Modem can't receive ANY data (fundamental issue)
 * - Firebase doesn't respond (auth or service issue)
 * - Network path blocking responses (APN/firewall issue)
 */

class CellularDiagnostics {
public:
    struct TestResult {
        bool success;
        int httpStatus;
        std::string responseData;
        uint32_t responseTime;
        std::string errorMessage;
        
        // Detailed diagnostics
        int connectionTime;  // Time to AT+CCHOPEN OK
        int sendTime;        // Time to AT+CCHSEND OK
        int receiveTime;     // Time to first data arrival
        int urcRounds;       // How many URC poll rounds before data
        
        TestResult() : success(false), httpStatus(0), responseTime(0), 
                       connectionTime(0), sendTime(0), receiveTime(0), urcRounds(0) {}
    };

    /**
     * Test if modem can receive data from HTTP echo service
     * 
     * This is the simplest possible test:
     * - Connects to httpbin.org (public echo service)
     * - Sends simple HTTP GET request
     * - Tries to receive response
     * 
     * If this fails, modem has fundamental receive issue
     * If this succeeds, modem can receive data (Firebase issue then)
     * 
     * @return TestResult with success flag and diagnostic info
     */
    static TestResult testEchoService();

    /**
     * Test if modem can reach Firebase and authenticate
     * 
     * This test:
     * - Connects to Firebase HTTPS endpoint
     * - Sends auth token verification request
     * - Checks if response is received
     * 
     * If this fails after echo succeeds, Firebase rejects this modem
     * 
     * @param authToken Firebase auth token to test
     * @return TestResult with success flag and diagnostic info
     */
    static TestResult testFirebaseAuth(const std::string& authToken);

    /**
     * Run full diagnostic suite and log results
     * 
     * Runs tests in order and stops at first failure for efficient diagnosis
     * 
     * @return true if all tests pass, false otherwise
     */
    static bool runFullDiagnostic(const std::string& authToken);

private:
    // Internal helper to send raw HTTPS request and capture response
    struct HTTPRequest {
        std::string method;    // "GET", "POST", etc.
        std::string path;      // "/path", "/api/v1/data", etc.
        std::string host;      // "host.com"
        std::string body;      // For POST/PUT
        std::string hostHeader; // "Host: header value"
    };

    static TestResult sendRawHTTPRequest(const std::string& hostname, uint16_t port, 
                                        bool useSSL, const HTTPRequest& request);

    // Helper to format HTTP request bytes
    static std::string buildHTTPRequest(const HTTPRequest& req);
};
