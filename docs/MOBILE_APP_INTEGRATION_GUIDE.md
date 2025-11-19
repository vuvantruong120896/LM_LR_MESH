# Mobile App BLE Provisioning Integration Guide

## Overview

This guide provides step-by-step instructions for integrating the Kagri BLE provisioning system into React Native or Flutter mobile applications. It covers device discovery, BLE communication, payload management, and error handling.

---

## Part 1: React Native Implementation

### Setup & Dependencies

#### Required Packages
```bash
npm install react-native-ble-plx
npm install react-native-permissions
npm install @react-native-camera/camera  # For potential QR code scanning
npm install axios  # For HTTP requests (if needed)
```

#### iOS Setup (Podfile)
```ruby
# ios/Podfile
target 'YourApp' do
  pod 'react-native-ble-plx', :path => '../node_modules/react-native-ble-plx'
  
  post_install do |installer|
    installer.pods_project.targets.each do |target|
      target.build_configurations.each do |config|
        config.build_settings['GCC_PREPROCESSOR_DEFINITIONS'] ||= ['$(inherited)', 'RCT_DEV=1']
      end
    end
  end
end
```

#### Info.plist (iOS)
```xml
<key>NSBluetoothAlwaysAndWhenInUseUsageDescription</key>
<string>Kagri needs Bluetooth to provision devices</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>Kagri uses Bluetooth LE to communicate with Gateway and Node devices</string>
<key>NSLocalNetworkUsageDescription</key>
<string>Kagri may use local network for WiFi configuration</string>
```

#### AndroidManifest.xml (Android)
```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />
```

---

### Device Discovery Implementation

#### BLE Service & Constants
```typescript
// src/services/constants.ts
export const BLE_CONSTANTS = {
  // Gateway
  GATEWAY_SERVICE_UUID: '0000ffb0-0000-1000-8000-00805f9b34fb',
  GATEWAY_PROV_CHAR: '0000ffb1-0000-1000-8000-00805f9b34fb',
  GATEWAY_RESPONSE_CHAR: '0000ffb2-0000-1000-8000-00805f9b34fb',
  
  // Node
  NODE_SERVICE_UUID: '0000ffc0-0000-1000-8000-00805f9b34fb',
  NODE_PROV_CHAR: '0000ffc1-0000-1000-8000-00805f9b34fb',
  NODE_RESPONSE_CHAR: '0000ffc2-0000-1000-8000-00805f9b34fb',
  
  // Scan parameters
  SCAN_TIMEOUT: 15000,  // 15 seconds
  CONNECTION_TIMEOUT: 10000,  // 10 seconds
  NOTIFICATION_TIMEOUT: 30000,  // 30 seconds
};

export const DEVICE_NAMES = {
  GATEWAY_PREFIX: 'KAGRI-GW-',
  NODE_PREFIX: 'KAGRI-NODE-',
};
```

