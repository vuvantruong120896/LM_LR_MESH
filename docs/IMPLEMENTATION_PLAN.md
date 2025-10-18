# Implementation Plan - Smart Device Provisioning via Firebase

## Mục tiêu
Triển khai tính năng thêm thiết bị (Gateway và Nodes) thông qua Firebase Command Queue, cho phép provisioning nhiều nodes cùng lúc mà không cần BLE trực tiếp.

---

## PHASE 1: Firebase Setup (30 phút - 1 giờ)

### Bước 1.1: Apply Firebase Structure
- [ ] Đọc `FIREBASE_STRUCTURE_MULTI_USER.md`
- [ ] Login vào Firebase Console
- [ ] Backup dữ liệu hiện tại (export JSON)

### Bước 1.2: Update Security Rules
```bash
# Copy rules từ firebase-rules.json
# Paste vào Firebase Console → Realtime Database → Rules
```

- [ ] Vào Firebase Console
- [ ] Realtime Database → Rules tab
- [ ] Paste nội dung từ `firebase-rules.json`
- [ ] Click "Publish"
- [ ] Verify rules không có lỗi

### Bước 1.3: Test Firebase Structure
- [ ] Tạo test user trong Firebase Auth
- [ ] Manually tạo data structure:
  ```
  users/
    test-uid-123/
      profile/
        email: "test@example.com"
      gateways/
        AA:BB:CC:DD:EE:FF/
          info/ ...
      commands/
        AA:BB:CC:DD:EE:FF/
          pending/ (empty)
  ```
- [ ] Verify security rules hoạt động (read/write permissions)

### Deliverables Phase 1:
- ✅ Firebase structure sẵn sàng
- ✅ Security rules đã apply
- ✅ Test data để development

**Thời gian:** 30 phút - 1 giờ

---

## PHASE 2: Firmware - Command Polling (1-2 ngày)

### Bước 2.1: Create FirebaseCommandPoller Service

**File:** `src/services/firebase_command_poller.h`

```cpp
#ifndef FIREBASE_COMMAND_POLLER_H
#define FIREBASE_COMMAND_POLLER_H

#include <Arduino.h>
#include <FirebaseESP32.h>

class FirebaseCommandPoller {
public:
    struct Command {
        String id;
        String type;
        String params;  // JSON string
        uint32_t timestamp;
        uint8_t priority;
    };

    FirebaseCommandPoller(FirebaseData* fbdo, const String& userUID, const String& gatewayMAC);
    
    // Lifecycle
    void begin();
    void poll();  // Call every 5-10 seconds in main loop
    
    // Command handling
    bool hasCommand();
    Command getNextCommand();
    void moveToProcessing(const Command& cmd);
    void moveToCompleted(const Command& cmd, const String& result, const String& message);
    void moveToFailed(const Command& cmd, const String& errorCode, const String& message);
    
    // Status updates
    void updateCommandResult(const String& cmdId, const String& status, const String& message);
    void updateProgress(const String& cmdId, uint16_t nodesDiscovered, uint32_t timeRemaining);

private:
    FirebaseData* m_fbdo;
    String m_userUID;
    String m_gatewayMAC;
    String m_basePath;  // users/{uid}/commands/{mac}
    
    Command m_currentCommand;
    bool m_hasCommand;
    uint32_t m_lastPoll;
    
    bool fetchPendingCommands();
    void cleanup();
};

#endif
```

**File:** `src/services/firebase_command_poller.cpp`

