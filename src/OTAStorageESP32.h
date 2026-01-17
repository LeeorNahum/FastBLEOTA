/**
 * @file OTAStorageESP32.h
 * @brief ESP32 OTA storage backend using Update.h
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 */

#ifndef OTA_STORAGE_ESP32_H
#define OTA_STORAGE_ESP32_H

#include "OTAStorage.h"

#ifdef FBO_PLATFORM_ESP32

#include <Update.h>

class OTAStorageESP32 : public OTAStorage {
public:
  OTAStorageESP32();
  
  ota_storage_result_t begin(size_t size) override;
  size_t write(const uint8_t* data, size_t length) override;
  ota_storage_result_t end() override;
  void abort() override;
  void apply() override;
  size_t maxSize() override;
  size_t bytesWritten() override;
  bool isActive() override;
  const char* platformName() override;
  
private:
  size_t _bytesWritten;
  size_t _expectedSize;
  bool _active;
};

// Global instance
extern OTAStorageESP32 OTAStorageBackend;

#endif // FBO_PLATFORM_ESP32

#endif // OTA_STORAGE_ESP32_H
