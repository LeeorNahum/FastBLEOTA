/**
 * @file OTAStorageNRF52.h
 * @brief nRF52 OTA storage backend using direct NRF_NVMC flash access
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 * 
 * This implementation writes firmware to the upper half of flash, then
 * copies it to the application area and resets. Based on ArduinoOTA's
 * InternalStorage approach by Juraj Andrassy.
 * 
 * Memory layout (nRF52840 with 1MB flash):
 *   0x00000000 - 0x00001000: MBR
 *   0x00026000 - 0x0006A000: Application
 *   0x0006A000 - 0x000AE000: OTA staging area
 *   0x000AE000 - end: Bootloader
 */

#ifndef OTA_STORAGE_NRF52_H
#define OTA_STORAGE_NRF52_H

#include "OTAStorage.h"

#ifdef FBO_PLATFORM_NRF52

class OTAStorageNRF52 : public OTAStorage {
public:
  OTAStorageNRF52();
  
  ota_storage_result_t begin(size_t size) override;
  size_t write(const uint8_t* data, size_t length) override;
  ota_storage_result_t end() override;
  void abort() override;
  void apply() override;
  size_t maxSize() override;
  size_t bytesWritten() override;
  bool isActive() override;
  const char* platformName() override;
  
  // Debug helper
  void debugPrint();
  
private:
  // Flash memory addresses calculated at runtime
  uint32_t _sketchStartAddress;
  uint32_t _storageStartAddress;
  uint32_t _maxPartitionedSize;
  uint32_t _pageSize;
  
  // Write state
  uint32_t* _writeAddress;
  size_t _bytesWritten;
  size_t _expectedSize;
  size_t _pageAlignedLength;
  bool _active;
  
  // Buffer for 4-byte aligned writes
  union {
    uint32_t u32;
    uint8_t u8[4];
  } _writeBuffer;
  uint8_t _writeIndex;
  
  // Flash operations (placed in RAM for safety)
  void waitForReady();
  void eraseFlashPage(uint32_t address);
  void writeWord(uint32_t* address, uint32_t data);
  void copyFlashAndReset(uint32_t dest, uint32_t src, uint32_t length);
};

// Global instance
extern OTAStorageNRF52 OTAStorageBackend;

#endif // FBO_PLATFORM_NRF52

#endif // OTA_STORAGE_NRF52_H