```cpp
#include "firebase_command_poller.h"
#include <esp_log.h>

static const char* TAG = "CMD_POLLER";

FirebaseCommandPoller::FirebaseCommandPoller(FirebaseData* fbdo, const String& userUID, const String& gatewayMAC)
    : m_fbdo(fbdo), m_userUID(userUID), m_gatewayMAC(gatewayMAC), m_hasCommand(false), m_lastPoll(0) {
    m_basePath = String("users/") + userUID + "/commands/" + gatewayMAC;
}

void FirebaseCommandPoller::begin() {
    ESP_LOGI(TAG, "Command poller initialized for user: %s, gateway: %s", 
             m_userUID.c_str(), m_gatewayMAC.c_str());
}

void FirebaseCommandPoller::poll() {
    uint32_t now = millis();
    
    // Poll every 10 seconds
    if (now - m_lastPoll < 10000) {
        return;
    }
    
    m_lastPoll = now;
    
    ESP_LOGD(TAG, "Polling for pending commands...");
    
    if (fetchPendingCommands()) {
        ESP_LOGI(TAG, "✅ Found pending command: %s (type: %s)", 
                 m_currentCommand.id.c_str(), m_currentCommand.type.c_str());
    }
}

bool FirebaseCommandPoller::fetchPendingCommands() {
    String pendingPath = m_basePath + "/pending";
    
    if (!Firebase.RTDB.getJSON(m_fbdo, pendingPath.c_str())) {
        ESP_LOGD(TAG, "No pending commands or error: %s", m_fbdo->errorReason().c_str());
        return false;
    }
    
    FirebaseJson json = m_fbdo->jsonObject();
    size_t len = json.iteratorBegin();
    
    if (len == 0) {
        json.iteratorEnd();
        return false;
    }
    
    // Get first command (sorted by priority then timestamp)
    String key, value;
    int type;
    
    json.iteratorGet(0, type, key, value);
    json.iteratorEnd();
    
    // Parse command
    FirebaseJson cmdJson;
    cmdJson.setJsonData(value);
    
    FirebaseJsonData data;
    
    m_currentCommand.id = key;
    
    if (cmdJson.get(data, "type")) {
        m_currentCommand.type = data.stringValue;
    }
    
    if (cmdJson.get(data, "params")) {
        m_currentCommand.params = data.stringValue;
    }
    
    if (cmdJson.get(data, "timestamp")) {
        m_currentCommand.timestamp = data.intValue;
    }
    
    if (cmdJson.get(data, "priority")) {
        m_currentCommand.priority = data.intValue;
    }
    
    m_hasCommand = true;
    return true;
}

bool FirebaseCommandPoller::hasCommand() {
    return m_hasCommand;
}

FirebaseCommandPoller::Command FirebaseCommandPoller::getNextCommand() {
    m_hasCommand = false;
    return m_currentCommand;
}

void FirebaseCommandPoller::moveToProcessing(const Command& cmd) {
    // Move from pending to processing
    String pendingPath = m_basePath + "/pending/" + cmd.id;
    String processingPath = m_basePath + "/processing/" + cmd.id;
    
    // Copy to processing
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "processing");
    json.set("startedAt", (unsigned long)millis());
    json.set("params", cmd.params);
    
    Firebase.RTDB.setJSON(m_fbdo, processingPath.c_str(), &json);
    
    // Delete from pending
    Firebase.RTDB.deleteNode(m_fbdo, pendingPath.c_str());
    
    // Update command_results
    updateCommandResult(cmd.id, "processing", "Command is being executed");
    
    ESP_LOGI(TAG, "Command %s moved to processing", cmd.id.c_str());
}

void FirebaseCommandPoller::moveToCompleted(const Command& cmd, const String& result, const String& message) {
    String processingPath = m_basePath + "/processing/" + cmd.id;
    String completedPath = m_basePath + "/completed/" + cmd.id;
    
    // Create completed entry
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "completed");
    json.set("result", result);
    json.set("message", message);
    json.set("completedAt", (unsigned long)millis());
    
    Firebase.RTDB.setJSON(m_fbdo, completedPath.c_str(), &json);
    Firebase.RTDB.deleteNode(m_fbdo, processingPath.c_str());
    
    updateCommandResult(cmd.id, "completed", message);
    
    ESP_LOGI(TAG, "✅ Command %s completed: %s", cmd.id.c_str(), message.c_str());
}

void FirebaseCommandPoller::moveToFailed(const Command& cmd, const String& errorCode, const String& message) {
    String processingPath = m_basePath + "/processing/" + cmd.id;
    String failedPath = m_basePath + "/failed/" + cmd.id;
    
    FirebaseJson json;
    json.set("id", cmd.id);
    json.set("type", cmd.type);
    json.set("status", "failed");
    json.set("result", "failed");
    json.set("errorCode", errorCode);
    json.set("message", message);
    json.set("failedAt", (unsigned long)millis());
    
    Firebase.RTDB.setJSON(m_fbdo, failedPath.c_str(), &json);
    Firebase.RTDB.deleteNode(m_fbdo, processingPath.c_str());
    
    updateCommandResult(cmd.id, "failed", message);
    
    ESP_LOGE(TAG, "❌ Command %s failed: %s", cmd.id.c_str(), message.c_str());
}

void FirebaseCommandPoller::updateCommandResult(const String& cmdId, const String& status, const String& message) {
    String resultPath = String("users/") + m_userUID + "/command_results/" + m_gatewayMAC;
    
    FirebaseJson json;
    json.set("last_command_id", cmdId);
    json.set("status", status);
    json.set("message", message);
    json.set("timestamp", (unsigned long)millis());
    json.set("gateway_online", true);
    json.set("last_poll", (unsigned long)millis());
    
    Firebase.RTDB.updateNode(m_fbdo, resultPath.c_str(), &json);
}

void FirebaseCommandPoller::updateProgress(const String& cmdId, uint16_t nodesDiscovered, uint32_t timeRemaining) {
    String resultPath = String("users/") + m_userUID + "/command_results/" + m_gatewayMAC + "/progress";
    
    FirebaseJson json;
    json.set("nodes_discovered", nodesDiscovered);
    json.set("time_remaining_ms", timeRemaining);
    
    Firebase.RTDB.updateNode(m_fbdo, resultPath.c_str(), &json);
}
```

