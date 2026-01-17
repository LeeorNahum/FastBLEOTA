/**
 * @file ble.h
 * @brief BLE management module (Modular style pattern)
 */

#ifndef BLE_H
#define BLE_H

#include <NimBLEDevice.h>
#include "ble_ota/ble_ota.h"
#include "ble_demo_service/ble_demo_service.h"

void bleStart(const char* deviceName);
bool bleIsDeviceConnected();
NimBLEServer* bleGetServer();

#endif
