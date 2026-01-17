/**
 * @file ble_ota.h
 * @brief OTA service wrapper (Modular style pattern)
 * 
 * Wraps FastBLEOTA library for clean integration.
 */

#ifndef BLE_OTA_H
#define BLE_OTA_H

#include <FastBLEOTA.h>

/**
 * @brief Initialize OTA service
 * Must be called after BLE server is created.
 */
void bleStartOTA();

/**
 * @brief Get the OTA service UUID
 * @return Service UUID reference
 */
const NimBLEUUID& bleGetOTAServiceUUID();

#endif // BLE_OTA_H