### Bước 2.2: Integrate vào GatewayApp

**File:** `src/application/app_gateway/gateway_app.h`

```cpp
// Thêm vào class GatewayApp
private:
    FirebaseCommandPoller* commandPoller;
    
    // Command handlers
    void handleStartProvisioning(const FirebaseCommandPoller::Command& cmd);
    void handleStopProvisioning(const FirebaseCommandPoller::Command& cmd);
```

**File:** `src/application/app_gateway/gateway_app.cpp`

```cpp
// Trong setupFirebase(), sau khi connect thành công:
void GatewayApp::setupFirebase() {
    // ... existing code ...
    
    if (firebaseClient->connect()) {
        // ... existing code ...
        
        // Create command poller
        commandPoller = new FirebaseCommandPoller(
            firebaseClient->getFirebaseData(),
            userUID,
            gatewayMAC
        );
        commandPoller->begin();
        
        ESP_LOGI(TAG, "Command poller ready");
    }
}

// Trong loop(), thêm command polling:
void GatewayApp::loop() {
    // ... existing code ...
    
    // Poll for commands (if provisioned and online)
    if (isProvisioned && gatewayState.firebaseConnected && commandPoller) {
        commandPoller->poll();
        
        if (commandPoller->hasCommand()) {
            auto cmd = commandPoller->getNextCommand();
            
            ESP_LOGI(TAG, "Processing command: %s (type: %s)", 
                     cmd.id.c_str(), cmd.type.c_str());
            
            // Move to processing immediately
            commandPoller->moveToProcessing(cmd);
            
            // Handle command
            if (cmd.type == "start_provisioning") {
                handleStartProvisioning(cmd);
            } else if (cmd.type == "stop_provisioning") {
                handleStopProvisioning(cmd);
            } else {
                commandPoller->moveToFailed(cmd, "UNKNOWN_COMMAND", 
                                           "Unknown command type: " + cmd.type);
            }
        }
    }
    
    // ... rest of existing code ...
}

void GatewayApp::handleStartProvisioning(const FirebaseCommandPoller::Command& cmd) {
    ESP_LOGI(TAG, "*** START PROVISIONING via Firebase ***");
    
    // Parse params
    FirebaseJson json;
    json.setJsonData(cmd.params);
    
    FirebaseJsonData data;
    uint32_t durationMs = 600000;  // Default 10 minutes
    
    if (json.get(data, "durationMs")) {
        durationMs = data.intValue;
    }
    
    ESP_LOGI(TAG, "Duration: %u ms", durationMs);
    
    // Start fast discovery mode
    radio.startFastDiscoveryMode(durationMs);
    radio.broadcastHelloModeChange(HELLO_MODE_FAST_DISCOVERY, durationMs);
    
    // Update command result
    commandPoller->updateCommandResult(
        cmd.id,
        "processing",
        "Fast discovery mode activated"
    );
    
    // Schedule completion (will be handled in loop)
    gatewayState.provisioningEndTime = millis() + durationMs;
    gatewayState.provisioningActive = true;
    gatewayState.provisioningCommandId = cmd.id;
    
    ESP_LOGI(TAG, "✅ Provisioning started for %u ms", durationMs);
}

void GatewayApp::handleStopProvisioning(const FirebaseCommandPoller::Command& cmd) {
    ESP_LOGI(TAG, "*** STOP PROVISIONING via Firebase ***");
    
    // Stop fast discovery mode
    radio.stopFastDiscoveryMode();
    radio.broadcastHelloModeChange(HELLO_MODE_NORMAL, 0);
    
    gatewayState.provisioningActive = false;
    
    // Mark command as completed
    commandPoller->moveToCompleted(
        cmd,
        "success",
        "Provisioning stopped"
    );
    
    ESP_LOGI(TAG, "✅ Provisioning stopped");
}
```

