/**
 * @file ble_demo_service.h
 * @brief Demo service showing simple notify characteristic
 * 
 * This service demonstrates a typical BLE characteristic pattern.
 * It sends a text message periodically, which changes based on
 * the firmware build time - useful for verifying OTA worked.
 * 
 * Service UUID:  bbd7772e-6ec8-4181-b94e-0e05f5cf3cbf
 * Characteristic: 26345f5e-97db-42c2-8abd-70bb4917ab88
 */

#ifndef BLE_DEMO_SERVICE_H
#define BLE_DEMO_SERVICE_H

#include <NimBLEDevice.h>

// UUIDs (provided by user)
#define DEMO_SERVICE_UUID                    "bbd7772e-6ec8-4181-b94e-0e05f5cf3cbf"
#define DEMO_MESSAGE_CHARACTERISTIC_UUID     "26345f5e-97db-42c2-8abd-70bb4917ab88"

/**
 * @brief Initialize demo service
 * Must be called after BLE server is created.
 */
void bleStartDemoService();

/**
 * @brief Check if client is subscribed to demo notifications
 * @return true if subscribed
 */
bool bleDemoServiceSubscribed();

/**
 * @brief Send demo message
 * @param message Text message to send
 * @param notify If true, send notification
 */
void bleSendDemoMessage(const char* message, bool notify);

#endif // BLE_DEMO_SERVICE_H
