/**
 * @file FastBLEOTA.cpp
 * @brief Fast BLE Over-The-Air Update Library
 * 
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#include "FastBLEOTA.h"
#include <crc.h>

// BLE Callbacks
class FastBLEOTAClass::DataCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue attValue = pCharacteristic->getValue();
    FastBLEOTA.processDataPacket(attValue.data(), attValue.size());
  }
};

class FastBLEOTAClass::ControlCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() >= 1) {
      FastBLEOTA.processControlCommand(value[0]);
    }
  }
  
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    if (subValue > 0) {
      FastBLEOTA.sendProgressNotification();
    }
  }
};

FastBLEOTAClass::FastBLEOTAClass() :
  _pService(nullptr),
  _pDataCharacteristic(nullptr),
  _pControlCharacteristic(nullptr),
  _pProgressCharacteristic(nullptr),
  _state(FBO_STATE_IDLE),
  _lastError(FBO_ERROR_NONE),
  _expectedSize(0),
  _receivedSize(0),
  _expectedCRC(0),
  _calculatedCRC(0),
  _lastNotifiedPercent(0),
  _chunkCount(0),
  _callbacks(nullptr)
{
}

void FastBLEOTAClass::begin(NimBLEServer* pServer) {
  #ifdef FBO_DEBUG
  const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  crc_t testCRCValue = crc_init();
  testCRCValue = crc_update(testCRCValue, testData, sizeof(testData));
  uint32_t testCRC = (uint32_t)crc_finalize(testCRCValue);
  Serial.printf("[FBO] CRC self-test: 0x%08lX (expected 0xCBF43926) - %s\n",
                (unsigned long)testCRC, (testCRC == 0xCBF43926) ? "PASS" : "FAIL");
  #endif
  
  _pService = pServer->createService(FBO_SERVICE_UUID);
  
  createDataCharacteristic();
  createControlCharacteristic();
  createProgressCharacteristic();
  
  _pService->start();
  reset();
  
  FBO_LOG("FastBLEOTA v%s initialized on %s", FASTBLEOTA_VERSION_STRING, getPlatform());
}

void FastBLEOTAClass::createDataCharacteristic() {
  _pDataCharacteristic = _pService->createCharacteristic(
    FBO_DATA_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  _pDataCharacteristic->setCallbacks(new DataCharacteristicCallbacks());
  
  NimBLEDescriptor* userDesc = _pDataCharacteristic->createDescriptor(NimBLEUUID("2901"));
  userDesc->setValue("OTA Firmware Data");
  
  NimBLE2904* formatDesc = (NimBLE2904*)_pDataCharacteristic->createDescriptor(NimBLEUUID("2904"));
  formatDesc->setFormat(NimBLE2904::FORMAT_OPAQUE);
  formatDesc->setUnit(0x2700);
}

void FastBLEOTAClass::createControlCharacteristic() {
  _pControlCharacteristic = _pService->createCharacteristic(
    FBO_CONTROL_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  _pControlCharacteristic->setCallbacks(new ControlCharacteristicCallbacks());
  
  NimBLEDescriptor* userDesc = _pControlCharacteristic->createDescriptor(NimBLEUUID("2901"));
  userDesc->setValue("OTA Control");
  
  NimBLE2904* formatDesc = (NimBLE2904*)_pControlCharacteristic->createDescriptor(NimBLEUUID("2904"));
  formatDesc->setFormat(NimBLE2904::FORMAT_UINT8);
  formatDesc->setUnit(0x2700);
}

void FastBLEOTAClass::createProgressCharacteristic() {
  _pProgressCharacteristic = _pService->createCharacteristic(
    FBO_PROGRESS_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  
  NimBLEDescriptor* userDesc = _pProgressCharacteristic->createDescriptor(NimBLEUUID("2901"));
  userDesc->setValue("OTA Progress");
  
  NimBLE2904* formatDesc = (NimBLE2904*)_pProgressCharacteristic->createDescriptor(NimBLEUUID("2904"));
  formatDesc->setFormat(NimBLE2904::FORMAT_OPAQUE);
  formatDesc->setUnit(0x2700);
  
  fbo_progress_t progress = {0};
  progress.state = FBO_STATE_IDLE;
  _pProgressCharacteristic->setValue((uint8_t*)&progress, sizeof(progress));
}

void FastBLEOTAClass::setCallbacks(FastBLEOTACallbacks* callbacks) {
  _callbacks = callbacks;
}

fbo_state_t FastBLEOTAClass::getState() { return _state; }
fbo_error_t FastBLEOTAClass::getLastError() { return _lastError; }

float FastBLEOTAClass::getProgress() {
  if (_expectedSize == 0) return 0.0f;
  return ((float)_receivedSize * 100.0f) / (float)_expectedSize;
}

void FastBLEOTAClass::reset() {
  if (_state == FBO_STATE_RECEIVING || _state == FBO_STATE_VALIDATING) {
    OTAStorageBackend.abort();
  }
  
  _state = FBO_STATE_IDLE;
  _lastError = FBO_ERROR_NONE;
  _expectedSize = 0;
  _receivedSize = 0;
  _expectedCRC = 0;
  _calculatedCRC = (uint32_t)crc_init();
  _lastNotifiedPercent = 0;
  _chunkCount = 0;
  
  sendProgressNotification();
}

bool FastBLEOTAClass::isActive() { return _state == FBO_STATE_RECEIVING; }

const NimBLEUUID& FastBLEOTAClass::getServiceUUID() { return FBO_SERVICE_UUID; }
const NimBLEUUID& FastBLEOTAClass::getDataCharacteristicUUID() { return FBO_DATA_CHARACTERISTIC_UUID; }
const NimBLEUUID& FastBLEOTAClass::getControlCharacteristicUUID() { return FBO_CONTROL_CHARACTERISTIC_UUID; }
const NimBLEUUID& FastBLEOTAClass::getProgressCharacteristicUUID() { return FBO_PROGRESS_CHARACTERISTIC_UUID; }
const char* FastBLEOTAClass::getVersion() { return FASTBLEOTA_VERSION_STRING; }
const char* FastBLEOTAClass::getPlatform() { return OTAStorageBackend.platformName(); }

void FastBLEOTAClass::processDataPacket(const uint8_t* data, size_t length) {
  if (_state == FBO_STATE_ERROR) return;
  
  if (_state == FBO_STATE_IDLE) {
    processInitPacket(data, length);
  } else if (_state == FBO_STATE_RECEIVING) {
    processDataChunk(data, length);
  }
}

void FastBLEOTAClass::processInitPacket(const uint8_t* data, size_t length) {
  if (length != FBO_INIT_PACKET_SIZE) {
    FBO_LOG("Invalid init packet size: %u", length);
    setError(FBO_ERROR_INIT_PACKET_INVALID);
    return;
  }
  
  fbo_init_packet_t* init = (fbo_init_packet_t*)data;
  _expectedSize = init->firmwareSize;
  _expectedCRC = init->firmwareCRC;
  
  FBO_LOG("Init: size=%u, crc=0x%08lX", _expectedSize, (unsigned long)_expectedCRC);
  
  if (_expectedSize == 0) {
    setError(FBO_ERROR_INIT_PACKET_INVALID);
    return;
  }
  
  if (_expectedSize > OTAStorageBackend.maxSize()) {
    FBO_LOG("Size too large: %u > %u", _expectedSize, OTAStorageBackend.maxSize());
    setError(FBO_ERROR_SIZE_TOO_LARGE);
    return;
  }
  
  ota_storage_result_t result = OTAStorageBackend.begin(_expectedSize);
  if (result != OTA_STORAGE_OK) {
    FBO_LOG("Storage begin failed: %d", result);
    setError(FBO_ERROR_STORAGE_BEGIN_FAILED);
    return;
  }
  
  _receivedSize = 0;
  _calculatedCRC = (uint32_t)crc_init();
  _lastNotifiedPercent = 0;
  _chunkCount = 0;
  _state = FBO_STATE_RECEIVING;
  
  sendProgressNotification();
  
  if (_callbacks) {
    _callbacks->onStart(_expectedSize, _expectedCRC);
  }
}

void FastBLEOTAClass::processDataChunk(const uint8_t* data, size_t length) {
  #if FBO_ENABLE_CRC
  updateCRC(data, length);
  #endif
  
  size_t written = OTAStorageBackend.write(data, length);
  if (written != length) {
    FBO_LOG("Write failed: wrote %u of %u", written, length);
    setError(FBO_ERROR_WRITE_FAILED);
    return;
  }
  
  _receivedSize += written;
  _chunkCount++;
  
  // Send progress notification every percent change
  uint8_t currentPercent = (uint8_t)((_receivedSize * 100) / _expectedSize);
  if (currentPercent != _lastNotifiedPercent || _receivedSize >= _expectedSize) {
    _lastNotifiedPercent = currentPercent;
    sendProgressNotification();
    
    if (_callbacks) {
      _callbacks->onProgress(_receivedSize, _expectedSize, getProgress());
    }
  }
  
  #if FBO_ENABLE_FLOW_CONTROL && FBO_ACK_INTERVAL > 0
  if (_chunkCount % FBO_ACK_INTERVAL == 0) {
    sendAck();
  }
  #endif
  
  if (_receivedSize >= _expectedSize) {
    finalizeUpdate();
  } else if (_receivedSize > _expectedSize) {
    setError(FBO_ERROR_SIZE_MISMATCH);
  }
}

void FastBLEOTAClass::finalizeUpdate() {
  _state = FBO_STATE_VALIDATING;
  sendProgressNotification();
  
  FBO_LOG("Validating: %u bytes received, %lu chunks", _receivedSize, (unsigned long)_chunkCount);
  
  #if FBO_ENABLE_CRC
  uint32_t finalCRC = (uint32_t)crc_finalize((crc_t)_calculatedCRC);
  _calculatedCRC = finalCRC;
  FBO_LOG("CRC: calculated=0x%08lX, expected=0x%08lX", (unsigned long)finalCRC, (unsigned long)_expectedCRC);
  
  if (_expectedCRC != 0 && finalCRC != _expectedCRC) {
    FBO_LOG("CRC mismatch!");
    setError(FBO_ERROR_CRC_MISMATCH);
    return;
  }
  #endif
  
  ota_storage_result_t result = OTAStorageBackend.end();
  if (result != OTA_STORAGE_OK) {
    FBO_LOG("Storage end failed: %d", result);
    setError(FBO_ERROR_FINALIZE_FAILED);
    return;
  }
  
  FBO_LOG("Update complete, applying...");
  
  _state = FBO_STATE_APPLYING;
  sendProgressNotification();
  
  if (_callbacks) {
    _callbacks->onComplete();
  }
  
  delay(100);
  OTAStorageBackend.apply();
}

void FastBLEOTAClass::processControlCommand(uint8_t command) {
  switch (command) {
    case FBO_CMD_ABORT:
      FBO_LOG("Abort command received");
      if (_callbacks) _callbacks->onAbort();
      reset();
      break;
    case FBO_CMD_RESET:
      FBO_LOG("Reset command received");
      reset();
      break;
    case FBO_CMD_APPLY:
      FBO_LOG("Apply command received");
      if (_state == FBO_STATE_IDLE && OTAStorageBackend.bytesWritten() > 0) {
        finalizeUpdate();
      }
      break;
    case FBO_CMD_GET_STATUS:
      sendProgressNotification();
      break;
    default:
      FBO_LOG("Unknown command: 0x%02X", command);
      break;
  }
}

void FastBLEOTAClass::sendProgressNotification() {
  if (_pProgressCharacteristic == nullptr) return;
  
  fbo_progress_t progress;
  progress.state = (uint8_t)_state;
  progress.error = (uint8_t)_lastError;
  progress.percent = (uint8_t)((_expectedSize > 0) ? ((_receivedSize * 100) / _expectedSize) : 0);
  progress.bytesReceived = _receivedSize;
  progress.bytesExpected = _expectedSize;
  progress.crcCalculated = (uint32_t)crc_finalize((crc_t)_calculatedCRC);
  
  _pProgressCharacteristic->setValue((uint8_t*)&progress, sizeof(progress));
  _pProgressCharacteristic->notify();
}

void FastBLEOTAClass::sendAck() {
  if (_pControlCharacteristic == nullptr) return;
  uint8_t ack = 0x01;
  _pControlCharacteristic->setValue(&ack, 1);
  _pControlCharacteristic->notify();
}

void FastBLEOTAClass::setError(fbo_error_t error) {
  _lastError = error;
  _state = FBO_STATE_ERROR;
  OTAStorageBackend.abort();
  FBO_LOG("Error: %s", errorToString(error));
  sendProgressNotification();
  if (_callbacks) {
    _callbacks->onError(error, errorToString(error));
  }
}

const char* FastBLEOTAClass::errorToString(fbo_error_t error) {
  switch (error) {
    case FBO_ERROR_NONE: return "No error";
    case FBO_ERROR_INIT_PACKET_INVALID: return "Invalid init packet";
    case FBO_ERROR_SIZE_TOO_LARGE: return "Firmware too large";
    case FBO_ERROR_STORAGE_BEGIN_FAILED: return "Storage begin failed";
    case FBO_ERROR_WRITE_FAILED: return "Write failed";
    case FBO_ERROR_CRC_MISMATCH: return "CRC mismatch";
    case FBO_ERROR_SIZE_MISMATCH: return "Size mismatch";
    case FBO_ERROR_FINALIZE_FAILED: return "Finalize failed";
    case FBO_ERROR_TIMEOUT: return "Timeout";
    case FBO_ERROR_ABORTED: return "Aborted";
    case FBO_ERROR_NOT_SUPPORTED: return "Not supported";
    default: return "Unknown error";
  }
}

void FastBLEOTAClass::updateCRC(const uint8_t* data, size_t length) {
  _calculatedCRC = (uint32_t)crc_update((crc_t)_calculatedCRC, data, length);
}

FastBLEOTAClass FastBLEOTA;
