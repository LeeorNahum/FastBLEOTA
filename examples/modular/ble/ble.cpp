/**
 * @file ble.cpp
 * @brief BLE management implementation
 */

#include "ble.h"

// BLE server instance
static NimBLEServer* ble_server = nullptr;

// Server callbacks for connection tracking (NimBLE 2.x signatures)
class ServerCallbacks : public NimBLEServerCallbacks {
public:
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    Serial.println("[BLE] Client connected");
    
    // Allow multiple connections
    NimBLEDevice::getAdvertising()->start();
  }
  
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    Serial.printf("[BLE] Client disconnected (reason: %d)\n", reason);
    
    // Resume advertising
    NimBLEDevice::getAdvertising()->start();
  }
};

void bleStart(const char* deviceName) {
  Serial.println("[BLE] Initializing...");
  
  // Initialize NimBLE
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(256);  // Larger MTU for faster OTA
  
  // Create server
  ble_server = NimBLEDevice::createServer();
  ble_server->setCallbacks(new ServerCallbacks());
  
  // Start all services
  bleStartOTA();
  bleStartDemoService();
  
  // Configure advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(deviceName);
  pAdvertising->addServiceUUID(bleGetOTAServiceUUID());
  pAdvertising->addServiceUUID(DEMO_SERVICE_UUID);
  pAdvertising->start();
  
  Serial.printf("[BLE] Started. Address: %s\n", 
                NimBLEDevice::getAddress().toString().c_str());
}

bool bleIsDeviceConnected() {
  return ble_server && ble_server->getConnectedCount() > 0;
}

NimBLEServer* bleGetServer() {
  return ble_server;
}
