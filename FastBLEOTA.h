#ifndef FASTBLEOTA_H
#define FASTBLEOTA_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Update.h>

typedef enum {
  FASTBLEOTA_ERROR_NONE,           //!< No error
  FASTBLEOTA_ERROR_SIZE_MISMATCH,  //!< Received size data of incorrect length
  FASTBLEOTA_ERROR_START_UPDATE,   //!< Failed to start update
  FASTBLEOTA_ERROR_WRITE_CHUNK,    //!< Failed to write firmware chunk
  FASTBLEOTA_ERROR_RECEIVED_MORE,  //!< Received more data than expected
  FASTBLEOTA_ERROR_FINALIZE_UPDATE //!< Failed to finalize update
} fastbleota_error_t;

class FastBLEOTACallbacks {
  public:
    virtual ~FastBLEOTACallbacks() {}

    virtual void onOTAStart(size_t expectedSize) {}
    virtual void onOTAProgress(size_t receivedSize, size_t expectedSize) {}
    virtual void onOTAComplete() {}
    virtual void onOTAError(fastbleota_error_t errorCode) {}
};

class FastBLEOTA {
  public:
    FastBLEOTA() = delete;

    static void begin(NimBLEServer* pServer);

    static void reset();

    static void setCallbacks(FastBLEOTACallbacks* callbacks);

    static const char* getServiceUUID();

  private:
    static void processData(const uint8_t* data, size_t length);

    static void onOTAStart(size_t expectedSize);
    static void onOTAProgress(size_t receivedSize, size_t expectedSize);
    static void onOTAComplete();
    static void onOTAError(fastbleota_error_t errorCode);

    static NimBLEService* _pService;
    static NimBLECharacteristic* _pCharacteristic;

    static size_t _expectedSize;
    static size_t _receivedSize;
    static bool _sizeReceived;

    class CharacteristicCallbacks;

    static FastBLEOTACallbacks* _callbacks;
};

#endif // FASTBLEOTA_H