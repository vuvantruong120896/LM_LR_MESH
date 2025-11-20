# Scenario 2 Visual Diagrams

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         KAGRI NODE APPLICATION                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌──────────────────────────┐  ┌────────────────────────────────────┐  │
│  │  Sensor Data Generator   │  │  Routing Table Service (LoraMesher)│  │
│  ├──────────────────────────┤  ├────────────────────────────────────┤  │
│  │ • Generate every 30s     │  │ • Manage mesh routes               │  │
│  │ • Then 10-min intervals  │  │ • Hello packet updates             │  │
│  │ • Send to Gateway        │  │ • Node timeout management          │  │
│  │ • Or buffer if offline   │  │ • CALLS: onRoutingTableChanged()   │  │
│  └──────────────────────────┘  └────────────────────────────────────┘  │
│           │                              │                              │
│           │ findGatewayAddress()         │ Routing table update        │
│           │                              │                              │
│           ▼                              ▼                              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  onRoutingTableChanged() ← NEW CALLBACK (SCENARIO 2)            │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │ 1. Save routing table (original function)                       │  │
│  │ 2. Check current gateway: findGatewayAddress()                  │  │
│  │ 3. Detect transitions:                                          │  │
│  │    • false → true:  Gateway became available (URGENT SYNC!)    │  │
│  │    • true → false:  Gateway became unavailable                │  │
│  │    • address change: Gateway switched                           │  │
│  │ 4. Update static flags:                                         │  │
│  │    • wasGatewayAvailable                                        │  │
│  │    • lastKnownGateway                                           │  │
│  │ 5. Log status + LED feedback                                    │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│           │                                                             │
│           ▼                                                             │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Main Loop - Sensor Transmission (Every 30 seconds)             │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │                                                                  │  │
│  │  Check buffer: getBufferedCount()                               │  │
│  │         │                                                       │  │
│  │         ├─→ count == 0: Continue                               │  │
│  │         │                                                       │  │
│  │         └─→ count > 0: Process buffer                          │  │
│  │                 │                                              │  │
│  │                 ├─ Check gateway: findGatewayAddress()         │  │
│  │                 │   │                                          │  │
│  │                 │   ├─→ Gateway NOT found: Skip                │  │
│  │                 │   │                                          │  │
│  │                 │   └─→ Gateway found:                         │  │
│  │                 │        │                                     │  │
│  │                 │        └─ Detect urgent sync:                │  │
│  │                 │           bool isUrgentSync =                │  │
│  │                 │             wasGatewayAvailable &&           │  │
│  │                 │             lastKnownGateway != 0            │  │
│  │                 │           │                                  │  │
│  │                 │           ├─ YES (Gateway just available):  │  │
│  │                 │           │  • maxSyncPerCycle = 10          │  │
│  │                 │           │  • delayMs = 200                 │  │
│  │                 │           │  • Log: "⚡ URGENT SYNC MODE"   │  │
│  │                 │           │                                  │  │
│  │                 │           └─ NO (Normal conditions):        │  │
│  │                 │              • maxSyncPerCycle = 3-5         │  │
│  │                 │              • delayMs = 500                 │  │
│  │                 │                                              │  │
│  │                 └─ Send buffered data:                         │  │
│  │                    for i in 0..maxSyncPerCycle:               │  │
│  │                      getOldestData(bufferedData)              │  │
│  │                      createPacketAndSend<sensorData>(         │  │
│  │                        dst, &bufferedData, 1)                 │  │
│  │                      removeOldest()                            │  │
│  │                      vTaskDelay(delayMs)                       │  │
│  │                                                                │  │
│  │  Update buffer status:                                         │  │
│  │    if (bufferedCount == 0):                                   │  │
│  │      Log: "✅ ALL BUFFERED DATA SYNCED SUCCESSFULLY!"         │  │
│  │      LED: Connected pattern (blue)                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│           │                                                             │
│           ▼                                                             │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  NodeOfflineBuffer (NVS-based Persistent Storage)              │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │ Capacity: 50 samples (~3.2 KB total)                           │  │
│  │ Type: Circular FIFO buffer                                     │  │
│  │ Methods: addData(), getOldestData(), removeOldest()            │  │
│  │ Persistence: Survives node reboots                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  LoRa Mesh Network (LoraMesher)                                 │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │ • Transmit sensor data to Gateway                              │  │
│  │ • Receive Hello packets from neighbors                         │  │
│  │ • Forward packets through mesh                                 │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  LED Feedback System                                            │  │
│  ├──────────────────────────────────────────────────────────────────┤  │
│  │ • Blue (pulsing): Gateway available, normal operation          │  │
│  │ • Blue (steady): Urgent sync complete, buffer empty            │  │
│  │ • Red (alert): Gateway unavailable, buffering data             │  │
│  │ • Cyan (flash): Data sent/buffered                             │  │
│  └──────────────────────────────────────────────────────────────────┘  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## State Machine Diagram

