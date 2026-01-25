/**
 * @file main.cpp
 * @brief FastBLEOTA Basic Example
 * 
 * Demonstrates minimal setup for OTA updates via BLE.
 * Works on ESP32 and nRF52 boards.
 * 
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <FastBLEOTA.h>

// Device name for BLE advertising
#define DEVICE_NAME "FastBLEOTA-Demo"

// OTA Callbacks (optional but recommended)
class MyOTACallbacks : public FastBLEOTACallbacks {
public:
  void onStart(size_t expectedSize, uint32_t expectedCRC) override {
    Serial.println("=== OTA Update Started ===");
    Serial.printf("  Size: %u bytes\n", expectedSize);
    if (expectedCRC != 0) {
      Serial.printf("  Expected CRC: 0x%08X\n", expectedCRC);
    }
  }
  
  void onProgress(size_t bytesReceived, size_t bytesExpected, float percent) override {
    // Only log every 10%
    static int lastTen = -1;
    int currentTen = (int)percent / 10;
    if (currentTen != lastTen) {
      lastTen = currentTen;
      Serial.printf("  Progress: %.1f%% (%u / %u bytes)\n", percent, bytesReceived, bytesExpected);
    }
  }
  
  void onComplete() override {
    Serial.println("=== OTA Update Complete ===");
    Serial.println("Restarting...");
  }
  
  void onError(fbo_error_t error, const char* errorString) override {
    Serial.printf("=== OTA Error: %s ===\n", errorString);
  }
  
  void onAbort() override {
    Serial.println("=== OTA Aborted ===");
  }
};

// Global instances
NimBLEServer* pServer = nullptr;
MyOTACallbacks otaCallbacks;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("===============================");
  Serial.printf("FastBLEOTA v%s Demo\n", FastBLEOTA.getVersion());
  Serial.printf("Platform: %s\n", FastBLEOTA.getPlatform());
  Serial.println("===============================");
  Serial.println();
  
  // Initialize BLE
  Serial.println("Initializing BLE...");
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(256);  // Request larger MTU for faster transfers
  
  // Create server
  pServer = NimBLEDevice::createServer();
  
  // Initialize FastBLEOTA
  Serial.println("Initializing FastBLEOTA...");
  FastBLEOTA.setCallbacks(&otaCallbacks);
  FastBLEOTA.startService();
  
  // Start advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(DEVICE_NAME);
  pAdvertising->addServiceUUID(FastBLEOTAClass::SERVICE_UUID);
  pAdvertising->start();
  
  Serial.println();
  Serial.println("Ready for OTA updates!");
  Serial.printf("Device address: %s\n", NimBLEDevice::getAddress().toString().c_str());
  Serial.printf("Service UUID: %s\n", FastBLEOTAClass::SERVICE_UUID.toString().c_str());
  Serial.println();
  Serial.println("Use BLE_OTA.py to upload firmware:");
  Serial.printf("  python BLE_OTA.py -a %s -f firmware.bin\n", 
                NimBLEDevice::getAddress().toString().c_str());
  Serial.println();
}

void loop() {
  // Main loop - OTA is handled in BLE callbacks
  // You can add your application logic here
  
  // Optional: Print status periodically
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    lastPrint = millis();
    
    if (pServer->getConnectedCount() > 0) {
      Serial.println("[Main] Client connected");
    }
    
    if (FastBLEOTA.isActive()) {
      Serial.printf("[Main] OTA in progress: %.1f%%\n", FastBLEOTA.getProgress());
    }
  }
  
  delay(100);
}

// =============================================================================
// Entry Points (Platform-specific)
// =============================================================================

#if defined(ESP32) && !defined(CONFIG_AUTOSTART_ARDUINO)
// ESP-IDF with Arduino as component
extern "C" void app_main() {
  setup();
  while (true) {
    loop();
  }
}
#endif
