/**
 * @file FastBLEOTA.cpp
 * @brief Fast BLE Over-The-Air Update Library
 * 
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#include "FastBLEOTA.h"
#include <crc.h>

FastBLEOTAClass FastBLEOTA;

class FastBLEOTAClass::DataCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue attValue = pCharacteristic->getValue();
    FastBLEOTA.processDataPacket(attValue.data(), attValue.size());
  }
};

class FastBLEOTAClass::ControlCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue value = pCharacteristic->getValue();
    if (value.size() >= 1) {
      FastBLEOTA.processControlCommand(value.data()[0]);
    }
  }
  
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    if (subValue > 0) {
      FastBLEOTA.sendProgressNotification();
    }
  }
};

bool FastBLEOTAClass::startService() {
  NimBLEServer* pServer = NimBLEDevice::getServer();
  if (pServer == nullptr) return false;
  
  #ifdef FBO_DEBUG
  const uint8_t testData[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  crc_t testCRCValue = crc_init();
  testCRCValue = crc_update(testCRCValue, testData, sizeof(testData));
  uint32_t testCRC = (uint32_t)crc_finalize(testCRCValue);
  Serial.printf("[FBO] CRC self-test: 0x%08lX (expected 0xCBF43926) - %s\n",
                (unsigned long)testCRC, (testCRC == 0xCBF43926) ? "PASS" : "FAIL");
  #endif
  
  ota_service = pServer->getServiceByUUID(OTA_SERVICE_UUID);
  if (ota_service == nullptr) {
    ota_service = pServer->createService(OTA_SERVICE_UUID);
  }
  
  createDataCharacteristic();
  createControlCharacteristic();
  createProgressCharacteristic();
  
  reset();
  
  FBO_LOG("FastBLEOTA v%s initialized on %s", FASTBLEOTA_VERSION_STRING, getPlatform());
  
  return ota_service->start();
}

void FastBLEOTAClass::createDataCharacteristic() {
  if (ota_service == nullptr) return;

  if (data_characteristic == nullptr) {
    data_characteristic = ota_service->getCharacteristic(OTA_DATA_CHARACTERISTIC_UUID);
    if (data_characteristic == nullptr) {
      data_characteristic = ota_service->createCharacteristic(
        OTA_DATA_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
      );

      data_characteristic->setCallbacks(new DataCallbacks());

      NimBLEDescriptor* user_description = data_characteristic->createDescriptor(NimBLEUUID("2901"), NIMBLE_PROPERTY::READ);
      user_description->setValue("OTA Firmware Data");

      NimBLE2904* presentation_format = (NimBLE2904*)data_characteristic->createDescriptor(NimBLEUUID("2904"));
      presentation_format->setFormat(NimBLE2904::FORMAT_OPAQUE);
      presentation_format->setExponent(0x00);
      presentation_format->setUnit(0x2700);
      presentation_format->setNamespace(0x00);
      presentation_format->setDescription(0x0000);
    }
  }
}

void FastBLEOTAClass::createControlCharacteristic() {
  if (ota_service == nullptr) return;

  if (control_characteristic == nullptr) {
    control_characteristic = ota_service->getCharacteristic(OTA_CONTROL_CHARACTERISTIC_UUID);
    if (control_characteristic == nullptr) {
      control_characteristic = ota_service->createCharacteristic(
        OTA_CONTROL_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
      );

      control_characteristic->setCallbacks(new ControlCallbacks());

      NimBLEDescriptor* user_description = control_characteristic->createDescriptor(NimBLEUUID("2901"), NIMBLE_PROPERTY::READ);
      user_description->setValue("OTA Control");

      NimBLE2904* presentation_format = (NimBLE2904*)control_characteristic->createDescriptor(NimBLEUUID("2904"));
      presentation_format->setFormat(NimBLE2904::FORMAT_UINT8);
      presentation_format->setExponent(0x00);
      presentation_format->setUnit(0x2700);
      presentation_format->setNamespace(0x00);
      presentation_format->setDescription(0x0000);
    }
  }
}

void FastBLEOTAClass::createProgressCharacteristic() {
  if (ota_service == nullptr) return;

  if (progress_characteristic == nullptr) {
    progress_characteristic = ota_service->getCharacteristic(OTA_PROGRESS_CHARACTERISTIC_UUID);
    if (progress_characteristic == nullptr) {
      progress_characteristic = ota_service->createCharacteristic(
        OTA_PROGRESS_CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
      );

      NimBLEDescriptor* user_description = progress_characteristic->createDescriptor(NimBLEUUID("2901"), NIMBLE_PROPERTY::READ);
      user_description->setValue("OTA Progress");

      NimBLE2904* presentation_format = (NimBLE2904*)progress_characteristic->createDescriptor(NimBLEUUID("2904"));
      presentation_format->setFormat(NimBLE2904::FORMAT_OPAQUE);
      presentation_format->setExponent(0x00);
      presentation_format->setUnit(0x2700);
      presentation_format->setNamespace(0x00);
      presentation_format->setDescription(0x0000);

      fbo_progress_t progress = {0};
      progress.state = FBO_STATE_IDLE;
      progress_characteristic->setValue((uint8_t*)&progress, sizeof(progress));
    }
  }
}

void FastBLEOTAClass::setCallbacks(FastBLEOTACallbacks* callbacks) {
  ota_callbacks = callbacks;
}

float FastBLEOTAClass::getProgress() {
  if (expected_size == 0) return 0.0f;
  return ((float)received_size * 100.0f) / (float)expected_size;
}

void FastBLEOTAClass::reset() {
  if (state == FBO_STATE_RECEIVING || state == FBO_STATE_VALIDATING) {
    OTAStorageBackend.abort();
  }
  
  state = FBO_STATE_IDLE;
  last_error = FBO_ERROR_NONE;
  expected_size = 0;
  received_size = 0;
  expected_crc = 0;
  calculated_crc = (uint32_t)crc_init();
  last_notified_percent = 0;
  chunk_count = 0;
  
  sendProgressNotification();
}

const char* FastBLEOTAClass::getPlatform() { return OTAStorageBackend.platformName(); }

void FastBLEOTAClass::processDataPacket(const uint8_t* data, size_t length) {
  if (state == FBO_STATE_ERROR) return;
  
  if (state == FBO_STATE_IDLE) {
    processInitPacket(data, length);
  } else if (state == FBO_STATE_RECEIVING) {
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
  expected_size = init->firmwareSize;
  expected_crc = init->firmwareCRC;
  
  FBO_LOG("Init: size=%u, crc=0x%08lX", expected_size, (unsigned long)expected_crc);
  
  if (expected_size == 0) {
    setError(FBO_ERROR_INIT_PACKET_INVALID);
    return;
  }
  
  if (expected_size > OTAStorageBackend.maxSize()) {
    FBO_LOG("Size too large: %u > %u", expected_size, OTAStorageBackend.maxSize());
    setError(FBO_ERROR_SIZE_TOO_LARGE);
    return;
  }
  
  ota_storage_result_t result = OTAStorageBackend.begin(expected_size);
  if (result != OTA_STORAGE_OK) {
    FBO_LOG("Storage begin failed: %d", result);
    setError(FBO_ERROR_STORAGE_BEGIN_FAILED);
    return;
  }
  
  received_size = 0;
  calculated_crc = (uint32_t)crc_init();
  last_notified_percent = 0;
  chunk_count = 0;
  state = FBO_STATE_RECEIVING;
  
  sendProgressNotification();
  
  if (ota_callbacks) {
    ota_callbacks->onStart(expected_size, expected_crc);
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
  
  received_size += written;
  chunk_count++;
  
  // Send progress notification every percent change
  uint8_t currentPercent = (uint8_t)((received_size * 100) / expected_size);
  if (currentPercent != last_notified_percent || received_size >= expected_size) {
    last_notified_percent = currentPercent;
    sendProgressNotification();
    
    if (ota_callbacks) {
      ota_callbacks->onProgress(received_size, expected_size, getProgress());
    }
  }
  
  #if FBO_ENABLE_FLOW_CONTROL && FBO_ACK_INTERVAL > 0
  if (chunk_count % FBO_ACK_INTERVAL == 0) {
    sendAck();
  }
  #endif
  
  if (received_size >= expected_size) {
    finalizeUpdate();
  } else if (received_size > expected_size) {
    setError(FBO_ERROR_SIZE_MISMATCH);
  }
}

void FastBLEOTAClass::finalizeUpdate() {
  state = FBO_STATE_VALIDATING;
  sendProgressNotification();
  
  FBO_LOG("Validating: %u bytes received, %lu chunks", received_size, (unsigned long)chunk_count);
  
  #if FBO_ENABLE_CRC
  uint32_t finalCRC = (uint32_t)crc_finalize((crc_t)calculated_crc);
  calculated_crc = finalCRC;
  FBO_LOG("CRC: calculated=0x%08lX, expected=0x%08lX", (unsigned long)finalCRC, (unsigned long)expected_crc);
  
  if (expected_crc != 0 && finalCRC != expected_crc) {
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
  
  state = FBO_STATE_APPLYING;
  sendProgressNotification();
  
  if (ota_callbacks) {
    ota_callbacks->onComplete();
  }
  
  delay(100);
  OTAStorageBackend.apply();
}

void FastBLEOTAClass::processControlCommand(uint8_t command) {
  switch (command) {
    case FBO_CMD_ABORT:
      FBO_LOG("Abort command received");
      if (ota_callbacks) ota_callbacks->onAbort();
      reset();
      break;
    case FBO_CMD_RESET:
      FBO_LOG("Reset command received");
      reset();
      break;
    case FBO_CMD_APPLY:
      FBO_LOG("Apply command received");
      if (state == FBO_STATE_IDLE && OTAStorageBackend.bytesWritten() > 0) {
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
  if (progress_characteristic == nullptr) return;
  
  fbo_progress_t progress;
  progress.state = (uint8_t)state;
  progress.error = (uint8_t)last_error;
  progress.percent = (uint8_t)((expected_size > 0) ? ((received_size * 100) / expected_size) : 0);
  progress.bytesReceived = received_size;
  progress.bytesExpected = expected_size;
  progress.crcCalculated = (uint32_t)crc_finalize((crc_t)calculated_crc);
  
  progress_characteristic->setValue((uint8_t*)&progress, sizeof(progress));
  progress_characteristic->notify();
}

void FastBLEOTAClass::sendAck() {
  if (control_characteristic == nullptr) return;
  uint8_t ack = 0x01;
  control_characteristic->setValue(&ack, 1);
  control_characteristic->notify();
}

void FastBLEOTAClass::setError(fbo_error_t error) {
  last_error = error;
  state = FBO_STATE_ERROR;
  OTAStorageBackend.abort();
  FBO_LOG("Error: %s", errorToString(error));
  sendProgressNotification();
  if (ota_callbacks) {
    ota_callbacks->onError(error, errorToString(error));
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
  calculated_crc = (uint32_t)crc_update((crc_t)calculated_crc, data, length);
}