```
                            ┌─────────────────────┐
                            │   Node Startup      │
                            │ wasGateway... = F   │
                            └──────────┬──────────┘
                                       │
                                       ▼
                    ┌──────────────────────────────────────┐
                    │ Listen for routing table updates     │
                    │ Loop: Generate sensor data           │
                    └──────────┬───────────────────────────┘
                               │
                ┌──────────────┴──────────────┐
                │                             │
                ▼                             ▼
        ┌───────────────┐         ┌─────────────────────┐
        │ Gateway       │         │ Gateway NOT found   │
        │ Found in RT   │         │ (BROADCAST_ADDR)    │
        │               │         │                     │
        └───┬───────────┘         └─────┬───────────────┘
            │                           │
            ▼                           ▼
    ┌──────────────┐        ┌──────────────────────────┐
    │ Send data    │        │ Buffer data to NVS       │
    │ directly     │        │ (NodeOfflineBuffer)      │
    │ (normal op)  │        │                          │
    │              │        │ wasGatewayAvailable:     │
    │ was: true    │        │ true → FALSE             │
    │ now: true    │        │                          │
    │              │        │ ┌──────────────────────┐ │
    │ Continue ────┼─────┬──→ │ LED: Red pattern     │ │
    └──────────────┘     │  │ │ (offline indicator)  │ │
                         │  │ └──────────────────────┘ │
                         │  │                          │
                         │  └──────────────┬───────────┘
                         │                 │
                         │ ┌───────────────▼─────────────┐
                         │ │ [TIME PASSES - Up to hours] │
                         │ │ Buffer holds 50 samples     │
                         │ │ Data persists in NVS        │
                         │ └──────────────┬──────────────┘
                         │                │
                         │  ┌─────────────▼────────────────────┐
                         │  │ HELLO PACKET from Gateway        │
                         │  │ Routing table updated            │
                         │  │ onRoutingTableChanged() CALLED   │
                         │  └─────────────┬────────────────────┘
                         │                │
                         │                ▼
                         │  ┌──────────────────────────────────┐
                         │  │ SCENARIO 2: GATEWAY DETECTED     │
                         │  │                                  │
                         │  │ wasGatewayAvailable: false→TRUE  │
                         │  │ lastKnownGateway = 0x04XX        │
                         │  │                                  │
                         │  │ ┌────────────────────────────┐   │
                         │  │ │ Log: 🎯 SCENARIO 2:        │   │
                         │  │ │ GATEWAY DETECTED OFFLINE!   │   │
                         │  │ │                             │   │
                         │  │ │ LED: Blue pattern (ready)  │   │
                         │  │ └────────────────────────────┘   │
                         │  └─────────────┬────────────────────┘
                         │                │
                         │                ▼
                    ┌────┴──────────────────────────────────────┐
                    │ Next 30-second sync cycle:                │
                    │ • Check buffered count: 5 samples        │
                    │ • Detect: isUrgentSync = true            │
                    │ • Set: maxSyncPerCycle = 10              │
                    │ • Set: delayMs = 200                     │
                    │                                          │
                    │ Log: ⚡ URGENT SYNC MODE                │
                    │                                          │
                    │ Send buffered samples rapidly:           │
                    │ 1. Sample #1 (age: 125s)                │
                    │ 2. Sample #2 (age: 115s)                │
                    │ 3. Sample #3 (age: 105s)                │
                    │ 4. Sample #4 (age: 95s)                 │
                    │ 5. Sample #5 (age: 85s)                 │
                    │                                          │
                    └────┬───────────────────────────────────────┘
                         │
                         ▼
                    ┌────────────────────────┐
                    │ Buffer empty? YES      │
                    │                        │
                    │ Log: ✅ ALL SYNCED   │
                    │ LED: Connected (blue)  │
                    │                        │
                    │ wasGatewayAvailable    │
                    │ remains TRUE           │
                    │ (until gateway times   │
                    │  out again)            │
                    └────────────────────────┘
                         │
                         ▼
                    ┌────────────────────────┐
                    │ Resume normal cycle:   │
                    │ • maxSyncPerCycle = 3  │
                    │ • delayMs = 500        │
                    │ • Regular 30s check    │
                    └────────────────────────┘
```

