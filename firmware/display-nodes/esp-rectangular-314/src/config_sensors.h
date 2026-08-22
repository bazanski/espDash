#ifndef CONFIG_SENSORS_H
#define CONFIG_SENSORS_H

#include <Arduino.h>

// =========================================================================
// TPMS SENSOR CONFIGURATION
// =========================================================================
// Replace these placeholders with the actual letters/numbers printed on 
// your sensors, or the data you get when scanning their QR codes.
//
// Examples:
// - Hex string: "80EACA" or "80EACA10"
// - Decimal string: "8448123"
// - Short name: "BR_1234"
//
// The parsing engine will automatically search for matches in the BLE 
// device name, the device MAC address, and the Manufacturer Data payload.

const String SENSOR_ID_LF = "5B2110"; // Left Front Tire
const String SENSOR_ID_RF = "5BA030"; // Right Front Tire
const String SENSOR_ID_LR = "5B074B"; // Left Rear Tire
const String SENSOR_ID_RR = "5B9863"; // Right Rear Tire

#endif // CONFIG_SENSORS_H
