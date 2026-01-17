/**
 * @file FastBLEOTA.h
 * @brief Fast BLE Over-The-Air Update Library for ESP32 and nRF52
 * 
 * FastBLEOTA
 * Copyright (c) 2024-2026 Leeor Nahum
 * 
 * Features:
 * - Cross-platform: ESP32 and nRF52 support
 * - CRC32 validation for data integrity
 * - Progress notifications via BLE
 * - Flow control with ACK mechanism
 * - Simple integration with existing NimBLE stack
 * - Parameterized API for flexibility
 */

#ifndef FASTBLEOTA_H
#define FASTBLEOTA_H

#include <Arduino.h>
#include <NimBLEDevice.h>

// Include platform-specific storage backend
#include "src/OTAStorage.h"

#ifdef FBO_PLATFORM_ESP32
  #include "src/OTAStorageESP32.h"
#elif defined(FBO_PLATFORM_NRF52)
  #include "src/OTAStorageNRF52.h"
#endif

// -----------------------------------------------------------------------------
// Version Information
// -----------------------------------------------------------------------------
#define FASTBLEOTA_VERSION_MAJOR 3
#define FASTBLEOTA_VERSION_MINOR 0
#define FASTBLEOTA_VERSION_PATCH 0
#define FASTBLEOTA_VERSION_STRING "3.0.0"

// -----------------------------------------------------------------------------
// BLE UUIDs
// -----------------------------------------------------------------------------
const NimBLEUUID FBO_SERVICE_UUID                   ("a4517317-df10-4aed-bcbd-442977fe3fe5");
const NimBLEUUID FBO_DATA_CHARACTERISTIC_UUID       ("d026496c-0b77-43fb-bd68-fce361a1be1c");
const NimBLEUUID FBO_CONTROL_CHARACTERISTIC_UUID    ("98f56d4d-0a27-487b-a01b-03ed15daedc7");
const NimBLEUUID FBO_PROGRESS_CHARACTERISTIC_UUID   ("094b7399-a3a0-41f3-bf8b-5d5f3170ceb0");

// -----------------------------------------------------------------------------
// Configuration Macros
// -----------------------------------------------------------------------------

// Enable/disable features at compile time (use true/false for clarity)
#ifndef FBO_ENABLE_CRC
  #define FBO_ENABLE_CRC true
#endif

#ifndef FBO_ENABLE_FLOW_CONTROL
  #define FBO_ENABLE_FLOW_CONTROL true
#endif

// Debug logging (define FBO_DEBUG to enable)
#ifdef FBO_DEBUG
  #define FBO_LOG(fmt, ...) Serial.printf("[FBO] " fmt "\n", ##__VA_ARGS__)
#else
  #define FBO_LOG(fmt, ...) ((void)0)
#endif

// ACK every N chunks for flow control (0 = disabled)
// Default of 20 balances throughput with reliability
#ifndef FBO_ACK_INTERVAL
  #define FBO_ACK_INTERVAL 20
#endif

// -----------------------------------------------------------------------------
// Error Codes
// -----------------------------------------------------------------------------
typedef enum {
  FBO_ERROR_NONE = 0,
  FBO_ERROR_INIT_PACKET_INVALID,      // Init packet wrong size/format
  FBO_ERROR_SIZE_TOO_LARGE,           // Firmware exceeds max size
  FBO_ERROR_STORAGE_BEGIN_FAILED,     // Failed to begin OTA storage
  FBO_ERROR_WRITE_FAILED,             // Failed to write chunk
  FBO_ERROR_CRC_MISMATCH,             // CRC32 validation failed
  FBO_ERROR_SIZE_MISMATCH,            // Received != expected size
  FBO_ERROR_FINALIZE_FAILED,          // Failed to finalize update
  FBO_ERROR_TIMEOUT,                  // Transfer timeout
  FBO_ERROR_ABORTED,                  // User/client aborted
  FBO_ERROR_NOT_SUPPORTED             // Platform doesn't support OTA
} fbo_error_t;

// -----------------------------------------------------------------------------
// OTA State
// -----------------------------------------------------------------------------
typedef enum {
  FBO_STATE_IDLE = 0,
  FBO_STATE_WAITING_INIT,             // Waiting for init packet
  FBO_STATE_RECEIVING,                // Receiving firmware chunks
  FBO_STATE_VALIDATING,               // Validating CRC
  FBO_STATE_APPLYING,                 // Applying update (will reset)
  FBO_STATE_ERROR                     // Error occurred
} fbo_state_t;

// -----------------------------------------------------------------------------
// Control Commands (written to control characteristic)
// -----------------------------------------------------------------------------
typedef enum {
  FBO_CMD_ABORT = 0x00,               // Abort current update
  FBO_CMD_RESET = 0x01,               // Reset state (ready for new update)
  FBO_CMD_APPLY = 0x02,               // Apply and restart (after validation)
  FBO_CMD_GET_STATUS = 0x03           // Request status notification
} fbo_command_t;