## Timing Diagram

```
Timeline of Scenario 2 Operation (in seconds)

 0     30    60    90   120   150   180   210   240
 ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────
 │  S  │  B  │  B  │  B  │  B  │ [H] │  U  │  U  │  U  │
 │     │     │     │     │     │     │ ╔═╗ │ ╔═╗ │ ╔═╗ │
 │     │     │     │     │     │     │ ║S║ │ ║S║ │ ║S║ │
 │     │     │     │     │     │     │ ║Y║ │ ║Y║ │ ║Y║ │
 │     │     │     │     │     │     │ ║N║ │ ║N║ │ ║N║ │
 │     │     │     │     │     │     │ ║C║ │ ║C║ │ ║C║ │
 │     │     │     │     │     │     │ ╚═╝ │ ╚═╝ │ ╚═╝ │
 └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────

Legend:
 S = Sensor data generated (normal send to gateway)
 B = Sensor data generated (buffered - no gateway)
 [H] = Hello packet from Gateway received
      = Routing table updated
      = onRoutingTableChanged() triggered
      = Gateway detected after offline!
 U = Sync cycle (Urgent mode)
 ╔═╗ = Transmit samples rapidly
 ║S║   (10 samples/cycle, 200ms delay)
 ║Y║
 ║N║
 ║C║
 ╚═╝

Sample Counts During Timeline:
────────────────────────────────────────────────────
Time    Event               Buffered  Status
────    ──────────────────  ────────  ──────────────
0s      Startup            0/50      Waiting
30s     Sensor #1 (B)      1/50      No gateway
60s     Sensor #2 (B)      2/50      No gateway
90s     Sensor #3 (B)      3/50      No gateway
120s    Sensor #4 (B)      4/50      No gateway
150s    Hello packet →     4/50      DETECTED! URGENT
180s    Sync cycle (U)     3/50      Sent sample #1
        Sync cycle (U)     2/50      Sent sample #2
        Sync cycle (U)     1/50      Sent sample #3
210s    Sync cycle (U)     0/50      All sent! ✅
240s    Resume normal      0/50      Regular ops
────────────────────────────────────────────────────

Detailed Sync Timeline (Gateway Detection to Completion):
────────────────────────────────────────────────────────────
Time  Action                      Buffer  Duration
────  ────────────────────────────  ──────  ────────
150s  [H] Hello packet from GW     4/50    event
      └─ RT updated
      └─ onRoutingTableChanged()
      └─ Detect: false → true
      └─ Log urgent sync
      └─ LED: Blue

180s  Next 30s sync cycle started           30ms
      ├─ Check buffered: 4 samples
      ├─ Detect: isUrgentSync = true
      ├─ Set: rate 10/cycle, 200ms delay
      ├─ Send sample #1 (age: 125s)        200ms
      ├─ Send sample #2 (age: 115s)        200ms
      ├─ Send sample #3 (age: 105s)        200ms
      ├─ Send sample #4 (age: 95s)         200ms
      └─ Buffer now empty                  0 ms
                                             ─────
                          Total: ~30 seconds
────────────────────────────────────────────────────────────
```

## Gateway State Transitions