### Bước 2.3: Update GatewayState Structure

**File:** `src/application/app_gateway/gateway_app.h`

```cpp
struct GatewayState {
    // ... existing fields ...
    
    // NEW: Provisioning state
    bool provisioningActive;
    uint32_t provisioningStartTime;
    uint32_t provisioningEndTime;
    String provisioningCommandId;
    uint16_t nodesDiscoveredDuringProvisioning;
};
```

### Bước 2.4: Build & Test

```bash
cd d:\Projects\Lora\LM_LR_MESH
pio run -t upload -t monitor
```

**Test cases:**
- [ ] Gateway boots và connect Firebase
- [ ] Gateway polls commands mỗi 10 giây
- [ ] Manually tạo command trong Firebase Console
- [ ] Gateway detect và process command
- [ ] Command status updates correctly
- [ ] Provisioning mode activates/deactivates

### Deliverables Phase 2:
- ✅ FirebaseCommandPoller service
- ✅ Command handling logic
- ✅ Integration với GatewayApp
- ✅ Tested với hardware

**Thời gian:** 1-2 ngày

---

## PHASE 3: Mobile App - UI & Logic (2-3 ngày)

### Bước 3.1: Create Firebase Services

**File:** `lib/services/firebase_command_service.dart`

```dart
import 'package:firebase_database/firebase_database.dart';

class FirebaseCommandService {
  final DatabaseReference _db = FirebaseDatabase.instance.ref();
  
  // Send command to gateway
  Future<String> sendCommand({
    required String userUID,
    required String gatewayMAC,
    required String commandType,
    required Map<String, dynamic> params,
  }) async {
    final commandId = 'cmd_${DateTime.now().millisecondsSinceEpoch}';
    
    final command = {
      'id': commandId,
      'type': commandType,
      'params': params,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
      'status': 'pending',
      'priority': 1,
      'createdBy': 'mobile_app',
    };
    
    await _db
        .child('users/$userUID/commands/$gatewayMAC/pending/$commandId')
        .set(command);
    
    return commandId;
  }
  
  // Listen to command results
  Stream<Map<String, dynamic>> listenCommandResult(
    String userUID,
    String gatewayMAC,
  ) {
    return _db
        .child('users/$userUID/command_results/$gatewayMAC')
        .onValue
        .map((event) {
      if (event.snapshot.value != null) {
        return Map<String, dynamic>.from(event.snapshot.value as Map);
      }
      return {};
    });
  }
  
  // Check if gateway is online
  Future<bool> isGatewayOnline(String userUID, String gatewayMAC) async {
    final snapshot = await _db
        .child('users/$userUID/gateways/$gatewayMAC/status/online')
        .get();
    
    return snapshot.value as bool? ?? false;
  }
  
  // Get all gateways for user
  Future<List<Map<String, dynamic>>> getUserGateways(String userUID) async {
    final snapshot = await _db.child('users/$userUID/gateways').get();
    
    if (!snapshot.exists) return [];
    
    final gateways = <Map<String, dynamic>>[];
    final data = Map<String, dynamic>.from(snapshot.value as Map);
    
    data.forEach((mac, gatewayData) {
      final gateway = Map<String, dynamic>.from(gatewayData as Map);
      gateway['mac'] = mac;
      gateways.add(gateway);
    });
    
    return gateways;
  }
}
```

