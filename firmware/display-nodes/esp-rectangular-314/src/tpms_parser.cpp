#include <NimBLEDevice.h>
#include "tpms_parser.h"
#include "config_sensors.h"

// Define the global instances
TPMSData tire_lf;
TPMSData tire_rf;
TPMSData tire_lr;
TPMSData tire_rr;

// Helper function to check if a scanned device matches our configured IDs
static bool isSensorMatch(const NimBLEAddress& address, const String& name, const String& configId) {
    if (configId.length() == 0 || configId == "LF_SENSOR_ID" || configId == "RF_SENSOR_ID" || configId == "LR_SENSOR_ID" || configId == "RR_SENSOR_ID") {
        return false;
    }
    
    // Clean up config ID (lowercase, remove colons)
    String cleanConfig = configId;
    cleanConfig.replace(":", "");
    cleanConfig.toLowerCase();
    cleanConfig.trim();
    
    // Clean up MAC address (lowercase, remove colons)
    String macStr = address.toString().c_str();
    macStr.replace(":", "");
    macStr.toLowerCase();
    
    // Clean up name (lowercase)
    String nameStr = name;
    nameStr.toLowerCase();
    
    // Check if cleanConfig matches MAC address or name
    if (macStr.indexOf(cleanConfig) != -1) {
        return true;
    }
    if (nameStr.indexOf(cleanConfig) != -1) {
        return true;
    }
    
    return false;
}

// Helper function to dynamically locate the sensor ID anchor inside payload
static int find_sensor_anchor(const uint8_t* payload, size_t length, const String& configId) {
    if (length < 14) return (int)length - 1;
    
    // Parse the 3 hex bytes from configId (e.g. "5B2110" -> 0x5B, 0x21, 0x10)
    String cleanId = configId;
    cleanId.replace(":", "");
    cleanId.trim();
    if (cleanId.length() >= 6) {
        uint8_t b0 = (uint8_t)strtol(cleanId.substring(0, 2).c_str(), NULL, 16);
        uint8_t b1 = (uint8_t)strtol(cleanId.substring(2, 4).c_str(), NULL, 16);
        uint8_t b2 = (uint8_t)strtol(cleanId.substring(4, 6).c_str(), NULL, 16);
        
        // Scan payload for exact 3-byte match (Forward: 5B 98 63 or Reverse: 63 98 5B)
        for (int i = 0; i <= (int)length - 3; i++) {
            if ((payload[i] == b0 && payload[i+1] == b1 && payload[i+2] == b2) ||
                (payload[i] == b2 && payload[i+1] == b1 && payload[i+2] == b0)) {
                return i + 2; // Anchor on the last byte of the Sensor ID
            }
        }
    }
    
    return (int)length - 1;
}