#### BLE Device Discovery Service
```typescript
// src/services/BleDiscoveryService.ts
import { BleManager } from 'react-native-ble-plx';
import { NativeEventEmitter } from 'react-native';
import { BLE_CONSTANTS, DEVICE_NAMES } from './constants';

export interface KagriDevice {
  id: string;
  name: string;
  type: 'gateway' | 'node';
  rssi: number;
}

export class BleDiscoveryService {
  private manager: BleManager;
  private discoveredDevices: Map<string, KagriDevice> = new Map();
  private eventEmitter: NativeEventEmitter;

  constructor() {
    this.manager = new BleManager();
    this.eventEmitter = new NativeEventEmitter();
  }

  /**
   * Start scanning for Kagri devices
   */
  async startScan(onDeviceFound?: (device: KagriDevice) => void): Promise<void> {
    try {
      this.discoveredDevices.clear();

      const subscription = this.manager.onStateChange((state) => {
        if (state === 'PoweredOn') {
          this.performScan(onDeviceFound);
          subscription.remove();
        }
      }, true);

    } catch (error) {
      console.error('Scan error:', error);
      throw new Error('Failed to start BLE scan');
    }
  }

  private async performScan(onDeviceFound?: (device: KagriDevice) => void): Promise<void> {
    try {
      this.manager.startDeviceScan(
        null,
        { allowDuplicates: false },
        (error, device) => {
          if (error) {
            console.error('Scan error:', error);
            return;
          }

          if (!device?.name) return;

          // Check if device is a Kagri device
          if (
            device.name.startsWith(DEVICE_NAMES.GATEWAY_PREFIX) ||
            device.name.startsWith(DEVICE_NAMES.NODE_PREFIX)
          ) {
            const kagriDevice = this.parseKagriDevice(device);
            
            if (kagriDevice) {
              this.discoveredDevices.set(device.id, kagriDevice);
              onDeviceFound?.(kagriDevice);
            }
          }
        }
      );

      // Stop scan after timeout
      setTimeout(() => {
        this.manager.stopDeviceScan();
      }, BLE_CONSTANTS.SCAN_TIMEOUT);

    } catch (error) {
      console.error('Perform scan error:', error);
      throw error;
    }
  }

  private parseKagriDevice(device: any): KagriDevice | null {
    const name = device.name || '';
    let type: 'gateway' | 'node' | null = null;

    if (name.startsWith(DEVICE_NAMES.GATEWAY_PREFIX)) {
      type = 'gateway';
    } else if (name.startsWith(DEVICE_NAMES.NODE_PREFIX)) {
      type = 'node';
    }

    if (!type) return null;

    return {
      id: device.id,
      name: name,
      type: type,
      rssi: device.rssi || -100,
    };
  }

  /**
   * Get all discovered devices
   */
  getDiscoveredDevices(): KagriDevice[] {
    return Array.from(this.discoveredDevices.values())
      .sort((a, b) => b.rssi - a.rssi); // Sort by signal strength
  }

  /**
   * Stop scanning
   */
  stopScan(): void {
    this.manager.stopDeviceScan();
  }

  /**
   * Cleanup resources
   */
  destroy(): void {
    this.stopScan();
    this.manager.destroy();
  }
}
```

#### Discovery Screen Component
```typescript
// src/screens/DeviceDiscoveryScreen.tsx
import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  FlatList,
  TouchableOpacity,
  ActivityIndicator,
  Alert,
} from 'react-native';
import { BleDiscoveryService, KagriDevice } from '../services/BleDiscoveryService';

export const DeviceDiscoveryScreen = ({ navigation }: any) => {
  const [devices, setDevices] = useState<KagriDevice[]>([]);
  const [scanning, setScanning] = useState(false);
  const [discoveryService] = useState(() => new BleDiscoveryService());

  useEffect(() => {
    startDiscovery();
    return () => {
      discoveryService.destroy();
    };
  }, []);

  const startDiscovery = async () => {
    try {
      setScanning(true);
      await discoveryService.startScan((device) => {
        setDevices((prev) => {
          const existing = prev.find((d) => d.id === device.id);
          if (existing) {
            return prev.map((d) => (d.id === device.id ? device : d));
          }
          return [...prev, device];
        });
      });
    } catch (error) {
      Alert.alert('Error', 'Failed to start scanning');
      setScanning(false);
    }
  };

  const handleDeviceSelect = (device: KagriDevice) => {
    discoveryService.stopScan();
    
    if (device.type === 'gateway') {
      navigation.navigate('GatewayProvisioning', { device });
    } else {
      navigation.navigate('NodeProvisioning', { device });
    }
  };

  return (
    <View style={{ flex: 1, padding: 16 }}>
      <Text style={{ fontSize: 18, fontWeight: 'bold', marginBottom: 16 }}>
        Discover Devices
      </Text>

      {scanning && <ActivityIndicator size="large" color="#0000ff" />}

      <FlatList
        data={devices}
        keyExtractor={(item) => item.id}
        renderItem={({ item }) => (
          <TouchableOpacity
            onPress={() => handleDeviceSelect(item)}
            style={{
              padding: 12,
              borderRadius: 8,
              backgroundColor: '#f0f0f0',
              marginBottom: 8,
            }}
          >
            <Text style={{ fontSize: 14, fontWeight: '600' }}>{item.name}</Text>
            <Text style={{ fontSize: 12, color: '#666' }}>
              {item.type === 'gateway' ? 'Gateway' : 'Node'} • Signal: {item.rssi} dBm
            </Text>
          </TouchableOpacity>
        )}
      />
    </View>
  );
};
```

---

### Gateway Provisioning Implementation

