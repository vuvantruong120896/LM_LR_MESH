# Mobile App Integration Guide - Firebase Command Queue

## Tổng quan

Guide này hướng dẫn Mobile App developers tích hợp với Firebase Command Queue để điều khiển Gateway từ xa.

## Prerequisites

### 1. Firebase Setup

```bash
# For React Native
npm install @react-native-firebase/app @react-native-firebase/database

# For Flutter
flutter pub add firebase_core firebase_database
```

### 2. Firebase Configuration

**React Native:**
```javascript
// firebase.config.js
import database from '@react-native-firebase/database';

const firebaseConfig = {
  databaseURL: 'https://kagri-iot-default-rtdb.asia-southeast1.firebasedatabase.app/',
};

export const db = database();
```

**Flutter:**
```dart
// firebase_config.dart
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';

final databaseRef = FirebaseDatabase.instance.ref();
```

## Command Queue API

### 1. Check Gateway Status

Trước khi gửi command, check xem Gateway có online không.

**React Native:**
```javascript
async function checkGatewayStatus(gatewayId) {
  try {
    const snapshot = await database()
      .ref(`gateway_status/${gatewayId}`)
      .once('value');
    
    const status = snapshot.val();
    
    if (!status) {
      return {
        online: false,
        error: 'Gateway not found'
      };
    }
    
    return {
      online: status.online,
      canAcceptCommands: status.can_accept_commands,
      provisioningActive: status.provisioning_active,
      connectedNodes: status.connected_nodes,
      lastHeartbeat: status.last_heartbeat
    };
  } catch (error) {
    console.error('Error checking gateway status:', error);
    return { online: false, error: error.message };
  }
}

// Usage
const status = await checkGatewayStatus('0xABCD');
if (!status.online) {
  showError('Gateway is offline');
  return;
}
```

**Flutter:**
```dart
Future<Map<String, dynamic>> checkGatewayStatus(String gatewayId) async {
  try {
    final snapshot = await databaseRef
        .child('gateway_status/$gatewayId')
        .get();
    
    if (!snapshot.exists) {
      return {'online': false, 'error': 'Gateway not found'};
    }
    
    final status = snapshot.value as Map;
    return {
      'online': status['online'] ?? false,
      'canAcceptCommands': status['can_accept_commands'] ?? false,
      'provisioningActive': status['provisioning_active'] ?? false,
      'connectedNodes': status['connected_nodes'] ?? 0,
      'lastHeartbeat': status['last_heartbeat']
    };
  } catch (e) {
    print('Error checking gateway status: $e');
    return {'online': false, 'error': e.toString()};
  }
}

// Usage
final status = await checkGatewayStatus('0xABCD');
if (!(status['online'] ?? false)) {
  showError('Gateway is offline');
  return;
}
```

### 2. Send Command

**React Native:**
```javascript
async function sendCommand(gatewayId, commandType, params = {}) {
  try {
    // Generate unique command ID
    const commandId = `cmd_${Date.now()}`;
    
    // Create command object
    const command = {
      type: commandType,
      params: params,
      timestamp: Date.now(),
      status: 'pending',
      priority: getPriority(commandType),
      created_by: 'mobile_app',
      user_id: getCurrentUserId()
    };
    
    // Write to Firebase
    await database()
      .ref(`commands/${gatewayId}/pending/${commandId}`)
      .set(command);
    
    console.log(`Command ${commandId} sent successfully`);
    return commandId;
    
  } catch (error) {
    console.error('Error sending command:', error);
    throw error;
  }
}

function getPriority(commandType) {
  const priorities = {
    'set_netkey': 0,           // High priority
    'start_provisioning': 1,   // Normal priority
    'stop_provisioning': 2     // Low priority
  };
  return priorities[commandType] || 1;
}

// Usage examples:

// 1. Start Provisioning
const cmdId = await sendCommand('0xABCD', 'start_provisioning', {
  durationMs: 600000,  // 10 minutes
  maxSessions: 4
});

// 2. Stop Provisioning
const cmdId = await sendCommand('0xABCD', 'stop_provisioning', {});

// 3. Set Network Key
const cmdId = await sendCommand('0xABCD', 'set_netkey', {
  networkKey: '0102030405060708090A0B0C0D0E0F10',
  authToken: '1112131415161718',
  networkId: 4660,
  keyVersion: 2
});
```

