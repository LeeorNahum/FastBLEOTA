/**
 * @file ble_demo_service.cpp
 * @brief Demo service implementation
 */

#include "ble_demo_service.h"
#include "../ble.h"

// Characteristic pointer
static NimBLECharacteristic* demo_message_characteristic = nullptr;

// Subscription state
static bool demo_subscribed = false;

// Characteristic callbacks
class DemoMessageCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    demo_subscribed = subValue > 0;
    Serial.printf("[Demo] Subscription: %s\n", demo_subscribed ? "enabled" : "disabled");
  }
  
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.println("[Demo] Message read by client");
  }
};

void bleStartDemoService() {
  NimBLEService* service = bleGetServer()->createService(DEMO_SERVICE_UUID);
  
  // Create message characteristic
  demo_message_characteristic = service->createCharacteristic(
    DEMO_MESSAGE_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  
  // User Description Descriptor (2901)
  NimBLEDescriptor* userDesc = demo_message_characteristic->createDescriptor(NimBLEUUID("2901"));
  userDesc->setValue("Build Info Message");
  
  // Presentation Format Descriptor (2904)
  NimBLE2904* formatDesc = (NimBLE2904*)demo_message_characteristic->createDescriptor(NimBLEUUID("2904"));
  formatDesc->setFormat(NimBLE2904::FORMAT_UTF8);
  formatDesc->setExponent(0x00);
  formatDesc->setUnit(0x2700);      // Unitless
  formatDesc->setNamespace(0x00);   // Custom namespace
  formatDesc->setDescription(0x0000);
  
  // Set initial value
  demo_message_characteristic->setValue("Waiting for connection...");
  demo_message_characteristic->setCallbacks(new DemoMessageCallbacks());
  
  service->start();
  
  Serial.printf("[Demo] Service started (UUID: %s)\n", DEMO_SERVICE_UUID);
}

bool bleDemoServiceSubscribed() {
  return demo_subscribed;
}

void bleSendDemoMessage(const char* message, bool notify) {
  if (!demo_message_characteristic) return;
  
  demo_message_characteristic->setValue(message);
  
  if (notify) {
    demo_message_characteristic->notify();
  }
}
