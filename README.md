# FastBLEOTA

FastBLEOTA is a library for ESP32 that implements a simple Over-The-Air (OTA) update mechanism using BLE. It is designed to be fast and efficient, and can be used to update the firmware of devices over BLE. The library is based on the NimBLE-Arduino library.

## Version 2.0.0 - Breaking Changes

**This version requires NimBLE-Arduino 2.x or esp-nimble-cpp 2.x.**

Version 2.0.0 updates the callback signatures to be compatible with NimBLE 2.x, which changed all connection-oriented callbacks to include a `NimBLEConnInfo&` parameter. If you're using NimBLE 1.x, please use FastBLEOTA 1.x.

### Migration from 1.x

No changes required in your application code - FastBLEOTA handles the callback internally. Just update your NimBLE dependency to 2.x.

### ESP-IDF Note

If using esp-nimble-cpp with PlatformIO and Arduino+ESP-IDF framework, you may need to add this build flag:
```ini
build_flags =
  -D CONFIG_NIMBLE_CPP_IDF=1
```

## Requirements

- **ESP32** (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, etc.)
- **NimBLE-Arduino >= 2.0.0** or **esp-nimble-cpp >= 2.0.0**
- OTA-capable partition table (dual app partitions)

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
  https://github.com/LeeorNahum/FastBLEOTA.git#main
  h2zero/NimBLE-Arduino@^2.0.0
```

For ESP-IDF + Arduino component:
```ini
lib_deps =
  https://github.com/h2zero/esp-nimble-cpp.git#2.3.4
  https://github.com/LeeorNahum/FastBLEOTA.git#main

build_flags =
  -D CONFIG_NIMBLE_CPP_IDF=1
```

## Usage

You can use the `BLE_OTA.py` script to upload firmware to your device over BLE. There are two ways to run the script: using the GUI or by creating a batch file with predefined address and path.

### Using the GUI

Simply run the script without any arguments to use the GUI

### Using a Batch File

Create a batch script named `BLE_OTA.bat` with the following content, replacing `<BLE_DEVICE_ADDRESS>` with your device's BLE address and `<FIRMWARE_FILE_PATH>` with the path to your firmware file:

```batch
cd /d "%~dp0"
python "BLE_OTA.py" --address <BLE_DEVICE_ADDRESS> --file "<FIRMWARE_FILE_PATH>"
pause
```

This batch file will navigate to the script's directory and execute the `BLE_OTA.py` script with the specified arguments.

Both methods will initiate the firmware upload process to your BLE device.