// Advertised Device Callbacks
class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        NimBLEAddress address = advertisedDevice->getAddress();
        String name = advertisedDevice->getName().c_str();
        
        TPMSData* targetTire = nullptr;
        String sensorConfigId = "";
        const char* position = "Unknown";
        
        if (isSensorMatch(address, name, SENSOR_ID_LF)) {
            targetTire = &tire_lf;
            sensorConfigId = SENSOR_ID_LF;
            position = "Left Front (LF)";
        } else if (isSensorMatch(address, name, SENSOR_ID_RF)) {
            targetTire = &tire_rf;
            sensorConfigId = SENSOR_ID_RF;
            position = "Right Front (RF)";
        } else if (isSensorMatch(address, name, SENSOR_ID_LR)) {
            targetTire = &tire_lr;
            sensorConfigId = SENSOR_ID_LR;
            position = "Left Rear (LR)";
        } else if (isSensorMatch(address, name, SENSOR_ID_RR)) {
            targetTire = &tire_rr;
            sensorConfigId = SENSOR_ID_RR;
            position = "Right Rear (RR)";
        }
        
        if (targetTire != nullptr) {
            std::string rawData = advertisedDevice->getManufacturerData();
            bool isServiceData = false;
            
            // Fallback to Service Data if Manufacturer Data is empty
            if (rawData.length() == 0) {
                rawData = advertisedDevice->getServiceData();
                isServiceData = true;
            }
            
            if (rawData.length() > 0) {
                const uint8_t* payload = (const uint8_t*)rawData.data();
                size_t length = rawData.length();
                
                Serial.printf("[TPMS] Match -> MAC: %s, Name: '%s', Payload Length: %d (%s)\n", 
                              address.toString().c_str(), name.c_str(), length, isServiceData ? "Service" : "Mfr");
                Serial.print("[TPMS] Raw Hex: ");
                for (size_t i = 0; i < length; i++) {
                    Serial.printf("%02X ", payload[i]);
                }
                Serial.println();
                
                if (length >= 14) {
                    int anchor = find_sensor_anchor(payload, length, sensorConfigId);
                    if (anchor >= 13 && (size_t)anchor < length) {
                        // Q5 Fixed-Point Pressure Byte (anchor - 13)
                        uint8_t pres_byte = payload[anchor - 13];

                        targetTire->raw_pressure_val = pres_byte;
                        targetTire->raw_bytes[0] = pres_byte;
                        targetTire->raw_bytes[1] = payload[anchor - 12];
                        targetTire->raw_bytes[2] = payload[anchor - 11];
                        targetTire->raw_bytes[3] = payload[anchor - 10];

                        // Q5 Fixed-Point Conversion (1 count = 1/32 BAR = 0.03125 BAR)
                        targetTire->pressure_bar = (float)pres_byte / 32.0f; // 0 -> 0.00 BAR, 6 -> 0.19 BAR, 79 -> 2.47 BAR
                        if (targetTire->pressure_bar < 0.0f) targetTire->pressure_bar = 0.0f;
                        targetTire->pressure_psi = targetTire->pressure_bar * 14.50377f;
                        targetTire->pressure_kpa = targetTire->pressure_bar * 100.0f;
                        targetTire->pressure_kgf = targetTire->pressure_bar * 1.01972f;

                        // Dynamic Temperature Byte (anchor - 6) with -12 C offset -> 28.0 C
                        uint8_t t_byte = payload[anchor - 6];
                        targetTire->temperature_c = (float)t_byte - 12.0f;

                        // Dynamic Battery Percentage & Voltage (anchor - 14) -> 0xB3 = 100%, 0xB1 = 99%
                        uint8_t bat_byte = payload[anchor - 14];
                        int bat_pct = (int)((float)bat_byte / 1.79f);
                        if (bat_pct > 100) bat_pct = 100;
                        if (bat_pct < 0) bat_pct = 0;
                        targetTire->battery_pct = bat_pct;
                        targetTire->battery_v = 2.0f + ((float)bat_pct / 100.0f) * 1.00f; // 100% -> 3.00V, 99% -> 2.99V
                        targetTire->status = payload[anchor - 7];

                        Serial.printf("[TPMS-ALL-OK] Pos: %s | pres_bar: %.2f BAR | temp_c: %.1f C | bat: %d%% (%.2fV)\n",
                                      position, targetTire->pressure_bar, targetTire->temperature_c, targetTire->battery_pct, targetTire->battery_v);
                    }
                } else if (length == 7 || length == 9) {
                    // 7-Byte BR Protocol Decoding: SS BB TT PPPP CCCC
                    size_t offset = (length == 9) ? 2 : 0;
                    const uint8_t* tpmsPayload = &payload[offset];
                    
                    targetTire->status = tpmsPayload[0];
                    targetTire->battery_v = tpmsPayload[1] / 10.0f;
                    targetTire->temperature_c = (int8_t)tpmsPayload[2]; // Raw Celsius (signed)
                    
                    uint16_t raw_pressure = (tpmsPayload[3] << 8) | tpmsPayload[4];
                    float abs_psi = raw_pressure / 10.0f;
                    
                    // Convert absolute to gauge pressure (PSI) by subtracting standard atmospheric pressure
                    targetTire->pressure_psi = abs_psi - 14.5f; 
                    if (targetTire->pressure_psi < 0.0f) {
                        targetTire->pressure_psi = 0.0f;
                    }
                    
                    // Convert PSI to BAR (1 PSI = 0.0689476 BAR)
                    targetTire->pressure_bar = targetTire->pressure_psi * 0.0689476f;
                    
                    // Calculate battery percentage (from 2.0V to 3.0V linear mapping)
                    targetTire->battery_pct = (int)((targetTire->battery_v - 2.0f) * 100.0f);
                    if (targetTire->battery_pct < 0) targetTire->battery_pct = 0;
                    if (targetTire->battery_pct > 100) targetTire->battery_pct = 100;
                } else {
                    Serial.printf("[TPMS] Warning: Unknown payload length %d. Cannot parse.\n", length);
                    return;
                }
                
                targetTire->last_update_time = millis();
                targetTire->active = true;
                
                Serial.printf("[TPMS] Successfully parsed %s -> Pressure: %.2f BAR (%.1f PSI), Temp: %.1f C, Battery: %d%% (%.1f V), Status: 0x%02X\n",
                              position, targetTire->pressure_bar, targetTire->pressure_psi, 
                              targetTire->temperature_c, targetTire->battery_pct, targetTire->battery_v, 
                              targetTire->status);
            }
        }
    }
};

// Continuous background scanning task
static void tpms_scan_task(void* pvParameters) {
    Serial.println("[TPMS] Starting NimBLE Scanning Task Loop...");
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
    pScan->setActiveScan(true);  // Active scanning enabled for maximum responsiveness
    pScan->setInterval(100);     // 100ms scan interval
    pScan->setWindow(100);        // 100ms scan window (100% duty cycle)
    pScan->setDuplicateFilter(false); // Process every incoming advertisement packet
    
    while (true) {
        pScan->start(0, false); // Scan continuously
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void tpms_init() {
    Serial.println("[TPMS] Initializing NimBLE Device...");
    NimBLEDevice::init("CivicGauges");
    
    xTaskCreatePinnedToCore(
        tpms_scan_task,
        "tpms_scan_task",
        16384, // Increased stack size to 16KB to prevent stack smashing/overflow
        NULL,
        1,
        NULL,
        0 // Run on Core 0 to keep Core 1 dedicated to GUI rendering
    );
}