#### Gateway Service
```typescript
// src/services/GatewayProvisioningService.ts
import { BleManager, Device } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import { BLE_CONSTANTS } from './constants';

export interface GatewayProvisioningPayload {
  userUID: string;
  isWiFi: boolean;
  wifiSSID?: string;
  wifiPassword?: string;
  timestamp: number;
}

export interface GatewayProvisioningResponse {
  status: 'success' | 'error';
  message: string;
  gatewayMAC?: string;
  deviceMode?: number;
  code?: string;
}

export class GatewayProvisioningService {
  private manager: BleManager;
  private connectedDevice: Device | null = null;

  constructor() {
    this.manager = new BleManager();
  }

  /**
   * Connect to gateway device
   */
  async connectToGateway(deviceId: string): Promise<void> {
    try {
      const device = await this.manager.connectToDevice(deviceId, {
        timeoutMillis: BLE_CONSTANTS.CONNECTION_TIMEOUT,
      });

      await device.discoverAllServicesAndCharacteristics();
      this.connectedDevice = device;
    } catch (error) {
      throw new Error(`Failed to connect to gateway: ${error}`);
    }
  }

  /**
   * Provision gateway with WiFi credentials
   */
  async provisionGatewayWiFi(
    userUID: string,
    ssid: string,
    password: string,
    onProgress?: (message: string) => void
  ): Promise<GatewayProvisioningResponse> {
    if (!this.connectedDevice) {
      throw new Error('Not connected to device');
    }

    onProgress?.('Preparing provisioning data...');

    const payload: GatewayProvisioningPayload = {
      userUID,
      isWiFi: true,
      wifiSSID: ssid,
      wifiPassword: password,
      timestamp: Math.floor(Date.now() / 1000),
    };

    return this.sendProvisioningData(payload, onProgress);
  }

  /**
   * Provision gateway for Cellular mode
   */
  async provisionGatewayCellular(
    userUID: string,
    onProgress?: (message: string) => void
  ): Promise<GatewayProvisioningResponse> {
    if (!this.connectedDevice) {
      throw new Error('Not connected to device');
    }

    onProgress?.('Preparing provisioning data...');

    const payload: GatewayProvisioningPayload = {
      userUID,
      isWiFi: false,
      timestamp: Math.floor(Date.now() / 1000),
    };

    return this.sendProvisioningData(payload, onProgress);
  }

  private async sendProvisioningData(
    payload: GatewayProvisioningPayload,
    onProgress?: (message: string) => void
  ): Promise<GatewayProvisioningResponse> {
    if (!this.connectedDevice) {
      throw new Error('Not connected to device');
    }

    try {
      // Convert payload to JSON string
      const jsonString = JSON.stringify(payload);
      onProgress?.(`Sending ${jsonString.length} bytes...`);

      // Convert to base64 for BLE transmission
      const base64Payload = Buffer.from(jsonString).toString('base64');

      // Write to provisioning characteristic
      await this.connectedDevice.writeCharacteristicWithoutResponseForService(
        BLE_CONSTANTS.GATEWAY_SERVICE_UUID,
        BLE_CONSTANTS.GATEWAY_PROV_CHAR,
        base64Payload
      );

      onProgress?.('Waiting for device response...');

      // Wait for response notification
      return await this.waitForResponse(
        BLE_CONSTANTS.GATEWAY_RESPONSE_CHAR
      );

    } catch (error) {
      console.error('Provisioning error:', error);
      throw new Error(`Provisioning failed: ${error}`);
    }
  }

  private async waitForResponse(
    characteristicUUID: string
  ): Promise<GatewayProvisioningResponse> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Provisioning response timeout'));
      }, BLE_CONSTANTS.NOTIFICATION_TIMEOUT);

      const subscription = this.connectedDevice?.monitorCharacteristicForService(
        BLE_CONSTANTS.GATEWAY_SERVICE_UUID,
        characteristicUUID,
        (error, characteristic) => {
          clearTimeout(timeout);
          subscription?.remove();

          if (error) {
            reject(error);
            return;
          }

          if (characteristic?.value) {
            try {
              // Decode base64 to JSON
              const jsonString = Buffer.from(
                characteristic.value,
                'base64'
              ).toString('utf-8');
              const response: GatewayProvisioningResponse = JSON.parse(jsonString);
              resolve(response);
            } catch (parseError) {
              reject(new Error('Failed to parse response'));
            }
          }
        }
      );
    });
  }

  /**
   * Disconnect from device
   */
  async disconnect(): Promise<void> {
    if (this.connectedDevice) {
      await this.manager.cancelDeviceConnection(this.connectedDevice.id);
      this.connectedDevice = null;
    }
  }

  /**
   * Cleanup resources
   */
  destroy(): void {
    this.disconnect();
    this.manager.destroy();
  }
}
```