**Flutter:**
```dart
Future<String> sendCommand(
  String gatewayId, 
  String commandType, 
  Map<String, dynamic> params
) async {
  try {
    // Generate unique command ID
    final commandId = 'cmd_${DateTime.now().millisecondsSinceEpoch}';
    
    // Create command object
    final command = {
      'type': commandType,
      'params': params,
      'timestamp': DateTime.now().millisecondsSinceEpoch,
      'status': 'pending',
      'priority': _getPriority(commandType),
      'created_by': 'mobile_app',
      'user_id': _getCurrentUserId()
    };
    
    // Write to Firebase
    await databaseRef
        .child('commands/$gatewayId/pending/$commandId')
        .set(command);
    
    print('Command $commandId sent successfully');
    return commandId;
    
  } catch (e) {
    print('Error sending command: $e');
    rethrow;
  }
}

int _getPriority(String commandType) {
  const priorities = {
    'set_netkey': 0,
    'start_provisioning': 1,
    'stop_provisioning': 2
  };
  return priorities[commandType] ?? 1;
}

// Usage examples:

// 1. Start Provisioning
final cmdId = await sendCommand('0xABCD', 'start_provisioning', {
  'durationMs': 600000,
  'maxSessions': 4
});

// 2. Stop Provisioning
final cmdId = await sendCommand('0xABCD', 'stop_provisioning', {});

// 3. Set Network Key
final cmdId = await sendCommand('0xABCD', 'set_netkey', {
  'networkKey': '0102030405060708090A0B0C0D0E0F10',
  'authToken': '1112131415161718',
  'networkId': 4660,
  'keyVersion': 2
});
```

### 3. Listen for Command Results

**React Native:**
```javascript
function listenForCommandResult(gatewayId, commandId, callback) {
  const resultRef = database().ref(`command_results/${gatewayId}`);
  
  const listener = resultRef.on('value', snapshot => {
    const result = snapshot.val();
    
    if (result && result.last_command_id === commandId) {
      callback({
        status: result.status,
        message: result.message,
        timestamp: result.timestamp
      });
      
      // Unsubscribe after receiving result
      if (result.status === 'success' || result.status === 'failed') {
        resultRef.off('value', listener);
      }
    }
  });
  
  // Return unsubscribe function
  return () => resultRef.off('value', listener);
}

// Usage
const unsubscribe = listenForCommandResult('0xABCD', commandId, (result) => {
  if (result.status === 'success') {
    showSuccess(result.message);
  } else if (result.status === 'failed') {
    showError(result.message);
  } else {
    showLoading('Processing...');
  }
});

// Clean up when component unmounts
useEffect(() => {
  return () => unsubscribe();
}, []);
```

**Flutter:**
```dart
StreamSubscription listenForCommandResult(
  String gatewayId,
  String commandId,
  Function(Map<String, dynamic>) callback
) {
  final resultRef = databaseRef.child('command_results/$gatewayId');
  
  return resultRef.onValue.listen((event) {
    if (event.snapshot.exists) {
      final result = event.snapshot.value as Map;
      
      if (result['last_command_id'] == commandId) {
        callback({
          'status': result['status'],
          'message': result['message'],
          'timestamp': result['timestamp']
        });
      }
    }
  });
}

// Usage
late StreamSubscription subscription;

subscription = listenForCommandResult('0xABCD', commandId, (result) {
  if (result['status'] == 'success') {
    showSuccess(result['message']);
    subscription.cancel();
  } else if (result['status'] == 'failed') {
    showError(result['message']);
    subscription.cancel();
  } else {
    showLoading('Processing...');
  }
});

// Clean up
@override
void dispose() {
  subscription.cancel();
  super.dispose();
}
```

### 4. Complete Command Flow with UI

