# FastBLEOTA

Fast and simple BLE Over-The-Air firmware updates for ESP32 and nRF52.

[![Version](https://img.shields.io/badge/version-3.0.0-blue.svg)](https://github.com/LeeorNahum/FastBLEOTA)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20nRF52-green.svg)](https://github.com/LeeorNahum/FastBLEOTA)

## Features

- **NimBLE-Powered**: Built on the [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) BLE stack, offering ~50% less flash and ~100KB less RAM compared to Bluedroid
- **Cross-Platform**: Works on ESP32 (all variants) and nRF52 boards
- **CRC32 Validation**: IEEE 802.3 polynomial ensures data integrity during transfer
- **Progress Notifications**: Real-time progress via BLE characteristic notifications
- **Flow Control**: ACK-based mechanism prevents buffer overrun on slower devices
- **Event Callbacks**: Optional callback interface lets you react to OTA events (start, progress, complete, error, abort) without modifying the library
- **Simple Integration**: Add OTA to your existing NimBLE project with a single `begin()` call
- **Python Uploader**: GUI and CLI tool included

## Quick Start

### Installation

**PlatformIO:**

```ini
lib_deps =
  h2zero/NimBLE-Arduino@^2.0.0
  https://github.com/LeeorNahum/FastBLEOTA.git#main
```

**Arduino IDE:**

1. Download the library as ZIP
2. Sketch → Include Library → Add .ZIP Library

### ESP32 Partition Table

ESP32 requires an OTA-capable partition table with two app slots. The default Arduino partition table does **not** include OTA support. You must specify one:

**PlatformIO (built-in partition tables with OTA support):**

```ini
board_build.partitions = min_spiffs.csv   ; 4MB flash, ~1.8MB per app
board_build.partitions = default_8MB.csv  ; 8MB flash, ~3MB per app  
board_build.partitions = default_16MB.csv ; 16MB flash
```

Or create a custom `partitions.csv` in your project root for full control.

**Custom partition table example (`partitions_ota.csv`):**

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x1E0000,
app1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,   data, spiffs,  0x3D0000,0x20000,
```

The key requirements are:

- `otadata` partition (tracks which app slot is active)
- Two `app` partitions (`ota_0` and `ota_1`) of equal size

**nRF52 does NOT require partition configuration** - the library automatically divides available flash between the application and OTA staging area.

### Minimal Example

```cpp
#include <NimBLEDevice.h>
#include <FastBLEOTA.h>

void setup() {
  Serial.begin(115200);
  
  // Initialize BLE
  NimBLEDevice::init("MyDevice");
  NimBLEServer* server = NimBLEDevice::createServer();
  
  // Initialize FastBLEOTA
  FastBLEOTA.begin(server);
  
  // Start advertising
  NimBLEDevice::getAdvertising()->start();
  
  Serial.println("Ready for OTA updates!");
}

void loop() {
  delay(100);
}
```

### Upload Firmware

**GUI Mode:**

```bash
python BLE_OTA.py
```

**CLI Mode:**

```bash
python BLE_OTA.py -a AA:BB:CC:DD:EE:FF -f firmware.bin
```

## How It Works

### CRC32 Validation

CRC32 (Cyclic Redundancy Check) is a checksum algorithm that detects accidental changes to data during transmission. Before uploading, the Python script calculates a 32-bit CRC of the entire firmware binary using the standard IEEE 802.3 polynomial (same as Ethernet, zlib, PNG). This CRC is sent to the device in the init packet.

As the device receives firmware chunks, it calculates its own running CRC using the Arduino_CRC32 library's streaming API (`crc_init`, `crc_update`, `crc_finalize`). After all data is received, it compares its calculated CRC against the expected value. If they don't match, the update is rejected and an error is reported.

**Cross-language compatibility:** The CRC32 algorithm is standardized. Python's `zlib.crc32()`, JavaScript's npm `crc-32` package, Swift's `CRC32`, and Arduino_CRC32's `crc_*` functions all produce identical results for the same input.

### Flow Control

Flow control prevents the sender from overwhelming the receiver's buffers. FastBLEOTA uses an ACK (acknowledgment) mechanism:

1. After every N chunks (configurable via `FBO_ACK_INTERVAL`), the device sends an ACK byte via the Control characteristic
2. The Python uploader waits for this ACK before continuing
3. This creates natural "breathing room" for the device to process data

**Why it matters:** Without flow control, a fast BLE central can send data faster than the device can write to flash, causing buffer overflows and failed updates.

### Progress Notifications

The device sends progress updates via the Progress characteristic as a 15-byte packed struct:

| Field | Size | Description |
| ----- | ---- | ----------- |
| state | 1 byte | Current state (idle, receiving, validating, etc.) |
| error | 1 byte | Error code if in error state |
| percent | 1 byte | Progress 0-100 |
| bytesReceived | 4 bytes | Total bytes received |
| bytesExpected | 4 bytes | Total bytes expected |
| crcCalculated | 4 bytes | Running CRC (for debugging) |

The Python uploader subscribes to these notifications to show accurate progress.

## BLE Service

### Service UUID

```text
a4517317-df10-4aed-bcbd-442977fe3fe5
```

### Characteristics

| UUID | Name | Properties | Description |
| ---- | ---- | ---------- | ----------- |
| `d026496c-0b77-43fb-bd68-fce361a1be1c` | Data | Read, Write, Write Without Response | Receives init packet and firmware chunks |
| `98f56d4d-0a27-487b-a01b-03ed15daedc7` | Control | Read, Write, Notify | Commands (abort, reset, apply) and ACK notifications |
| `094b7399-a3a0-41f3-bf8b-5d5f3170ceb0` | Progress | Read, Notify | Status and progress notifications |

### Control Commands

| Command | Value | Description |
| ------- | ----- | ----------- |
| ABORT | 0x00 | Abort current update |
| RESET | 0x01 | Reset state (ready for new update) |
| APPLY | 0x02 | Apply and restart (after validation) |
| GET_STATUS | 0x03 | Request progress notification |

## Configuration

Compile-time options in `platformio.ini`:

```ini
build_flags =
  -D FBO_ENABLE_CRC=true           # Enable CRC32 validation (default: true)
  -D FBO_ENABLE_FLOW_CONTROL=true  # Enable ACK flow control (default: true)
  -D FBO_ACK_INTERVAL=20           # ACK every N chunks (default: 20)
  -D FBO_DEBUG                     # Enable debug logging
```

## Callbacks

FastBLEOTA provides an optional callback interface that lets you react to OTA events without modifying the library. All callbacks are optional - implement only what you need.

```cpp
class MyOTACallbacks : public FastBLEOTACallbacks {
public:
  void onStart(size_t size, uint32_t crc) override {
    Serial.printf("OTA started: %u bytes, CRC=0x%08X\n", size, crc);
  }
  
  void onProgress(size_t rx, size_t total, float pct) override {
    Serial.printf("Progress: %.1f%%\n", pct);
  }
  
  void onComplete() override {
    Serial.println("Update complete, restarting...");
  }
  
  void onError(fbo_error_t err, const char* msg) override {
    Serial.printf("Error: %s\n", msg);
  }
  
  void onAbort() override {
    Serial.println("Update aborted");
  }
};

// In setup():
FastBLEOTA.setCallbacks(new MyOTACallbacks());
```

### Callback Reference

| Callback | When Called | Parameters |
| -------- | ----------- | ---------- |
| `onStart` | When a valid init packet is received and OTA begins | `size`: firmware size in bytes, `crc`: expected CRC32 |
| `onProgress` | Periodically during data transfer | `bytesReceived`, `bytesExpected`, `percent` (0.0-100.0) |
| `onComplete` | When transfer completes and CRC validates. Device will restart after this returns. | None |
| `onError` | When an error occurs (CRC mismatch, write failure, etc.) | `error`: error code, `errorString`: human-readable message |
| `onAbort` | When update is aborted via control command or reset | None |

**Important:** Callbacks are invoked from the BLE task context. Keep them fast to avoid blocking BLE operations.

## Platform Support

| Platform | Storage Backend | Notes |
| -------- | ---------------- | ----- |
| ESP32 | `Update.h` | All variants (ESP32, S2, S3, C3, C6). Requires OTA partition table. |
| nRF52840 | `NRF_NVMC` | Direct flash access. 1MB flash, ~400KB usable for OTA. |
| nRF52832 | `NRF_NVMC` | 512KB flash, ~200KB usable for OTA. |

## Requirements

- **NimBLE-Arduino** >= 2.0.0
- **Python 3.7+** with `bleak` library (for uploader)
- OTA-capable partition table (ESP32 only)

## Architecture

```text
FastBLEOTA/
├── src/
│   ├── FastBLEOTA.h              # Main API
│   ├── FastBLEOTA.cpp            # BLE logic, CRC, flow control
│   ├── OTAStorage.h              # Abstract storage interface
│   ├── OTAStorageESP32.h/cpp     # ESP32 backend (Update.h)
│   └── OTAStorageNRF52.h/cpp     # nRF52 backend (NRF_NVMC)
├── examples/
│   ├── basic/                    # Minimal example
│   └── modular/                  # Modular architecture example
├── BLE_OTA.py                    # Python uploader (GUI + CLI)
└── platformio.ini                # Build/test configuration
```

## Restart Behavior

After a successful OTA update, the device automatically restarts to apply the new firmware:

- **ESP32**: Uses `ESP.restart()` which is the standard Arduino restart function
- **nRF52**: Uses `NVIC_SystemReset()` after copying firmware from staging to application area

The `onComplete()` callback is called immediately before the restart, giving you a chance to save state or notify the user.

## Error Codes

| Code | Name | Description |
| ---- | ---- | ----------- |
| 0 | FBO_ERROR_NONE | No error |
| 1 | FBO_ERROR_INIT_PACKET_INVALID | Init packet wrong size or format |
| 2 | FBO_ERROR_SIZE_TOO_LARGE | Firmware exceeds available space |
| 3 | FBO_ERROR_STORAGE_BEGIN_FAILED | Failed to begin OTA storage |
| 4 | FBO_ERROR_WRITE_FAILED | Failed to write chunk to flash |
| 5 | FBO_ERROR_CRC_MISMATCH | Calculated CRC doesn't match expected |
| 6 | FBO_ERROR_SIZE_MISMATCH | Received bytes != expected size |
| 7 | FBO_ERROR_FINALIZE_FAILED | Failed to finalize update |
| 8 | FBO_ERROR_TIMEOUT | Transfer timeout |
| 9 | FBO_ERROR_ABORTED | Update aborted by user/client |
| 10 | FBO_ERROR_NOT_SUPPORTED | Platform doesn't support OTA |

## Related

- [BLE-OTA-Tester](https://github.com/LeeorNahum/BLE-OTA-Tester) - Hardware tester for FastBLEOTA using ESP32-S3 with visual OTA verification

## Credits

- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) by h2zero
- Inspired by [ArduinoBleOTA](https://github.com/vovagorodok/ArduinoBleOTA) and [ArduinoOTA](https://github.com/JAndrassy/ArduinoOTA)