#### Gateway Provisioning Screen
```typescript
// src/screens/GatewayProvisioningScreen.tsx
import React, { useState } from 'react';
import {
  View,
  Text,
  TextInput,
  TouchableOpacity,
  Alert,
  ActivityIndicator,
  ScrollView,
} from 'react-native';
import { GatewayProvisioningService } from '../services/GatewayProvisioningService';
import { KagriDevice } from '../services/BleDiscoveryService';

interface Props {
  route: { params: { device: KagriDevice } };
  navigation: any;
}

export const GatewayProvisioningScreen = ({ route, navigation }: Props) => {
  const device = route.params.device;
  const [mode, setMode] = useState<'wifi' | 'cellular'>('wifi');
  const [ssid, setSSID] = useState('');
  const [password, setPassword] = useState('');
  const [userUID, setUserUID] = useState('');
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState('');
  const [service] = useState(() => new GatewayProvisioningService());

  const handleProvision = async () => {
    try {
      if (!userUID) {
        Alert.alert('Error', 'Please enter User UID');
        return;
      }

      if (mode === 'wifi' && (!ssid || !password)) {
        Alert.alert('Error', 'Please enter WiFi SSID and password');
        return;
      }

      setLoading(true);
      setStatus('Connecting to device...');

      await service.connectToGateway(device.id);
      setStatus('Connected! Sending provisioning data...');

      let response;
      if (mode === 'wifi') {
        response = await service.provisionGatewayWiFi(
          userUID,
          ssid,
          password,
          (msg) => setStatus(msg)
        );
      } else {
        response = await service.provisionGatewayCellular(
          userUID,
          (msg) => setStatus(msg)
        );
      }

      if (response.status === 'success') {
        Alert.alert(
          'Success',
          `Gateway provisioned!\nMAC: ${response.gatewayMAC}`,
          [
            {
              text: 'OK',
              onPress: () => navigation.goBack(),
            },
          ]
        );
      } else {
        Alert.alert(
          'Provisioning Error',
          response.message || 'Unknown error occurred'
        );
      }
    } catch (error) {
      Alert.alert('Error', error instanceof Error ? error.message : 'Unknown error');
    } finally {
      setLoading(false);
      await service.disconnect();
    }
  };

  return (
    <ScrollView style={{ flex: 1, padding: 16 }}>
      <Text style={{ fontSize: 18, fontWeight: 'bold', marginBottom: 16 }}>
        Provision Gateway: {device.name}
      </Text>

      {/* Mode Selection */}
      <View style={{ marginBottom: 20 }}>
        <Text style={{ fontSize: 14, fontWeight: '600', marginBottom: 8 }}>
          Connection Mode
        </Text>
        <View style={{ flexDirection: 'row', gap: 10 }}>
          <TouchableOpacity
            style={{
              flex: 1,
              padding: 12,
              borderRadius: 8,
              backgroundColor: mode === 'wifi' ? '#4CAF50' : '#e0e0e0',
            }}
            onPress={() => setMode('wifi')}
          >
            <Text style={{ color: mode === 'wifi' ? 'white' : 'black' }}>WiFi</Text>
          </TouchableOpacity>
          <TouchableOpacity
            style={{
              flex: 1,
              padding: 12,
              borderRadius: 8,
              backgroundColor: mode === 'cellular' ? '#4CAF50' : '#e0e0e0',
            }}
            onPress={() => setMode('cellular')}
          >
            <Text style={{ color: mode === 'cellular' ? 'white' : 'black' }}>
              Cellular
            </Text>
          </TouchableOpacity>
        </View>
      </View>

      {/* User UID */}
      <View style={{ marginBottom: 16 }}>
        <Text style={{ fontSize: 12, fontWeight: '600', marginBottom: 4 }}>
          User UID
        </Text>
        <TextInput
          placeholder="Enter your user ID"
          value={userUID}
          onChangeText={setUserUID}
          style={{
            borderWidth: 1,
            borderColor: '#ccc',
            borderRadius: 8,
            padding: 10,
          }}
          editable={!loading}
        />
      </View>

      {/* WiFi Fields */}
      {mode === 'wifi' && (
        <>
          <View style={{ marginBottom: 16 }}>
            <Text style={{ fontSize: 12, fontWeight: '600', marginBottom: 4 }}>
              WiFi SSID
            </Text>
            <TextInput
              placeholder="Network name"
              value={ssid}
              onChangeText={setSSID}
              style={{
                borderWidth: 1,
                borderColor: '#ccc',
                borderRadius: 8,
                padding: 10,
              }}
              editable={!loading}
            />
          </View>

          <View style={{ marginBottom: 16 }}>
            <Text style={{ fontSize: 12, fontWeight: '600', marginBottom: 4 }}>
              WiFi Password
            </Text>
            <TextInput
              placeholder="Network password"
              value={password}
              onChangeText={setPassword}
              secureTextEntry
              style={{
                borderWidth: 1,
                borderColor: '#ccc',
                borderRadius: 8,
                padding: 10,
              }}
              editable={!loading}
            />
          </View>
        </>
      )}

      {/* Status */}
      {status && (
        <View
          style={{
            padding: 12,
            backgroundColor: '#f5f5f5',
            borderRadius: 8,
            marginBottom: 16,
          }}
        >
          <Text style={{ fontSize: 12, color: '#666' }}>{status}</Text>
        </View>
      )}

      {/* Loading */}
      {loading && <ActivityIndicator size="large" color="#0000ff" />}

      {/* Provision Button */}
      <TouchableOpacity
        style={{
          padding: 14,
          backgroundColor: '#2196F3',
          borderRadius: 8,
          marginTop: 20,
        }}
        onPress={handleProvision}
        disabled={loading}
      >
        <Text style={{ color: 'white', textAlign: 'center', fontWeight: '600' }}>
          {loading ? 'Provisioning...' : 'Provision Gateway'}
        </Text>
      </TouchableOpacity>
    </ScrollView>
  );
};
```