**React Native Example:**
```javascript
import React, { useState, useEffect } from 'react';
import { View, Button, Text, ActivityIndicator } from 'react-native';

function ProvisioningControl({ gatewayId }) {
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState('');
  const [gatewayOnline, setGatewayOnline] = useState(false);

  useEffect(() => {
    // Check gateway status on mount
    checkGatewayStatus(gatewayId).then(status => {
      setGatewayOnline(status.online);
    });
  }, [gatewayId]);

  const startProvisioning = async () => {
    if (!gatewayOnline) {
      setStatus('Gateway is offline');
      return;
    }

    setLoading(true);
    setStatus('Sending command...');

    try {
      const commandId = await sendCommand('0xABCD', 'start_provisioning', {
        durationMs: 600000,
        maxSessions: 4
      });

      setStatus('Waiting for gateway response...');

      // Listen for result
      const unsubscribe = listenForCommandResult('0xABCD', commandId, (result) => {
        setLoading(false);
        setStatus(result.message);
        
        if (result.status !== 'processing') {
          unsubscribe();
        }
      });

    } catch (error) {
      setLoading(false);
      setStatus(`Error: ${error.message}`);
    }
  };

  const stopProvisioning = async () => {
    // Similar implementation
  };

  return (
    <View>
      <Text>Gateway Status: {gatewayOnline ? 'Online' : 'Offline'}</Text>
      
      <Button 
        title="Start Provisioning" 
        onPress={startProvisioning}
        disabled={loading || !gatewayOnline}
      />
      
      <Button 
        title="Stop Provisioning" 
        onPress={stopProvisioning}
        disabled={loading || !gatewayOnline}
      />
      
      {loading && <ActivityIndicator />}
      {status && <Text>{status}</Text>}
    </View>
  );
}
```

**Flutter Example:**
```dart
import 'package:flutter/material.dart';

class ProvisioningControl extends StatefulWidget {
  final String gatewayId;
  
  ProvisioningControl({required this.gatewayId});
  
  @override
  _ProvisioningControlState createState() => _ProvisioningControlState();
}

class _ProvisioningControlState extends State<ProvisioningControl> {
  bool _loading = false;
  String _status = '';
  bool _gatewayOnline = false;
  StreamSubscription? _subscription;

  @override
  void initState() {
    super.initState();
    _checkGatewayStatus();
  }

  Future<void> _checkGatewayStatus() async {
    final status = await checkGatewayStatus(widget.gatewayId);
    setState(() {
      _gatewayOnline = status['online'] ?? false;
    });
  }

  Future<void> _startProvisioning() async {
    if (!_gatewayOnline) {
      setState(() => _status = 'Gateway is offline');
      return;
    }

    setState(() {
      _loading = true;
      _status = 'Sending command...';
    });

    try {
      final commandId = await sendCommand(
        widget.gatewayId,
        'start_provisioning',
        {'durationMs': 600000, 'maxSessions': 4}
      );

      setState(() => _status = 'Waiting for gateway response...');

      _subscription = listenForCommandResult(
        widget.gatewayId,
        commandId,
        (result) {
          setState(() {
            _loading = false;
            _status = result['message'];
          });
          
          if (result['status'] != 'processing') {
            _subscription?.cancel();
          }
        }
      );

    } catch (e) {
      setState(() {
        _loading = false;
        _status = 'Error: $e';
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Text('Gateway Status: ${_gatewayOnline ? "Online" : "Offline"}'),
        
        ElevatedButton(
          onPressed: _loading || !_gatewayOnline ? null : _startProvisioning,
          child: Text('Start Provisioning'),
        ),
        
        ElevatedButton(
          onPressed: _loading || !_gatewayOnline ? null : _stopProvisioning,
          child: Text('Stop Provisioning'),
        ),
        
        if (_loading) CircularProgressIndicator(),
        if (_status.isNotEmpty) Text(_status),
      ],
    );
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }
}
```

## Error Handling

### Common Errors

```javascript
// 1. Gateway Offline
if (!status.online) {
  showError('Gateway is offline. Please check connection.');
}

// 2. Gateway Busy
if (!status.canAcceptCommands) {
  showError('Gateway is busy processing another command.');
}

// 3. Command Timeout
setTimeout(() => {
  if (commandStillPending) {
    showError('Command timeout. Gateway may be offline.');
  }
}, 30000); // 30 second timeout

// 4. Invalid Parameters
try {
  validateCommandParams(commandType, params);
} catch (error) {
  showError(`Invalid parameters: ${error.message}`);
}
```