**File:** `lib/services/provisioning_session_service.dart`

```dart
import 'package:firebase_database/firebase_database.dart';

class ProvisioningSessionService {
  final DatabaseReference _db = FirebaseDatabase.instance.ref();
  
  // Listen to provisioning session
  Stream<Map<String, dynamic>?> listenSession(
    String userUID,
    String gatewayMAC,
  ) {
    return _db
        .child('users/$userUID/provisioning_sessions/$gatewayMAC')
        .onValue
        .map((event) {
      if (event.snapshot.value != null) {
        return Map<String, dynamic>.from(event.snapshot.value as Map);
      }
      return null;
    });
  }
  
  // Get discovered nodes
  Future<List<Map<String, dynamic>>> getDiscoveredNodes(
    String userUID,
    String gatewayMAC,
  ) async {
    final snapshot = await _db
        .child('users/$userUID/provisioning_sessions/$gatewayMAC/nodes_discovered')
        .get();
    
    if (!snapshot.exists) return [];
    
    final nodes = <Map<String, dynamic>>[];
    final data = snapshot.value as List?;
    
    if (data != null) {
      for (var node in data) {
        if (node != null) {
          nodes.add(Map<String, dynamic>.from(node as Map));
        }
      }
    }
    
    return nodes;
  }
}
```

### Bước 3.2: Create UI Screens

**File:** `lib/screens/add_device_screen.dart`

```dart
// Bottom sheet để chọn loại device
class AddDeviceSheet extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Container(
      padding: EdgeInsets.all(24),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text('Thêm thiết bị',
              style: Theme.of(context).textTheme.headline6),
          SizedBox(height: 24),
          ListTile(
            leading: Icon(Icons.router),
            title: Text('Thêm Gateway mới'),
            subtitle: Text('Quét và kết nối Gateway qua BLE'),
            onTap: () {
              Navigator.pop(context);
              Navigator.pushNamed(context, '/ble-scan', arguments: 'gateway');
            },
          ),
          ListTile(
            leading: Icon(Icons.sensors),
            title: Text('Thêm Node vào mạng'),
            subtitle: Text('Chọn Gateway để thêm nodes'),
            onTap: () {
              Navigator.pop(context);
              Navigator.pushNamed(context, '/gateway-selection');
            },
          ),
        ],
      ),
    );
  }
}
```

**File:** `lib/screens/gateway_selection_screen.dart`

```dart
class GatewaySelectionScreen extends StatefulWidget {
  @override
  _GatewaySelectionScreenState createState() => _GatewaySelectionScreenState();
}

class _GatewaySelectionScreenState extends State<GatewaySelectionScreen> {
  final FirebaseCommandService _commandService = FirebaseCommandService();
  List<Map<String, dynamic>> _gateways = [];
  bool _loading = true;
  
  @override
  void initState() {
    super.initState();
    _loadGateways();
  }
  
  Future<void> _loadGateways() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    
    final gateways = await _commandService.getUserGateways(user.uid);
    setState(() {
      _gateways = gateways;
      _loading = false;
    });
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('Chọn Gateway')),
      body: _loading
          ? Center(child: CircularProgressIndicator())
          : ListView.builder(
              itemCount: _gateways.length,
              itemBuilder: (context, index) {
                final gateway = _gateways[index];
                final info = gateway['info'] as Map?;
                final status = gateway['status'] as Map?;
                final online = status?['online'] as bool? ?? false;
                
                return Card(
                  margin: EdgeInsets.all(8),
                  child: ListTile(
                    leading: Icon(
                      Icons.router,
                      color: online ? Colors.green : Colors.grey,
                    ),
                    title: Text(info?['name'] ?? 'Gateway'),
                    subtitle: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text('MAC: ${gateway['mac']}'),
                        Text(online ? '🟢 Online' : '🔴 Offline'),
                        if (online) Text('Nodes: ${status?['connected_nodes'] ?? 0}'),
                      ],
                    ),
                    trailing: online
                        ? Icon(Icons.arrow_forward_ios)
                        : null,
                    onTap: online
                        ? () => _startProvisioning(gateway['mac'])
                        : null,
                  ),
                );
              },
            ),
    );
  }
  
  Future<void> _startProvisioning(String gatewayMAC) async {
    // Navigate to provisioning screen
    Navigator.pushNamed(
      context,
      '/provisioning-progress',
      arguments: {'gatewayMAC': gatewayMAC},
    );
  }
}
```