---

### Node Provisioning Implementation

#### Node Service
```typescript
// src/services/NodeProvisioningService.ts
import { BleManager, Device } from 'react-native-ble-plx';
import { Buffer } from 'buffer';
import { BLE_CONSTANTS } from './constants';

export interface NodeProvisioningPayload {
  userUID: string;
  gatewayMAC: string;
  timestamp: number;
}

export interface NodeProvisioningResponse {
  status: 'success' | 'error';
  message: string;
  nodeAddress?: number;
  gatewayMAC?: string;
  code?: string;
}

export class NodeProvisioningService {
  private manager: BleManager;
  private connectedDevice: Device | null = null;

  constructor() {
    this.manager = new BleManager();
  }

  /**
   * Connect to node device
   */
  async connectToNode(deviceId: string): Promise<void> {
    try {
      const device = await this.manager.connectToDevice(deviceId, {
        timeoutMillis: BLE_CONSTANTS.CONNECTION_TIMEOUT,
      });

      await device.discoverAllServicesAndCharacteristics();
      this.connectedDevice = device;
    } catch (error) {
      throw new Error(`Failed to connect to node: ${error}`);
    }
  }

  /**
   * Provision node with gateway information
   */
  async provisionNode(
    userUID: string,
    gatewayMAC: string,
    onProgress?: (message: string) => void
  ): Promise<NodeProvisioningResponse> {
    if (!this.connectedDevice) {
      throw new Error('Not connected to device');
    }

    // Validate MAC address format
    if (!this.validateMACAddress(gatewayMAC)) {
      throw new Error('Invalid Gateway MAC format. Use format: AA:BB:CC:DD:EE:FF');
    }

    onProgress?.('Preparing provisioning data...');

    const payload: NodeProvisioningPayload = {
      userUID,
      gatewayMAC,
      timestamp: Math.floor(Date.now() / 1000),
    };

    try {
      // Convert payload to JSON string
      const jsonString = JSON.stringify(payload);
      onProgress?.(`Sending ${jsonString.length} bytes...`);

      // Convert to base64 for BLE transmission
      const base64Payload = Buffer.from(jsonString).toString('base64');

      // Write to provisioning characteristic
      await this.connectedDevice.writeCharacteristicWithoutResponseForService(
        BLE_CONSTANTS.NODE_SERVICE_UUID,
        BLE_CONSTANTS.NODE_PROV_CHAR,
        base64Payload
      );

      onProgress?.('Waiting for device response...');

      // Wait for response notification
      return await this.waitForResponse(
        BLE_CONSTANTS.NODE_RESPONSE_CHAR
      );

    } catch (error) {
      console.error('Provisioning error:', error);
      throw new Error(`Provisioning failed: ${error}`);
    }
  }

  private async waitForResponse(
    characteristicUUID: string
  ): Promise<NodeProvisioningResponse> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Provisioning response timeout'));
      }, BLE_CONSTANTS.NOTIFICATION_TIMEOUT);

      const subscription = this.connectedDevice?.monitorCharacteristicForService(
        BLE_CONSTANTS.NODE_SERVICE_UUID,
        characteristicUUID,
        (error, characteristic) => {
          clearTimeout(timeout);
          subscription?.remove();

          if (error) {
            reject(error);
            return;
          }

          if (characteristic?.value) {
            try {
              // Decode base64 to JSON
              const jsonString = Buffer.from(
                characteristic.value,
                'base64'
              ).toString('utf-8');
              const response: NodeProvisioningResponse = JSON.parse(jsonString);
              resolve(response);
            } catch (parseError) {
              reject(new Error('Failed to parse response'));
            }
          }
        }
      );
    });
  }

  private validateMACAddress(mac: string): boolean {
    const macRegex = /^([0-9A-Fa-f]{2}:){5}([0-9A-Fa-f]{2})$/;
    return macRegex.test(mac);
  }

  /**
   * Disconnect from device
   */
  async disconnect(): Promise<void> {
    if (this.connectedDevice) {
      await this.manager.cancelDeviceConnection(this.connectedDevice.id);
      this.connectedDevice = null;
    }
  }

  /**
   * Cleanup resources
   */
  destroy(): void {
    this.disconnect();
    this.manager.destroy();
  }
}
```

