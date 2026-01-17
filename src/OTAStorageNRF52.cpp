/**
 * @file OTAStorageNRF52.cpp
 * @brief nRF52 OTA storage backend implementation
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 * 
 * Based on ArduinoOTA InternalStorage by Juraj Andrassy
 * Uses direct NRF_NVMC register access for flash operations
 */

#include "OTAStorageNRF52.h"

#ifdef FBO_PLATFORM_NRF52

// nRF5 SDK headers
#include <nrf.h>

// Linker symbol for ISR vector (start of sketch)
extern "C" {
  char* __isr_vector();
}

OTAStorageNRF52::OTAStorageNRF52() {
  // Calculate flash parameters from hardware registers
  _pageSize = (size_t)NRF_FICR->CODEPAGESIZE;
  uint32_t maxFlash = _pageSize * (uint32_t)NRF_FICR->CODESIZE;
  
  // Sketch starts after bootloader/SoftDevice
  _sketchStartAddress = (uint32_t)__isr_vector;
  
  // Divide available flash in half for OTA
  _maxPartitionedSize = (maxFlash - _sketchStartAddress) / 2;
  _storageStartAddress = _sketchStartAddress + _maxPartitionedSize;
  
  // Initialize state
  _writeAddress = nullptr;
  _bytesWritten = 0;
  _expectedSize = 0;
  _pageAlignedLength = 0;
  _active = false;
  _writeIndex = 0;
  _writeBuffer.u32 = 0xFFFFFFFF;
}

void OTAStorageNRF52::debugPrint() {
  Serial.print("NRF52 OTA Storage Config:\n");
  Serial.print("  Page Size: ");
  Serial.println(_pageSize);
  Serial.print("  Sketch Start: 0x");
  Serial.println(_sketchStartAddress, HEX);
  Serial.print("  Storage Start: 0x");
  Serial.println(_storageStartAddress, HEX);
  Serial.print("  Max Partition Size: ");
  Serial.println(_maxPartitionedSize);
}

void OTAStorageNRF52::waitForReady() {
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {
    // Wait for NVMC to be ready
  }
}

void OTAStorageNRF52::eraseFlashPage(uint32_t address) {
  // Enable erase mode
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een << NVMC_CONFIG_WEN_Pos;
  waitForReady();
  
  // Erase the page
  NRF_NVMC->ERASEPAGE = address;
  waitForReady();
  
  // Return to read-only mode
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
  waitForReady();
}

void OTAStorageNRF52::writeWord(uint32_t* address, uint32_t data) {
  // Enable write mode
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
  waitForReady();
  
  // Write the word
  *address = data;
  waitForReady();
  
  // Return to read-only mode
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
  waitForReady();
}

ota_storage_result_t OTAStorageNRF52::begin(size_t size) {
  if (size > _maxPartitionedSize) {
    return OTA_STORAGE_ERROR_SIZE;
  }
  
  _bytesWritten = 0;
  _expectedSize = size;
  _writeIndex = 0;
  _writeBuffer.u32 = 0xFFFFFFFF;
  _writeAddress = (uint32_t*)_storageStartAddress;
  
  // Calculate page-aligned length
  _pageAlignedLength = ((size / _pageSize) + 1) * _pageSize;
  
  _active = true;
  return OTA_STORAGE_OK;
}

size_t OTAStorageNRF52::write(const uint8_t* data, size_t length) {
  if (!_active) {
    return 0;
  }
  
  size_t written = 0;
  
  for (size_t i = 0; i < length; i++) {
    _writeBuffer.u8[_writeIndex] = data[i];
    _writeIndex++;
    written++;
    
    // When we have 4 bytes, write them
    if (_writeIndex == 4) {
      _writeIndex = 0;
      
      // Erase page if we're at a page boundary
      if (((uint32_t)_writeAddress % _pageSize) == 0) {
        eraseFlashPage((uint32_t)_writeAddress);
      }
      
      writeWord(_writeAddress, _writeBuffer.u32);
      _writeAddress++;
      _writeBuffer.u32 = 0xFFFFFFFF;
    }
  }
  
  _bytesWritten += written;
  return written;
}

ota_storage_result_t OTAStorageNRF52::end() {
  if (!_active) {
    return OTA_STORAGE_ERROR_FINALIZE;
  }
  
  // Flush any remaining bytes (pad with 0xFF)
  while (_writeIndex != 0) {
    _writeBuffer.u8[_writeIndex] = 0xFF;
    _writeIndex++;
    
    if (_writeIndex == 4) {
      if (((uint32_t)_writeAddress % _pageSize) == 0) {
        eraseFlashPage((uint32_t)_writeAddress);
      }
      writeWord(_writeAddress, _writeBuffer.u32);
      _writeAddress++;
      _writeIndex = 0;
    }
  }
  
  // Recalculate actual page-aligned length based on what was written
  _pageAlignedLength = ((uint32_t)_writeAddress - _storageStartAddress);
  
  _active = false;
  return OTA_STORAGE_OK;
}

void OTAStorageNRF52::abort() {
  _active = false;
  _bytesWritten = 0;
  _expectedSize = 0;
  _writeAddress = nullptr;
  _writeIndex = 0;
}

// This function is placed in RAM and runs with interrupts disabled
// It erases the sketch area, copies the new firmware, and resets
__attribute__((section(".data")))
void OTAStorageNRF52::copyFlashAndReset(uint32_t dest, uint32_t src, uint32_t length) {
  volatile uint32_t* d = (volatile uint32_t*)dest;
  uint32_t* s = (uint32_t*)src;
  
  // Erase destination pages
  for (uint32_t addr = dest; addr < dest + length; addr += _pageSize) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Een << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
    NRF_NVMC->ERASEPAGE = addr;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
  }
  
  // Enable write mode
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
  
  // Copy data
  for (uint32_t i = 0; i < length; i += 4) {
    *d++ = *s++;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy);
  }
  
  // Reset
  NVIC_SystemReset();
}

void OTAStorageNRF52::apply() {
  // Disable interrupts - we're about to erase our vector table
  __disable_irq();
  
  // Copy new firmware from staging area to application area and reset
  copyFlashAndReset(_sketchStartAddress, _storageStartAddress, _pageAlignedLength);
  
  // Should never reach here
  while(1);
}

size_t OTAStorageNRF52::maxSize() {
  return _maxPartitionedSize;
}

size_t OTAStorageNRF52::bytesWritten() {
  return _bytesWritten;
}

bool OTAStorageNRF52::isActive() {
  return _active;
}

const char* OTAStorageNRF52::platformName() {
  return "nRF52";
}

// Global instance
OTAStorageNRF52 OTAStorageBackend;

#endif // FBO_PLATFORM_NRF52