## Best Practices

### 1. Command Validation

```javascript
function validateStartProvisioningParams(params) {
  if (!params.durationMs || params.durationMs < 60000) {
    throw new Error('Duration must be at least 60 seconds');
  }
  if (params.durationMs > 3600000) {
    throw new Error('Duration cannot exceed 1 hour');
  }
  if (!params.maxSessions || params.maxSessions < 1 || params.maxSessions > 10) {
    throw new Error('Max sessions must be between 1 and 10');
  }
}

function validateSetNetkeyParams(params) {
  if (!params.networkKey || !/^[0-9A-Fa-f]{32}$/.test(params.networkKey)) {
    throw new Error('Network key must be 32 hex characters');
  }
  if (!params.authToken || !/^[0-9A-Fa-f]{16}$/.test(params.authToken)) {
    throw new Error('Auth token must be 16 hex characters');
  }
  if (!params.networkId || params.networkId < 0 || params.networkId > 65535) {
    throw new Error('Network ID must be between 0 and 65535');
  }
}
```

### 2. Retry Logic

```javascript
async function sendCommandWithRetry(gatewayId, commandType, params, maxRetries = 3) {
  for (let i = 0; i < maxRetries; i++) {
    try {
      return await sendCommand(gatewayId, commandType, params);
    } catch (error) {
      if (i === maxRetries - 1) throw error;
      
      console.log(`Retry ${i + 1}/${maxRetries} after error:`, error);
      await new Promise(resolve => setTimeout(resolve, 2000 * (i + 1)));
    }
  }
}
```

### 3. Command Queue Management

```javascript
// Clean up old completed/failed commands
async function cleanupOldCommands(gatewayId) {
  const cutoffTime = Date.now() - (24 * 60 * 60 * 1000); // 24 hours ago
  
  const completedRef = database().ref(`commands/${gatewayId}/completed`);
  const failedRef = database().ref(`commands/${gatewayId}/failed`);
  
  const completedSnapshot = await completedRef.once('value');
  const failedSnapshot = await failedRef.once('value');
  
  // Delete old completed commands
  completedSnapshot.forEach(child => {
    if (child.val().completedAt < cutoffTime) {
      child.ref.remove();
    }
  });
  
  // Delete old failed commands
  failedSnapshot.forEach(child => {
    if (child.val().failedAt < cutoffTime) {
      child.ref.remove();
    }
  });
}
```

## Testing

### Unit Tests (Jest)

```javascript
describe('Command Queue', () => {
  test('should send start provisioning command', async () => {
    const commandId = await sendCommand('0xABCD', 'start_provisioning', {
      durationMs: 600000,
      maxSessions: 4
    });
    
    expect(commandId).toMatch(/^cmd_\d+$/);
  });
  
  test('should validate network key format', () => {
    expect(() => {
      validateSetNetkeyParams({
        networkKey: 'invalid',
        authToken: '1112131415161718',
        networkId: 4660
      });
    }).toThrow('Network key must be 32 hex characters');
  });
});
```

## Troubleshooting

### Command Not Executed

1. Check gateway status: `gateway_status/{gatewayId}`
2. Verify command is in `pending` queue
3. Check Firebase security rules allow write
4. Verify Gateway polling interval (5-10 seconds)
5. Check Gateway logs for errors

### Timeout Issues

1. Increase timeout threshold (default: 30s)
2. Check Gateway WiFi connection
3. Verify Firebase connection stable
4. Check if Gateway is processing another command

### Result Not Received

1. Verify listener is attached before sending command
2. Check `command_results/{gatewayId}` path exists
3. Verify `last_command_id` matches sent command
4. Check Firebase security rules allow read

## Related Documentation

- [FIREBASE_COMMAND_QUEUE.md](FIREBASE_COMMAND_QUEUE.md) - Complete architecture
- [FIREBASE_INTEGRATION.md](FIREBASE_INTEGRATION.md) - Gateway Firebase setup
- Firebase JavaScript SDK: https://firebase.google.com/docs/reference/js
- Firebase Flutter SDK: https://firebase.google.com/docs/flutter/setup

---

**Version:** 1.0.0  
**Last Updated:** October 17, 2025  
**Target Audience:** Mobile App Developers
