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