#### Node Provisioning Screen
```typescript
// src/screens/NodeProvisioningScreen.tsx
import React, { useState } from 'react';
import {
  View,
  Text,
  TextInput,
  TouchableOpacity,
  Alert,
  ActivityIndicator,
  ScrollView,
} from 'react-native';
import { NodeProvisioningService } from '../services/NodeProvisioningService';
import { KagriDevice } from '../services/BleDiscoveryService';

interface Props {
  route: { params: { device: KagriDevice; gatewayMAC?: string } };
  navigation: any;
}

export const NodeProvisioningScreen = ({ route, navigation }: Props) => {
  const device = route.params.device;
  const [userUID, setUserUID] = useState('');
  const [gatewayMAC, setGatewayMAC] = useState(route.params.gatewayMAC || '');
  const [loading, setLoading] = useState(false);
  const [status, setStatus] = useState('');
  const [service] = useState(() => new NodeProvisioningService());

  const handleProvision = async () => {
    try {
      if (!userUID) {
        Alert.alert('Error', 'Please enter User UID');
        return;
      }

      if (!gatewayMAC) {
        Alert.alert('Error', 'Please enter Gateway MAC address');
        return;
      }

      setLoading(true);
      setStatus('Connecting to device...');

      await service.connectToNode(device.id);
      setStatus('Connected! Sending provisioning data...');

      const response = await service.provisionNode(
        userUID,
        gatewayMAC,
        (msg) => setStatus(msg)
      );

      if (response.status === 'success') {
        Alert.alert(
          'Success',
          `Node provisioned!\nAddress: ${response.nodeAddress}`,
          [
            {
              text: 'OK',
              onPress: () => navigation.goBack(),
            },
          ]
        );
      } else {
        Alert.alert(
          'Provisioning Error',
          response.message || 'Unknown error occurred'
        );
      }
    } catch (error) {
      Alert.alert('Error', error instanceof Error ? error.message : 'Unknown error');
    } finally {
      setLoading(false);
      await service.disconnect();
    }
  };

  return (
    <ScrollView style={{ flex: 1, padding: 16 }}>
      <Text style={{ fontSize: 18, fontWeight: 'bold', marginBottom: 16 }}>
        Provision Node: {device.name}
      </Text>

      {/* User UID */}
      <View style={{ marginBottom: 16 }}>
        <Text style={{ fontSize: 12, fontWeight: '600', marginBottom: 4 }}>
          User UID
        </Text>
        <TextInput
          placeholder="Enter your user ID"
          value={userUID}
          onChangeText={setUserUID}
          style={{
            borderWidth: 1,
            borderColor: '#ccc',
            borderRadius: 8,
            padding: 10,
          }}
          editable={!loading}
        />
      </View>

      {/* Gateway MAC */}
      <View style={{ marginBottom: 16 }}>
        <Text style={{ fontSize: 12, fontWeight: '600', marginBottom: 4 }}>
          Gateway MAC Address
        </Text>
        <TextInput
          placeholder="AA:BB:CC:DD:EE:FF"
          value={gatewayMAC}
          onChangeText={setGatewayMAC}
          style={{
            borderWidth: 1,
            borderColor: '#ccc',
            borderRadius: 8,
            padding: 10,
          }}
          editable={!loading}
        />
        <Text style={{ fontSize: 11, color: '#999', marginTop: 4 }}>
          Format: AA:BB:CC:DD:EE:FF
        </Text>
      </View>

      {/* Status */}
      {status && (
        <View
          style={{
            padding: 12,
            backgroundColor: '#f5f5f5',
            borderRadius: 8,
            marginBottom: 16,
          }}
        >
          <Text style={{ fontSize: 12, color: '#666' }}>{status}</Text>
        </View>
      )}

      {/* Loading */}
      {loading && <ActivityIndicator size="large" color="#0000ff" />}

      {/* Provision Button */}
      <TouchableOpacity
        style={{
          padding: 14,
          backgroundColor: '#2196F3',
          borderRadius: 8,
          marginTop: 20,
        }}
        onPress={handleProvision}
        disabled={loading}
      >
        <Text style={{ color: 'white', textAlign: 'center', fontWeight: '600' }}>
          {loading ? 'Provisioning...' : 'Provision Node'}
        </Text>
      </TouchableOpacity>
    </ScrollView>
  );
};
```

