/**
 * @file main.cpp
 * @brief FastBLEOTA Modular Example
 * 
 * Demonstrates a clean modular BLE architecture where:
 * - main.cpp contains ZERO FastBLEOTA code
 * - All BLE management is in ble/ble.cpp
 * - OTA functionality is wrapped in ble/ble_ota/
 * - Additional services (like demo) are in ble/ble_demo_service/
 * 
 * This pattern keeps main.cpp focused on application logic while
 * BLE concerns are encapsulated in dedicated modules.
 * 
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#include <Arduino.h>
#include "ble/ble.h"
#include "ble/ble_demo_service/ble_demo_service.h"

// Build-time demo message (changes when you recompile)
// Use PlatformIO's built-in $UNIX_TIME to get unique value per build
#ifndef DEMO_MESSAGE
  #define DEMO_MESSAGE "Built: " __DATE__ " " __TIME__
#endif

// Device name for BLE advertising
#define DEVICE_NAME "FBO-Modular"

// Demo notification interval (ms)
#define DEMO_INTERVAL_MS 2000

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println();
  Serial.println("===============================");
  Serial.println("FastBLEOTA Modular Example");
  Serial.printf("Demo Message: %s\n", DEMO_MESSAGE);
  Serial.println("===============================");
  Serial.println();
  
  // Initialize BLE (creates server, starts OTA and demo services)
  bleStart(DEVICE_NAME);
  
  Serial.println("Ready! Use BLE_OTA.py to upload new firmware.");
  Serial.println("After OTA, the demo message will show the new build time.");
  Serial.println();
}

void loop() {
  static unsigned long lastDemoNotify = 0;
  
  // Send demo notification every DEMO_INTERVAL_MS
  if (millis() - lastDemoNotify >= DEMO_INTERVAL_MS) {
    lastDemoNotify = millis();
    
    if (bleIsDeviceConnected() && bleDemoServiceSubscribed()) {
      bleSendDemoMessage(DEMO_MESSAGE, true);
    }
  }
  
  delay(100);
}

// =============================================================================
// Entry Points (Platform-specific)
// =============================================================================

#if defined(ESP32) && !defined(CONFIG_AUTOSTART_ARDUINO)
extern "C" void app_main() {
  setup();
  while (true) {
    loop();
  }
}
#endif