```
                      ┌─────────────────┐
                      │  STARTUP STATE  │
                      │                 │
                      │ wasGateway... F │
                      │ lastKnown = 0   │
                      └────────┬────────┘
                               │
                   ┌───────────┴───────────┐
                   │                       │
                   ▼                       ▼
        ┌──────────────────┐    ┌─────────────────────┐
        │ GATEWAY FOUND    │    │ GATEWAY NOT FOUND   │
        │ (via Hello pkt)  │    │ (at startup)        │
        │                  │    │                     │
        │ wasGW... = T     │    │ wasGW... = F        │
        │ lastKnown = 0x04 │    │ lastKnown = 0       │
        └────────┬─────────┘    └─────────┬───────────┘
                 │                        │
      (Loop 1)   │ send direct            │ buffer (Loop 1)
                 │                        │
        ┌────────▼────────────────────────▼────────┐
        │  Wait 30 seconds                         │
        │  (or until routing table changes)        │
        └────────┬──────────────────────────────────┘
                 │
     ┌───────────┼───────────┐
     │           │           │
     │    (Transition A)     │
     │                       │
     ▼                       ▼
(Still T, T→F)    (State change: F→T)
Gateway offline   Gateway online
   after timeout  (Hello received)
                           │
        ┌──────────────────┴────────────────────┐
        │  URGENT SYNC TRIGGERED! ⚡             │
        │  ┌─────────────────────────────────┐  │
        │  │ maxSyncPerCycle = 10            │  │
        │  │ delayMs = 200                   │  │
        │  │ Log: SCENARIO 2 URGENT          │  │
        │  │ LED: Blue pattern               │  │
        │  └─────────────────────────────────┘  │
        │                                       │
        │  Send buffered data rapidly           │
        │  (~10 samples/cycle)                  │
        │                                       │
        └───────────────────┬───────────────────┘
                            │
                  ┌─────────▼──────────┐
                  │ Buffer empty? YES  │
                  │ Resume normal ops  │
                  │ (3 samples/cycle)  │
                  └───────────────────┘
                            │
        (Loop continues - until next Hello or timeout)
```

## Component Interaction Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                    SCENARIO 2 DATA FLOW                             │
└─────────────────────────────────────────────────────────────────────┘

Input Events:
├─ LoRa Hello Packet (from Gateway)
├─ Sensor Timer (30s, then 10-min intervals)
├─ Sync Timer (every 30 seconds)
└─ Routing Table Timeout (3+ minutes, no update)

                        ┌──────────────────────┐
                        │  Sensor Generated    │
                        └──────┬───────────────┘
                               │
            ┌──────────────────┴──────────────────┐
            │                                     │
            ▼                                     ▼
    ┌─────────────────┐              ┌──────────────────────┐
    │ Gateway Found?  │              │ findGatewayAddress() │
    │                 │              │                      │
    │ Check priorities:               │ PRIORITY 1: Prov NVS│
    │ 1. Provisioned  │              │ PRIORITY 2: Cached   │
    │ 2. Cached NVS   │              │ PRIORITY 3: Routing  │
    │ 3. Routing table│              │                      │
    └────┬────────────┘              └──────────────────────┘
         │
         ├─→ YES (addr ≠ broadcast)     NO (addr = broadcast)
         │                              │
         ▼                              ▼
    ┌──────────┐              ┌─────────────────────────┐
    │ Send via │              │ Buffer to NodeOffline   │
    │ LoRa     │              │ Buffer (NVS)            │
    │ Mesh     │              │                         │
    └──────┬───┘              └────────┬────────────────┘
           │                           │
           │                           ▼
           │                  ┌────────────────────────┐
           │                  │ Update static flags:   │
           │                  │ wasGatewayAvailable    │
           │                  │   = false              │
           │                  │ LED: Red pattern       │
           │                  │ Log: ⚠️ OFFLINE       │
           │                  └────────────────────────┘
           │
    ┌──────┴──────────────────────────────────────────┐
    │                                                 │
    │     Every 30 seconds:                          │
    │     Check buffer & attempt sync                │
    │                                                 │
    ▼                                                 ▼
(Gateway available)                    (Waiting for gateway)
├─ Check buffered count
│  ├─→ count = 0: Done
│  └─→ count > 0: Process
│      ├─ Find gateway again
│      ├─ Detect urgent sync
│      │  (wasGateway && lastKnown)
│      └─ Transmit samples
│         (adaptive rate)
│
└─→ count = 0: Continue normal send

Routing Table Event:
│
├─ Hello packet updates routing table
│  └─ onRoutingTableChanged() called
│
├─ Case 1: was False, now True (GATEWAY FOUND!)
│  ├─ wasGatewayAvailable = true
│  ├─ lastKnownGateway = new address
│  ├─ Log: 🎯 SCENARIO 2
│  ├─ Log: 📤 URGENT SYNC
│  ├─ Log: ⚡ Urgent mode activated
│  └─ LED: Blue pattern
│      └─→ Next sync cycle: 10 samples, 200ms delay
│
├─ Case 2: was True, now False (GATEWAY LOST!)
│  ├─ wasGatewayAvailable = false
│  ├─ Log: ⚠️ UNAVAILABLE
│  └─ LED: Red pattern
│      └─→ Start buffering
│
└─ Case 3: address changed
   ├─ lastKnownGateway = new address
   └─ Log: 🔄 Gateway changed
```

---

This documentation provides clear visual representations of how Scenario 2 works at multiple levels of abstraction.
