/**
 * @file OTAStorage.h
 * @brief Abstract storage interface for cross-platform OTA updates
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 * 
 * This abstraction layer allows FastBLEOTA to work across multiple platforms
 * by delegating flash operations to platform-specific backends.
 */

#ifndef OTA_STORAGE_H
#define OTA_STORAGE_H

#include <Arduino.h>

/**
 * @brief OTA update result codes
 */
typedef enum {
  OTA_STORAGE_OK = 0,
  OTA_STORAGE_ERROR_INIT,
  OTA_STORAGE_ERROR_WRITE,
  OTA_STORAGE_ERROR_SIZE,
  OTA_STORAGE_ERROR_FINALIZE,
  OTA_STORAGE_ERROR_NOT_SUPPORTED
} ota_storage_result_t;

/**
 * @brief Abstract base class for OTA storage backends
 * 
 * Implement this interface for each platform:
 * - ESP32: Uses Update.h
 * - nRF52: Uses NRF_NVMC direct flash access or ArduinoOTA InternalStorage
 * - Other: Can use ArduinoOTA InternalStorage
 */
class OTAStorage {
public:
  virtual ~OTAStorage() {}
  
  /**
   * @brief Begin OTA update
   * @param size Total firmware size in bytes
   * @return OTA_STORAGE_OK on success, error code otherwise
   */
  virtual ota_storage_result_t begin(size_t size) = 0;
  
  /**
   * @brief Write a chunk of firmware data
   * @param data Pointer to data buffer
   * @param length Number of bytes to write
   * @return Number of bytes written, 0 on error
   */
  virtual size_t write(const uint8_t* data, size_t length) = 0;
  
  /**
   * @brief Finalize the OTA update
   * @return OTA_STORAGE_OK on success, error code otherwise
   */
  virtual ota_storage_result_t end() = 0;
  
  /**
   * @brief Abort the OTA update
   */
  virtual void abort() = 0;
  
  /**
   * @brief Apply the update and restart
   * This function should not return - device will reset
   */
  virtual void apply() = 0;
  
  /**
   * @brief Get maximum firmware size supported
   * @return Maximum size in bytes
   */
  virtual size_t maxSize() = 0;
  
  /**
   * @brief Get total bytes written so far
   * @return Bytes written
   */
  virtual size_t bytesWritten() = 0;
  
  /**
   * @brief Check if an update is in progress
   * @return true if update is active
   */
  virtual bool isActive() = 0;
  
  /**
   * @brief Get platform name for debugging
   * @return Platform identifier string
   */
  virtual const char* platformName() = 0;
};

// -----------------------------------------------------------------------------
// Platform Detection Macros
// -----------------------------------------------------------------------------

// ESP32 family detection
#if defined(ESP32) || defined(ESP_PLATFORM)
  #define FBO_PLATFORM_ESP32 1
#endif

// nRF52 family detection
#if defined(ARDUINO_ARCH_NRF5) || defined(NRF52_SERIES) || defined(ARDUINO_NRF52_ADAFRUIT)
  #define FBO_PLATFORM_NRF52 1
#endif

// SAMD detection
#if defined(ARDUINO_ARCH_SAMD)
  #define FBO_PLATFORM_SAMD 1
#endif

// RP2040 detection
#if defined(ARDUINO_ARCH_RP2040)
  #define FBO_PLATFORM_RP2040 1
#endif

// STM32 detection
#if defined(ARDUINO_ARCH_STM32)
  #define FBO_PLATFORM_STM32 1
#endif

// Combined: Platforms that can use ArduinoOTA InternalStorage
#if defined(FBO_PLATFORM_NRF52) || defined(FBO_PLATFORM_SAMD) || \
    defined(FBO_PLATFORM_RP2040) || defined(FBO_PLATFORM_STM32)
  #define FBO_USE_INTERNAL_STORAGE 1
#endif

// Check if any platform is supported
#if !defined(FBO_PLATFORM_ESP32) && !defined(FBO_USE_INTERNAL_STORAGE)
  #warning "FastBLEOTA: Unknown platform - OTA may not work"
#endif

#endif // OTA_STORAGE_H
