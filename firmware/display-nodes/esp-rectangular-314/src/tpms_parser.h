#ifndef TPMS_PARSER_H
#define TPMS_PARSER_H

#include <Arduino.h>

struct TPMSData {
    float pressure_bar = 0.0f;
    float pressure_psi = 0.0f;
    float pressure_kpa = 0.0f;
    float pressure_kgf = 0.0f;
    uint16_t raw_pressure_val = 0;
    uint8_t raw_bytes[4] = {0};
    float temperature_c = 0.0f;
    float battery_v = 0.0f;
    int battery_pct = 0;
    uint8_t status = 0;
    uint32_t last_update_time = 0;
    bool active = false;
};

// Global TPMS readings for each tire position
extern TPMSData tire_lf;
extern TPMSData tire_rf;
extern TPMSData tire_lr;
extern TPMSData tire_rr;

// Initialize NimBLE TPMS Scanner task
void tpms_init();

#endif // TPMS_PARSER_H