**File:** `lib/screens/provisioning_progress_screen.dart`

```dart
class ProvisioningProgressScreen extends StatefulWidget {
  final String gatewayMAC;
  
  const ProvisioningProgressScreen({required this.gatewayMAC});
  
  @override
  _ProvisioningProgressScreenState createState() => _ProvisioningProgressScreenState();
}

class _ProvisioningProgressScreenState extends State<ProvisioningProgressScreen> {
  final FirebaseCommandService _commandService = FirebaseCommandService();
  final ProvisioningSessionService _sessionService = ProvisioningSessionService();
  
  String? _commandId;
  String _status = 'Đang gửi lệnh...';
  int _nodesDiscovered = 0;
  int _timeRemaining = 600; // 10 minutes in seconds
  Timer? _timer;
  
  @override
  void initState() {
    super.initState();
    _startProvisioning();
  }
  
  Future<void> _startProvisioning() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    
    try {
      // Send start provisioning command
      _commandId = await _commandService.sendCommand(
        userUID: user.uid,
        gatewayMAC: widget.gatewayMAC,
        commandType: 'start_provisioning',
        params: {
          'durationMs': 600000, // 10 minutes
          'maxNodes': 10,
        },
      );
      
      setState(() => _status = 'Đang chờ Gateway...');
      
      // Listen to command results
      _commandService
          .listenCommandResult(user.uid, widget.gatewayMAC)
          .listen((result) {
        if (result['last_command_id'] == _commandId) {
          setState(() {
            _status = result['message'] ?? 'Đang xử lý...';
            
            final progress = result['progress'] as Map?;
            if (progress != null) {
              _nodesDiscovered = progress['nodes_discovered'] ?? 0;
              _timeRemaining = (progress['time_remaining_ms'] ?? 0) ~/ 1000;
            }
          });
        }
      });
      
      // Start countdown timer
      _timer = Timer.periodic(Duration(seconds: 1), (timer) {
        if (_timeRemaining > 0) {
          setState(() => _timeRemaining--);
        } else {
          timer.cancel();
        }
      });
      
    } catch (e) {
      setState(() => _status = 'Lỗi: $e');
    }
  }
  
  Future<void> _stopProvisioning() async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    
    await _commandService.sendCommand(
      userUID: user.uid,
      gatewayMAC: widget.gatewayMAC,
      commandType: 'stop_provisioning',
      params: {},
    );
  }
  
  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }
  
  @override
  Widget build(BuildContext context) {
    final minutes = _timeRemaining ~/ 60;
    final seconds = _timeRemaining % 60;
    
    return Scaffold(
      appBar: AppBar(title: Text('Thêm Nodes')),
      body: Padding(
        padding: EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Card(
              child: Padding(
                padding: EdgeInsets.all(16),
                child: Column(
                  children: [
                    CircularProgressIndicator(),
                    SizedBox(height: 16),
                    Text(_status, style: Theme.of(context).textTheme.headline6),
                    SizedBox(height: 8),
                    Text('Thời gian còn lại: ${minutes}:${seconds.toString().padLeft(2, '0')}'),
                  ],
                ),
              ),
            ),
            SizedBox(height: 24),
            Card(
              child: Padding(
                padding: EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text('📡 Nodes tìm thấy: $_nodesDiscovered',
                        style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
                    SizedBox(height: 16),
                    Text('Hướng dẫn:', style: TextStyle(fontWeight: FontWeight.bold)),
                    SizedBox(height: 8),
                    Text('• Bật nguồn các node cần thêm'),
                    Text('• Đảm bảo node ở gần Gateway'),
                    Text('• Chờ node tự động kết nối'),
                  ],
                ),
              ),
            ),
            Spacer(),
            ElevatedButton(
              onPressed: _stopProvisioning,
              child: Text('Dừng quét'),
              style: ElevatedButton.styleFrom(
                backgroundColor: Colors.orange,
                padding: EdgeInsets.symmetric(vertical: 16),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
```