// -----------------------------------------------------------------------------
// Progress/Status Structure (sent via progress characteristic)
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint8_t state;                      // fbo_state_t
  uint8_t error;                      // fbo_error_t (if state == ERROR)
  uint8_t percent;                    // Progress 0-100
  uint32_t bytesReceived;             // Total bytes received
  uint32_t bytesExpected;             // Total bytes expected
  uint32_t crcCalculated;             // Running CRC (for debugging)
} fbo_progress_t;
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Init Packet Structure (first write to data characteristic)
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
typedef struct {
  uint32_t firmwareSize;              // Firmware size in bytes
  uint32_t firmwareCRC;               // Expected CRC32 of firmware
  uint8_t flags;                      // Reserved flags
  // Total: 9 bytes
} fbo_init_packet_t;
#pragma pack(pop)

#define FBO_INIT_PACKET_SIZE sizeof(fbo_init_packet_t)

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

/**
 * @brief Callback interface for OTA events
 * 
 * All callbacks are optional. Implement only what you need.
 * Callbacks are called from the BLE task context.
 */
class FastBLEOTACallbacks {
public:
  virtual ~FastBLEOTACallbacks() {}
  
  /**
   * @brief Called when OTA update begins
   * @param expectedSize Total firmware size in bytes
   * @param expectedCRC Expected CRC32 (0 if not provided)
   */
  virtual void onStart(size_t expectedSize, uint32_t expectedCRC) {}
  
  /**
   * @brief Called periodically during transfer
   * @param bytesReceived Bytes received so far
   * @param bytesExpected Total expected bytes
   * @param percent Progress percentage (0.0-100.0)
   */
  virtual void onProgress(size_t bytesReceived, size_t bytesExpected, float percent) {}
  
  /**
   * @brief Called when transfer is complete and CRC validates
   * Device will restart shortly after this callback returns.
   */
  virtual void onComplete() {}
  
  /**
   * @brief Called when an error occurs
   * @param error Error code
   * @param errorString Human-readable error description
   */
  virtual void onError(fbo_error_t error, const char* errorString) {}
  
  /**
   * @brief Called when OTA is aborted (by client or error)
   */
  virtual void onAbort() {}
};

// -----------------------------------------------------------------------------
// FastBLEOTA Class
// -----------------------------------------------------------------------------

class FastBLEOTAClass {
public:
  FastBLEOTAClass();
  
  // -------------------------------------------------------------------------
  // Initialization
  // -------------------------------------------------------------------------
  
  /**
   * @brief Initialize FastBLEOTA with default UUIDs
   * @param pServer Pointer to NimBLE server
   */
  void begin(NimBLEServer* pServer);
  
  
  /**
   * @brief Set callback handler
   * @param callbacks Pointer to callbacks instance (must remain valid)
   */
  void setCallbacks(FastBLEOTACallbacks* callbacks);
  
  // -------------------------------------------------------------------------
  // Status and Control
  // -------------------------------------------------------------------------
  
  /**
   * @brief Get current OTA state
   * @return Current state
   */
  fbo_state_t getState();
  
  /**
   * @brief Get last error code
   * @return Last error (FBO_ERROR_NONE if no error)
   */
  fbo_error_t getLastError();
  
  /**
   * @brief Get progress percentage
   * @return Progress 0.0-100.0
   */
  float getProgress();
  
  /**
   * @brief Reset OTA state (abort any in-progress update)
   */
  void reset();
  
  /**
   * @brief Check if OTA is in progress
   * @return true if receiving firmware
   */
  bool isActive();
  
  // -------------------------------------------------------------------------
  // UUID Accessors (for logging/debugging)
  // -------------------------------------------------------------------------
  
  const NimBLEUUID& getServiceUUID();
  const NimBLEUUID& getDataCharacteristicUUID();
  const NimBLEUUID& getControlCharacteristicUUID();
  const NimBLEUUID& getProgressCharacteristicUUID();
  
  // -------------------------------------------------------------------------
  // Version Info
  // -------------------------------------------------------------------------
  
  const char* getVersion();
  const char* getPlatform();
  
  // -------------------------------------------------------------------------
  // Internal (called by BLE callbacks)
  // -------------------------------------------------------------------------
  
  void processDataPacket(const uint8_t* data, size_t length);
  void processControlCommand(uint8_t command);
  void sendProgressNotification();
  
private:
  // BLE components
  NimBLEService* _pService;
  NimBLECharacteristic* _pDataCharacteristic;
  NimBLECharacteristic* _pControlCharacteristic;
  NimBLECharacteristic* _pProgressCharacteristic;
  
  // State
  fbo_state_t _state;
  fbo_error_t _lastError;
  
  // Transfer tracking
  size_t _expectedSize;
  size_t _receivedSize;
  uint32_t _expectedCRC;
  uint32_t _calculatedCRC;
  uint8_t _lastNotifiedPercent;
  uint32_t _chunkCount;
  
  
  // Callbacks
  FastBLEOTACallbacks* _callbacks;
  
  // Internal callback classes
  class DataCharacteristicCallbacks;
  class ControlCharacteristicCallbacks;
  
  // Characteristic creation
  void createDataCharacteristic();
  void createControlCharacteristic();
  void createProgressCharacteristic();
  
  // Helper functions
  void processInitPacket(const uint8_t* data, size_t length);
  void processDataChunk(const uint8_t* data, size_t length);
  void finalizeUpdate();
  void setError(fbo_error_t error);
  void updateCRC(const uint8_t* data, size_t length);
  void sendAck();
  const char* errorToString(fbo_error_t error);
};

// Global instance
extern FastBLEOTAClass FastBLEOTA;

#endif // FASTBLEOTA_H
