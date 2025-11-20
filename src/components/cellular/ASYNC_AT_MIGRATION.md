/**
 * @file ASYNC_AT_MIGRATION.md
 * 
 * AT Command Handler - Async Migration Guide
 * ==========================================
 * 
 * Problem: Old blocking sendCommand() causes timeouts when mesh packets arrive
 * Solution: New async API prevents blocking cellular task
 * 
 * Timeline Example (Old vs New):
 * 
 * OLD (Blocking - Causes Timeout):
 * ┌─────────────────────────────────────┐
 * │ Cellular Task                       │
 * │ sendCommand("AT+CREG?")             │ ← Blocks for 1-3 seconds
 * │ ├─ Wait response...                 │
 * │ │  └─ [Mesh packet arrives]         │ ← Can't process!
 * │ │     [Mesh task runs ~500ms]       │
 * │ │  └─ [Response stuck in UART]      │
 * │ ├─ TIMEOUT! ❌                       │
 * └─────────────────────────────────────┘
 * 
 * NEW (Async - No Blocking):
 * ┌─────────────────────────────────────┐
 * │ Cellular Task (Core 0)              │
 * │ cmdId = sendCommandAsync("AT+CREG?")│ ← Returns immediately!
 * │ [Cellular task continues]           │
 * │                                     │
 * │ [Mesh packet arrives]               │
 * │ └─ Mesh task runs on Core 1         │ ← Separate core, no blocking!
 * │                                     │
 * │ Later: waitForResponse(cmdId)       │ ← Check response
 * │ ✅ Response ready!                   │
 * └─────────────────────────────────────┘
 * 
 * Meanwhile:
 * ┌─────────────────────────────────────┐
 * │ AT Receiver Task (Core 1)           │
 * │ - Continuously reads UART           │
 * │ - Matches responses to commands     │
 * │ - Processes URCs                    │
 * │ - Independent of cellular task     │
 * └─────────────────────────────────────┘
 * 
 * 
 * IMPLEMENTATION STEPS
 * ====================
 * 
 * 1. Replace ATCommandHandler with ATCommandAsync in cellular_connection_service
 * 
 * 2. Update initialization:
 *    OLD: atHandler = new ATCommandHandler(uart);
 *    NEW: atHandler = new ATCommandAsync(uart);
 *         atHandler->initialize();  // Start receiver task
 * 
 * 3. Update sendCommand calls:
 * 
 *    Option A: Drop-in replacement (blocking, for init phase)
 *    ┌─────────────────────────────────┐
 *    │ Response resp =                 │
 *    │   atHandler->sendCommand("+...");│
 *    │ if (!resp.success) { ... }      │
 *    └─────────────────────────────────┘
 * 
 *    Option B: Fully async (recommended for periodic tasks)
 *    ┌──────────────────────────────────────┐
 *    │ // Send (non-blocking)               │
 *    │ uint32_t cmdId =                     │
 *    │   atHandler->sendCommandAsync("+...");│
 *    │                                      │
 *    │ // Check later or wait               │
 *    │ Response resp;                       │
 *    │ if (atHandler->isCommandComplete(    │
 *    │         cmdId, &resp)) {             │
 *    │     // Process response              │
 *    │ }                                    │
 *    └──────────────────────────────────────┘
 * 
 * 4. For registration state queries (cellular_firebase_queue.cpp):
 * 
 *    OLD:
 *    ┌──────────────────────────────────┐
 *    │ auto resp = m_atHandler->       │
 *    │   sendCommand("+CREG?", timeout);│
 *    │ if (!resp.success) { retry(); }  │
 *    └──────────────────────────────────┘
 *    Problem: Blocks, can timeout during mesh activity
 * 
 *    NEW:
 *    ┌──────────────────────────────────────────┐
 *    │ // Check if last query is done           │
 *    │ if (m_lastCregQueryId != 0) {            │
 *    │     Response resp;                       │
 *    │     if (m_atHandler->isCommandComplete(  │
 *    │             m_lastCregQueryId, &resp)) { │
 *    │         processRegistrationResponse();   │
 *    │         m_lastCregQueryId = 0;           │
 *    │     } else {                             │
 *    │         return;  // Still waiting        │
 *    │     }                                    │
 *    │ } else {                                 │
 *    │     // Send new query                    │
 *    │     m_lastCregQueryId =                  │
 *    │         m_atHandler->                   │
 *    │         sendCommandAsync("+CREG?");      │
 *    │ }                                        │
 *    └──────────────────────────────────────────┘
 *    Benefit: Never blocks, mesh can process anytime
 * 
 * 
 * PERFORMANCE COMPARISON
 * ======================
 * 
 * Scenario: Mesh packet arrives while cellular queries registration
 * 
 * OLD (Blocking):
 * T+0000ms:   Cellular sends AT+CREG?
 * T+0100ms:   Cellular blocks in readResponse()
 * T+0150ms:   Mesh packet received
 * T+0200ms:   Mesh routingTableManager() starts processing
 * T+0700ms:   Mesh finishes, control returns to cellular
 * T+0800ms:   Cellular finally reads UART response
 * T+0900ms:   Command completes ✅ (but took 900ms instead of 100ms)
 * 
 * NEW (Async):
 * T+0000ms:   Cellular sends AT+CREG? (returns immediately)
 * T+0010ms:   Cellular continues other tasks
 * T+0150ms:   Mesh packet received
 * T+0200ms:   Mesh routingTableManager() starts processing
 * T+0700ms:   Mesh finishes, no interference
 * T+0720ms:   Receiver task reads UART response (was waiting in background)
 * T+0725ms:   Cellular checks isCommandComplete() - ✅ Response ready!
 * 
 * Key: Response arrives at ~100ms either way, but NEW doesn't block mesh!
 * 
 * 
 * ERROR HANDLING
 * ==============
 * 
 * Timeouts are handled automatically by receiver task:
 * - Command waits up to timeoutMs
 * - Receiver marks as TIMEOUT after expiry
 * - Cleaned up automatically
 * - No resource leaks
 * 
 * 
 * MIGRATION CHECKLIST
 * ===================
 * 
 * □ Replace ATCommandHandler include with ATCommandAsync
 * □ Call atHandler->initialize() in setup
 * □ Update registration state query loop (see step 4)
 * □ Update other periodic commands (signal quality, etc.)
 * □ Test: No timeouts during mesh activity
 * □ Monitor: Check pending command count (should stay <5)
 * □ Verify: Serial logs show no "Timeout waiting for final response"
 */

