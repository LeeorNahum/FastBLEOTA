/**
 * @file OTAStorageESP32.cpp
 * @brief ESP32 OTA storage backend implementation
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#include "OTAStorageESP32.h"

#ifdef FBO_PLATFORM_ESP32

OTAStorageESP32::OTAStorageESP32() 
  : _bytesWritten(0), _expectedSize(0), _active(false) {
}

ota_storage_result_t OTAStorageESP32::begin(size_t size) {
  _bytesWritten = 0;
  _expectedSize = size;
  
  if (!Update.begin(size)) {
    Update.printError(Serial);
    return OTA_STORAGE_ERROR_INIT;
  }
  
  _active = true;
  return OTA_STORAGE_OK;
}

size_t OTAStorageESP32::write(const uint8_t* data, size_t length) {
  if (!_active) {
    return 0;
  }
  
  size_t written = Update.write((uint8_t*)data, length);
  if (written > 0) {
    _bytesWritten += written;
  }
  return written;
}

ota_storage_result_t OTAStorageESP32::end() {
  if (!_active) {
    return OTA_STORAGE_ERROR_FINALIZE;
  }
  
  _active = false;
  
  if (!Update.end(true)) {
    Update.printError(Serial);
    return OTA_STORAGE_ERROR_FINALIZE;
  }
  
  return OTA_STORAGE_OK;
}

void OTAStorageESP32::abort() {
  if (_active) {
    Update.abort();
    _active = false;
  }
  _bytesWritten = 0;
  _expectedSize = 0;
}

void OTAStorageESP32::apply() {
  ESP.restart();
  // Does not return
}

size_t OTAStorageESP32::maxSize() {
  // Get the largest OTA partition size
  // Update.h handles this internally
  return ESP.getFlashChipSize() / 2;
}

size_t OTAStorageESP32::bytesWritten() {
  return _bytesWritten;
}

bool OTAStorageESP32::isActive() {
  return _active;
}

const char* OTAStorageESP32::platformName() {
  return "ESP32";
}

// Global instance
OTAStorageESP32 OTAStorageBackend;

#endif // FBO_PLATFORM_ESP32