---

## Part 2: Flutter Implementation

### Setup & Dependencies

#### pubspec.yaml
```yaml
dependencies:
  flutter:
    sdk: flutter
  flutter_blue_plus: ^1.10.0
  provider: ^6.0.0
  json_annotation: ^4.8.0

dev_dependencies:
  build_runner: ^2.3.0
  json_serializable: ^6.6.0
```

#### Permissions (Android & iOS)

**android/app/src/main/AndroidManifest.xml:**
```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

**ios/Runner/Info.plist:**
```xml
<key>NSBluetoothPeripheralUsageDescription</key>
<string>Kagri needs Bluetooth to provision devices</string>
<key>NSBluetoothAlwaysAndWhenInUseUsageDescription</key>
<string>Kagri uses Bluetooth LE to communicate with Gateway and Node</string>
```

### Gateway Provisioning (Flutter)

```dart
// lib/services/gateway_provisioning_service.dart
import 'dart:async';
import 'dart:convert';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class GatewayProvisioningService {
  static const String serviceUUID = '0000ffb0-0000-1000-8000-00805f9b34fb';
  static const String provCharacteristicUUID = '0000ffb1-0000-1000-8000-00805f9b34fb';
  static const String responseCharacteristicUUID = '0000ffb2-0000-1000-8000-00805f9b34fb';

  late BluetoothDevice device;
  BluetoothCharacteristic? responseCharacteristic;

  Future<Map<String, dynamic>> provisionGatewayWiFi({
    required String deviceId,
    required String userUID,
    required String ssid,
    required String password,
  }) async {
    try {
      // Find device
      device = await _findDevice(deviceId);

      // Connect
      await device.connect();
      await device.discoverServices();

      // Prepare payload
      final payload = {
        'userUID': userUID,
        'isWiFi': true,
        'wifiSSID': ssid,
        'wifiPassword': password,
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      };

      // Send provisioning data
      await _sendProvisioningData(payload);

      // Wait for response
      final response = await _waitForResponse();

      await device.disconnect();
      return response;
    } catch (e) {
      await device.disconnect();
      rethrow;
    }
  }

  Future<Map<String, dynamic>> provisionGatewayCellular({
    required String deviceId,
    required String userUID,
  }) async {
    try {
      device = await _findDevice(deviceId);
      await device.connect();
      await device.discoverServices();

      final payload = {
        'userUID': userUID,
        'isWiFi': false,
        'timestamp': DateTime.now().millisecondsSinceEpoch ~/ 1000,
      };

      await _sendProvisioningData(payload);
      final response = await _waitForResponse();

      await device.disconnect();
      return response;
    } catch (e) {
      await device.disconnect();
      rethrow;
    }
  }

  Future<void> _sendProvisioningData(Map<String, dynamic> payload) async {
    final jsonString = jsonEncode(payload);
    final bytes = utf8.encode(jsonString);

    final services = await device.discoverServices();
    final service = services.firstWhere(
      (s) => s.uuid.toString() == serviceUUID,
      orElse: () => throw Exception('Service not found'),
    );

    final characteristic = service.characteristics.firstWhere(
      (c) => c.uuid.toString() == provCharacteristicUUID,
      orElse: () => throw Exception('Characteristic not found'),
    );

    await characteristic.write(bytes, withoutResponse: true);
  }

  Future<Map<String, dynamic>> _waitForResponse() async {
    final services = await device.discoverServices();
    final service = services.firstWhere(
      (s) => s.uuid.toString() == serviceUUID,
    );

    responseCharacteristic = service.characteristics.firstWhere(
      (c) => c.uuid.toString() == responseCharacteristicUUID,
    );

    final completer = Completer<Map<String, dynamic>>();
    final timeout = Future.delayed(
      Duration(seconds: 30),
      () => completer.completeError(Exception('Response timeout')),
    );

    responseCharacteristic!.setNotifyValue(true);
    responseCharacteristic!.onValueReceived.listen((value) {
      if (value.isNotEmpty) {
        try {
          final jsonString = utf8.decode(value);
          final response = jsonDecode(jsonString) as Map<String, dynamic>;
          completer.complete(response);
        } catch (e) {
          completer.completeError(e);
        }
      }
    });

    return Future.any([completer.future, timeout]);
  }

  Future<BluetoothDevice> _findDevice(String deviceId) async {
    final systemDevices = await FlutterBluePlus.systemDevices;
    return systemDevices.firstWhere(
      (d) => d.remoteId.str == deviceId,
      orElse: () => throw Exception('Device not found'),
    );
  }
}
```

---

## Error Handling Best Practices

### Common Errors & Recovery

```typescript
// Error handling wrapper
export const handleProvisioningError = (error: any): string => {
  const message = error instanceof Error ? error.message : String(error);

  if (message.includes('Connection timeout')) {
    return 'Device not responding. Make sure it\'s nearby and not connected to another device.';
  }
  if (message.includes('Invalid payload')) {
    return 'The provisioning data was invalid. Please check your inputs.';
  }
  if (message.includes('WiFi')) {
    return 'WiFi configuration failed. Check your credentials and try again.';
  }
  if (message.includes('MAC')) {
    return 'Invalid MAC address format. Use format: AA:BB:CC:DD:EE:FF';
  }
  return 'An error occurred. Please try again.';
};
```

---

## Testing & Validation

### Unit Tests (React Native)
```typescript
import { GatewayProvisioningService } from '../services/GatewayProvisioningService';

describe('GatewayProvisioningService', () => {
  let service: GatewayProvisioningService;

  beforeEach(() => {
    service = new GatewayProvisioningService();
  });

  test('should validate MAC address format', () => {
    expect(service.validateMACAddress('AA:BB:CC:DD:EE:FF')).toBe(true);
    expect(service.validateMACAddress('INVALID')).toBe(false);
  });

  test('should encode provisioning payload correctly', () => {
    const payload = {
      userUID: 'test-user',
      isWiFi: true,
    };
    // Test encoding logic
  });
});
```

---

## Deployment Checklist

- [ ] BLE permissions configured for iOS and Android
- [ ] All UUIDs verified against firmware
- [ ] Payload encoding/decoding tested
- [ ] Error messages user-friendly
- [ ] Timeout values appropriate
- [ ] Memory cleanup on disconnect
- [ ] Retry logic implemented
- [ ] App-level error logging configured
- [ ] Beta testing with real devices completed
- [ ] Privacy policy updated for BLE usage

