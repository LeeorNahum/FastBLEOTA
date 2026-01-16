#include "FastBLEOTA.h"

NimBLEService* FastBLEOTA::_pService = nullptr;
NimBLECharacteristic* FastBLEOTA::_pCharacteristic = nullptr;
size_t FastBLEOTA::_expectedSize = 0;
size_t FastBLEOTA::_receivedSize = 0;
bool FastBLEOTA::_sizeReceived = false;

FastBLEOTACallbacks* FastBLEOTA::_callbacks = nullptr;

#define OTA_SERVICE_UUID        "4e8cbb5e-bc0f-4aab-a6e8-55e662418bef"
#define OTA_CHARACTERISTIC_UUID "513fcda9-f46d-4e41-ac4f-42b768495a85"

// NimBLE 2.x callback signature requires NimBLEConnInfo& parameter
class FastBLEOTA::CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    FastBLEOTA::processData((uint8_t*)value.data(), value.length());
  }
};

void FastBLEOTA::begin(NimBLEServer* pServer) {
  FastBLEOTA::reset();
  _pService = pServer->createService(OTA_SERVICE_UUID);

  _pCharacteristic = _pService->createCharacteristic(
    OTA_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );

  _pCharacteristic->setCallbacks(new CharacteristicCallbacks());

  _pService->start();
}

void FastBLEOTA::reset() {
  FastBLEOTA::_expectedSize = 0;
  FastBLEOTA::_receivedSize = 0;
  FastBLEOTA::_sizeReceived = false;
  Update.abort();
}

void FastBLEOTA::onOTAStart(size_t expectedSize) {
  if (FastBLEOTA::_callbacks) FastBLEOTA::_callbacks->onOTAStart(expectedSize);
}

void FastBLEOTA::onOTAProgress(size_t receivedSize, size_t expectedSize) {
  if (FastBLEOTA::_callbacks) FastBLEOTA::_callbacks->onOTAProgress(receivedSize, expectedSize);
}

void FastBLEOTA::onOTAComplete() {
  if (FastBLEOTA::_callbacks) FastBLEOTA::_callbacks->onOTAComplete();
}

void FastBLEOTA::onOTAError(fastbleota_error_t errorCode) {
  if (FastBLEOTA::_callbacks) FastBLEOTA::_callbacks->onOTAError(errorCode);
}

void FastBLEOTA::processData(const uint8_t* data, size_t length) {
  if (!FastBLEOTA::_sizeReceived) {
    if (length != sizeof(uint32_t)) {
      FastBLEOTA::onOTAError(FASTBLEOTA_ERROR_SIZE_MISMATCH);
      return;
    }
    FastBLEOTA::_expectedSize = *((uint32_t*)data);
    FastBLEOTA::_sizeReceived = true;

    if (!Update.begin(FastBLEOTA::_expectedSize)) {
      Update.printError(Serial);
      FastBLEOTA::onOTAError(FASTBLEOTA_ERROR_START_UPDATE);
      return;
    }

    FastBLEOTA::onOTAStart(FastBLEOTA::_expectedSize);
  }
  else {
    size_t chunkSize = length;
    size_t written = Update.write((uint8_t*)data, chunkSize);
    if (written != chunkSize) {
      Update.printError(Serial);
      FastBLEOTA::onOTAError(FASTBLEOTA_ERROR_WRITE_CHUNK);
      return;
    }

    FastBLEOTA::_receivedSize += written;
    FastBLEOTA::onOTAProgress(FastBLEOTA::_receivedSize, FastBLEOTA::_expectedSize);

    if (FastBLEOTA::_receivedSize > FastBLEOTA::_expectedSize) {
      Update.end();
      FastBLEOTA::onOTAError(FASTBLEOTA_ERROR_RECEIVED_MORE);
      return;
    }

    if (FastBLEOTA::_receivedSize == FastBLEOTA::_expectedSize) {
      if (Update.end()) {
        FastBLEOTA::onOTAComplete();
      }
      else {
        Update.printError(Serial);
        FastBLEOTA::onOTAError(FASTBLEOTA_ERROR_FINALIZE_UPDATE);
      }
    }
  }
}

void FastBLEOTA::setCallbacks(FastBLEOTACallbacks* callbacks) {
  if (callbacks) FastBLEOTA::_callbacks = callbacks;
}

const char* FastBLEOTA::getServiceUUID() {
  return OTA_SERVICE_UUID;
}