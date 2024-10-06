# FastBLEOTA

FastBLEOTA is a library for ESP32 that implements a simple Over-The-Air (OTA) update mechanism using BLE. It is designed to be fast and efficient, and can be used to update the firmware of devices over BLE. The library is based on the NimBLE-Arduino library.

## Installation

### Arduino IDE

1. Download the library as a ZIP file.
2. Open the Arduino IDE.
3. Go to `Sketch` > `Include Library` > `Add .ZIP Library`.
4. Select the downloaded ZIP file.

### PlatformIO

Add the following to your `platformio.ini`:

```ini
lib_deps =
  https://github.com/LeeorNahum/FastBLEOTA.git
```
