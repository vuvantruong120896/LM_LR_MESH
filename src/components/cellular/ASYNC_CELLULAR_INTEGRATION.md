/**
 * @file ASYNC_CELLULAR_INTEGRATION.md
 * 
 * Async Cellular Integration - Complete Overview
 * ==============================================
 * 
 * PROBLEM SOLVED
 * ==============
 * 
 * Previously: Cellular CREG/CSQ queries were BLOCKING
 * Result: Mesh task couldn't run during cellular query → timeout
 * 
 * Timeline:
 * T+0000ms:  sendCommand("+CREG?") → BLOCK for ~1-3 seconds
 * T+0150ms:  Mesh packet arrives
 * T+0200ms:  Mesh routingTableManager() runs (500ms+)
 * T+0700ms:  Mesh finishes, cellular finally gets response
 * ❌ TIMEOUT because response buried under mesh processing
 * 
 * 
 * NEW ARCHITECTURE
 * ================
 * 
 * Core Components:
 * 
 * 1. ATCommandAsync (NEW)
 *    - Separate receiver task on Core 1
 *    - sendCommandAsync() returns ID immediately (non-blocking)
 *    - isCommandComplete() checks if response ready
 *    - Background receiver matches responses to command IDs
 * 
 * 2. CellularConnectionService (UPDATED)
 *    - Added async query tracking:
 *      * m_registrationQueryId
 *      * m_signalQualityQueryId
 *      * m_timeQueryId
 *      * m_operatorQueryId
 *      * m_ipAddressQueryId
 *    - Added processPendingAsyncResponses() in update()
 *    - updateSignalQuality() & updateRegistrationState() now async
 * 
 * 3. CellularFirebaseQueue (EXISTING)
 *    - Already calls updateSignalQuality() & updateRegistrationState()
 *    - Now runs with async backend (no blocking!)
 * 
 * 
 * EXECUTION FLOW (NEW)
 * ====================
 * 
 * Scenario: Firebase queue checks registration every 30s
 * 
 * Call 1 (T+0s):
 *   update() → processPendingAsyncResponses()
 *   → m_registrationQueryId == 0 → sendCommandAsync("+CREG?") returns ID=42
 *   → update() completes instantly ✅
 *   
 * [Mesh packet arrives at T+100ms]
 *   [Mesh runs for 500ms, no interference with cellular]
 * 
 * Call 2 (T+50ms):
 *   update() → processPendingAsyncResponses()
 *   → m_registrationQueryId == 42 → isCommandComplete(42) → NOT YET
 *   → return immediately (no blocking) ✅
 *   
 * [Receiver task runs on Core 1]
 *   T+110ms: Reads "+CREG: 0,1" from UART
 *   T+111ms: Matches to command ID 42, marks as COMPLETED
 * 
 * Call 3 (T+150ms):
 *   update() → processPendingAsyncResponses()
 *   → m_registrationQueryId == 42 → isCommandComplete(42) → YES! ✅
 *   → Parse response, update m_registrationState
 *   → m_registrationQueryId = 0 (reset for next query)
 *   → return immediately ✅
 * 
 * 
 * KEY CHANGES IN cellular_connection_service.cpp
 * ===============================================
 * 
 * 1. Header Include
 *    OLD: #include "at_command_handler.h"
 *    NEW: #include "at_command_async.h"
 * 
 * 2. Initialization
 *    OLD:
 *      m_atHandler = new ATCommandHandler(m_uart);
 *      m_atHandler->registerURCCallback(...);
 *    
 *    NEW:
 *      m_atHandler = new ATCommandAsync(m_uart);
 *      m_atHandler->initialize();  // ← Start receiver task!
 *      m_atHandler->registerURCCallback(...);
 * 
 * 3. Add async state tracking
 *    uint32_t m_registrationQueryId = 0;
 *    uint32_t m_signalQualityQueryId = 0;
 *    uint32_t m_lastRegistrationQueryTime = 0;
 *    uint32_t m_lastSignalQualityQueryTime = 0;
 *    uint32_t m_registrationQueryIntervalMs = 30000;
 *    uint32_t m_signalQualityQueryIntervalMs = 60000;
 * 
 * 4. New method: processPendingAsyncResponses()
 *    ├─ Called in update() first thing
 *    ├─ Checks all pending query IDs
 *    ├─ If response ready, parses and updates state
 *    ├─ If timeout, logs warning
 *    └─ Never blocks (returns immediately)
 * 
 * 5. Updated updateSignalQuality()
 *    OLD: for retry in 0..2: sendCommand(blocking...)
 *    NEW:
 *      if m_signalQualityQueryId != 0:
 *          if isCommandComplete(): parse response, reset ID
 *          else: skip (still waiting)
 *      else if timeout_interval_elapsed:
 *          m_signalQualityQueryId = sendCommandAsync()
 * 
 * 6. Updated updateRegistrationState()
 *    OLD: for retry in 0..2: sendCommand(blocking...)
 *    NEW: Same pattern as signal quality
 * 
 * 7. update() method
 *    First line: processPendingAsyncResponses()
 *    Rest: unchanged (enqueues to Firebase queue)
 * 
 * 
 * BENEFITS
 * ========
 * 
 * ✅ No more blocking cellular queries
 * ✅ Mesh task can run anytime (no contention)
 * ✅ Multiple commands can be pending simultaneously
 * ✅ Automatic timeout cleanup
 * ✅ Same API for init phase (backward compatible)
 * ✅ Scales to multiple async operations
 * ✅ Better use of dual-core ESP32
 * 
 * 
 * TIMING
 * ======
 * 
 * Registration Query Interval: 30 seconds
 * Signal Quality Query Interval: 60 seconds
 * Command Timeout: 3 seconds
 * 
 * Both are NON-BLOCKING, so actual timing overhead is <1ms per check!
 * 
 * 
 * LOGGING OUTPUT (NEW)
 * ====================
 * 
 * Initialization:
 * [CELLULAR_CONN] Create async AT handler
 * [AT_CMD_ASYNC] 🚀 Initializing async AT command handler
 * [AT_CMD_ASYNC] ✅ Async AT handler ready - receiver task created
 * 
 * Signal Quality Check:
 * [CELLULAR_CONN] 📶 Async signal quality query sent (ID: 42)
 * [CELLULAR_CONN] 📶 Signal quality updated: RSSI=25
 * 
 * Registration Check:
 * [CELLULAR_CONN] 📡 Async registration query sent (ID: 43)
 * [CELLULAR_CONN] 📡 Registration state changed: Registered (home)
 * 
 * Timeout:
 * [AT_CMD_ASYNC] [CMD 42] ⏱️  Timeout after 3000ms: +CSQ
 * 
 * 
 * TESTING CHECKLIST
 * =================
 * 
 * □ Build succeeds (no compilation errors)
 * □ Initialization completes (receiver task starts)
 * □ Signal quality updates every 60s (non-blocking)
 * □ Registration state updates every 30s (non-blocking)
 * □ Mesh packet reception works (no interference)
 * □ No timeout warnings during heavy mesh activity
 * □ Multiple queries can pend simultaneously
 * □ Serial logs show async query IDs
 * □ Firebase queue processes normally
 * 
 * 
 * FUTURE ENHANCEMENTS
 * ===================
 * 
 * 1. Add more async operations:
 *    - Operator name query (+COPS?)
 *    - IP address query
 *    - Time sync (+CCLK?)
 * 
 * 2. Priority queue for commands:
 *    - High: Critical responses (TCP data, errors)
 *    - Low: Periodic checks (signal, registration)
 * 
 * 3. Command batching:
 *    - Group related queries (+CSQ, +COPS, +CREG?)
 *    - Send multi-line responses at once
 * 
 * 4. Metrics collection:
 *    - Track response latency
 *    - Monitor pending command count
 *    - Log command timeouts
 */
