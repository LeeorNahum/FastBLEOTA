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

#### **Features to be implemented**

The following features are planned to be implemented:

*Capability Exchange:* The device can inform the client about supported features (e.g., maximum chunk size).

*Sending Firmware Size and Metadata:*

* *Firmware Size Characteristic:* Client writes the total firmware size to the device.
* *Metadata Characteristic (Optional):* Include version information, checksum/hash values.

*Acknowledgment:* Device acknowledges receipt and readiness to receive data.

*Checksum/Hash Verification:* Device calculates a checksum/hash of received data.
Compares it to the value provided by the client.

*Progress Notifications:* Device can send notifications about programming progress.

*Final Status Update:* Device sends a final status update indicating successful update.

*Reset Command:* Client can send a Reset command to the device to make it reset after completion.