### Bước 3.3: Update HomeScreen FAB Logic

**File:** `lib/screens/home_screen.dart`

```dart
// Update FAB onPressed
FloatingActionButton(
  onPressed: () async {
    final user = FirebaseAuth.instance.currentUser;
    if (user == null) return;
    
    // Check if user has gateways
    final commandService = FirebaseCommandService();
    final gateways = await commandService.getUserGateways(user.uid);
    
    if (gateways.isEmpty) {
      // No gateways → Direct to BLE scan
      Navigator.pushNamed(context, '/ble-scan', arguments: 'gateway');
    } else {
      // Has gateways → Show options
      showModalBottomSheet(
        context: context,
        builder: (context) => AddDeviceSheet(),
      );
    }
  },
  child: Icon(Icons.add),
)
```

### Deliverables Phase 3:
- ✅ Firebase command services
- ✅ Gateway selection UI
- ✅ Provisioning progress UI
- ✅ Real-time status updates
- ✅ Integration với HomeScreen

**Thời gian:** 2-3 ngày

---

## PHASE 4: Testing & Polish (1 ngày)

### Test Scenarios

#### Test 1: No Gateway
- [ ] User mở app lần đầu
- [ ] Nhấn FAB (+)
- [ ] → Vào trực tiếp BLE scan
- [ ] Provision Gateway thành công
- [ ] Gateway xuất hiện trong Firebase

#### Test 2: Has Gateway - Add Node
- [ ] User đã có Gateway
- [ ] Nhấn FAB (+)
- [ ] → Show bottom sheet
- [ ] Chọn "Thêm Node"
- [ ] → Show gateway list
- [ ] Chọn gateway online
- [ ] → Bắt đầu provisioning
- [ ] Bật node mới
- [ ] → Node xuất hiện trong app
- [ ] Countdown chính xác
- [ ] Dừng provisioning hoạt động

#### Test 3: Gateway Offline
- [ ] User chọn gateway offline
- [ ] → Disabled, không cho chọn
- [ ] Hoặc show warning message

#### Test 4: Multiple Nodes
- [ ] Start provisioning
- [ ] Bật 3 nodes cùng lúc
- [ ] → Tất cả 3 nodes join thành công
- [ ] Counter cập nhật real-time

#### Test 5: Timeout
- [ ] Start provisioning
- [ ] Không bật node nào
- [ ] → Sau 10 phút tự dừng
- [ ] Show kết quả: 0 nodes

### Polish Items
- [ ] Loading states
- [ ] Error messages
- [ ] Success animations
- [ ] Sound effects (optional)
- [ ] Haptic feedback
- [ ] Empty states
- [ ] Network error handling
- [ ] Offline mode handling

### Deliverables Phase 4:
- ✅ All test cases pass
- ✅ UX polished
- ✅ Error handling complete
- ✅ Documentation updated

**Thời gian:** 1 ngày

---

## Timeline Summary

| Phase | Task | Duration | Dependencies |
|-------|------|----------|--------------|
| 1 | Firebase Setup | 30 min - 1 hr | None |
| 2 | Firmware Implementation | 1-2 days | Phase 1 |
| 3 | Mobile App Implementation | 2-3 days | Phase 1 |
| 4 | Testing & Polish | 1 day | Phase 2 & 3 |

**Total: 5-7 ngày**

---

## Next Steps

**Bước tiếp theo ngay bây giờ:**

1. ✅ **Apply Firebase Structure** (30 phút)
   - Login Firebase Console
   - Apply security rules
   - Create test data

2. 🔧 **Start Phase 2** (Firmware)
   - Tạo FirebaseCommandPoller service
   - Test với manual Firebase writes

3. 📱 **Start Phase 3** (Mobile App) - có thể song song
   - Tạo Firebase services
   - Build UI screens

**Bạn muốn bắt đầu từ đâu?**
- A. Apply Firebase Rules ngay (30 phút)
- B. Code Firmware trước (1-2 ngày)
- C. Code Mobile App trước (2-3 ngày)
- D. Song song cả Firmware và Mobile App

Tôi recommend: **A → B → C** (tuần tự) hoặc **A → (B + C song song)** nếu có nhiều người.
