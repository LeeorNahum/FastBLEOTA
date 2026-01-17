# AGENTS.md - FastBLEOTA

A simple, fast BLE OTA library for ESP32 and nRF52. Values simplicity, speed (both transfer and integration), and cross-platform support.

## Quick Reference

```bash
# Build basic example
pio run -e esp32dev

# Build modular example  
pio run -e esp32_modular

# Build for nRF52
pio run -e nrf52840_dk
```

## Core Values

1. **Simplicity**: Single `begin()` call. Minimal configuration. ~400 lines total.
2. **Speed**: Fast transfers (ACK every 20 chunks), fast integration (5 minutes).
3. **Cross-Platform**: Same API for ESP32 and nRF52.
4. **No Bloat**: Don't add features unless clearly needed.

## Code Conventions

### Naming

- Use `CHARACTERISTIC` not `CHAR` (avoid confusion with `char` type)
- Prefix all public macros with `FBO_`
- Use `snake_case` for files, `PascalCase` for classes, `camelCase` for methods
- UUIDs defined as `const NimBLEUUID` objects

### Build Flags

- Use `true`/`false` for boolean options: `-D FBO_ENABLE_CRC=true`
- Use numbers for numeric options: `-D FBO_ACK_INTERVAL=20`
- Keep defaults sensible so most users need zero configuration

### Structure

```text
src/
├── FastBLEOTA.h/cpp      # Main API and BLE logic
├── OTAStorage.h          # Abstract storage interface
├── OTAStorageESP32.*     # ESP32 implementation (Update.h)
└── OTAStorageNRF52.*     # nRF52 implementation (NRF_NVMC)

examples/
├── basic/                # Minimal working example
└── modular/              # Shows wrapper pattern (zero FBO code in main)
```

## Protocol

### UUIDs

```text
Service:  a4517317-df10-4aed-bcbd-442977fe3fe5
Data:     d026496c-0b77-43fb-bd68-fce361a1be1c
Control:  98f56d4d-0a27-487b-a01b-03ed15daedc7
Progress: 094b7399-a3a0-41f3-bf8b-5d5f3170ceb0
```

### Init Packet (9 bytes)

```text
[4] firmwareSize (uint32 LE)
[4] firmwareCRC (uint32 LE, IEEE 802.3)
[1] flags (reserved)
```

### Progress Notification (15 bytes)

```text
[1] state
[1] error  
[1] percent
[4] bytesReceived
[4] bytesExpected
[4] crcCalculated
```

### Control Commands

| Value | Command | Description |
| ----- | ------- | ----------- |
| 0x00 | ABORT | Abort current update |
| 0x01 | RESET | Reset state machine |
| 0x02 | APPLY | Apply update (manual mode) |
| 0x03 | GET_STATUS | Request progress notification |

### CRC32

IEEE 802.3 polynomial (0xEDB88320). Same as zlib.
Test: `CRC32("123456789") = 0xCBF43926`

Cross-platform implementations:

- Arduino (all boards): `Arduino_CRC32` (`crc.h` / `crc_update`)
- Python: `zlib.crc32()`
- JavaScript: `crc-32` npm package
- Swift: `crc32()` from zlib

## What NOT to Do

1. **Don't change UUIDs** - Breaks all existing clients
2. **Don't reorder struct fields** - Breaks packet parsing
3. **Don't change CRC algorithm** - Breaks validation
4. **Don't add complexity without clear need**

## What IS OK

1. **Append fields to structs** - Code checks minimum size, not exact
2. **Add new characteristics** - Additive, doesn't break existing
3. **Add new error codes** - Just don't reuse values
4. **Add new control commands** - Just don't change existing
5. **Improve performance** - Same protocol, faster implementation

## Platform Notes

### ESP32

- Requires OTA partition table: `board_build.partitions = min_spiffs.csv`
- Uses `Update.h` library

### nRF52

- No partition config needed (runtime calculation)
- Direct `NRF_NVMC` register access
- CRC32 uses `Arduino_CRC32` in Arduino builds
- `copyFlashAndReset()` runs from RAM

## Callback Interface

All callbacks optional. Keep them fast (BLE task context).

```cpp
class FastBLEOTACallbacks {
  void onStart(size_t size, uint32_t crc);
  void onProgress(size_t rx, size_t total, float pct);
  void onComplete();  // Device restarts after this
  void onError(fbo_error_t err, const char* msg);
  void onAbort();
};
```

## Python Uploader (BLE_OTA.py)

- GUI mode: `python BLE_OTA.py`
- CLI mode: `python BLE_OTA.py -a ADDRESS -f FILE`
- Scan: `python BLE_OTA.py --scan`

Structure: `OTAProtocol` handles BLE, `CLIUploader` handles CLI, GUI is optional tkinter.

## Roadmap

### v3.0.0 (Current)

- CRC32 validation
- Flow control with ACK
- Progress notifications every 1%
- ESP32 + nRF52 support
- Callback interface
- Python uploader (GUI + CLI)

### v3.1.0 (Next)

- Resume support (offset tracking)
- Improved error recovery

### v3.2.0+ (Future)

- Optional compression
- Capability characteristic (if needed)

## Testing

Before committing:

1. Build compiles: `pio run -e esp32dev`
2. If changing protocol, test with actual device
3. CRC test vector passes: `0xCBF43926`

## Key Files

| File | Purpose |
| ---- | ------- |
| `README.md` | User documentation |
| `BLE_OTA.py` | Python uploader |
| `src/FastBLEOTA.h` | Main header, types, callbacks |
| `src/OTAStorage.h` | Storage abstraction interface |