// EXAMPLE: cellular_firebase_queue.cpp processItem() update
/*
// OLD CODE (blocking, prone to timeout):
case FirebaseQueueItem::Type::CHECK_REGISTRATION_STATE:
    {
        ESP_LOGD(TAG, "Checking registration state...");
        auto resp = m_atHandler->sendCommand("+CREG?", 3000);  // ← BLOCKS!
        if (resp.success) {
            parseRegistrationResponse(resp.data);
        }
        break;
    }

// NEW CODE (async, won't timeout during mesh):
case FirebaseQueueItem::Type::CHECK_REGISTRATION_STATE:
    {
        // Check if previous query is complete
        if (m_registrationQueryId != 0) {
            Response resp;
            if (m_atHandler->isCommandComplete(m_registrationQueryId, &resp)) {
                if (resp.success) {
                    parseRegistrationResponse(resp.data);
                }
                m_registrationQueryId = 0;  // Reset for next query
            } else {
                // Still waiting, skip this iteration
                ESP_LOGD(TAG, "Waiting for registration response...");
            }
        } else {
            // Send new registration query (non-blocking)
            ESP_LOGD(TAG, "Sending registration query...");
            m_registrationQueryId = m_atHandler->sendCommandAsync("+CREG?");
        }
        break;
    }

// Member variable in cellular_firebase_queue.h:
private:
    uint32_t m_registrationQueryId = 0;  // Track pending registration query
*/
