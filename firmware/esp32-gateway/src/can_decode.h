// =========================================================================
// HONDA CIVIC 9TH GEN (2012-2015) CAN DECODE ENGINE
// =========================================================================
// Deliberately free of Arduino/ESP-IDF dependencies so it can be compiled and
// tested on the host against recorded traces. See docs/CAN_PROTOCOL_MAP.md for
// the evidence behind every formula here.
//
// Sources used to validate this table:
//   - This car:   espDash_raw_can_log_2026-08-08 (2014 Civic, stationary)
//   - 9th gen:    rusEFI OEM-Docs/Honda/civic-2015-si-9gen/1-2-3.trc (driving)
//   - 8th gen:    rusEFI .../2008-civic-5d-r18k2/...-driving-10kmh.trc
//   - opendbc:    opendbc/dbc/generator/honda/_honda_common.dbc
// =========================================================================

#ifndef CAN_DECODE_H
#define CAN_DECODE_H

#include <stdint.h>
#include <stdbool.h>

// ---- Tunables ----------------------------------------------------------
// Raw accelerator-pedal value at wide-open throttle. 97 (the previous value)
// was simply the highest this car reached while revving stationary; the 9th
// gen Si trace reaches 139 on both 0x17C b0 and 0x13C b4 during real pulls.
// Retune after one WOT run and nothing else needs to change.
#define PEDAL_GAS_FULL_SCALE 139.0f

// USER_BRAKE physical value at full braking effort, in opendbc's units
// (raw * 0.015625 - 1.609375). 6.6 is the maximum observed in the 2026-08-08
// capture; retune after a hard-braking run.
#define USER_BRAKE_FULL_SCALE 6.6f

// Signal groups, for staleness tracking. A gauge should show "--" rather than
// a frozen value when its source message stops arriving.
enum CanSigGroup {
    SIG_RPM = 0, SIG_SPEED, SIG_WHEELS, SIG_STEER, SIG_BRAKE, SIG_THROTTLE,
    SIG_COOLANT, SIG_FUEL_CONSUMPTION, SIG_BATTERY, SIG_AMBIENT, SIG_GEAR, SIG_COUNT
};

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} CanFrame;

typedef struct {
    uint16_t rpm;
    uint16_t speed_kmh_x10;
    int16_t  water_temp_x10;
    int16_t  oil_temp_x10;      // never set from CAN - see note at bottom
    uint16_t battery_mv;
    uint8_t  gear;              // 0=P 1=R 2=N 3=D 4=S
    uint8_t  fuel_consumption_x10; // instant consumption, L/100km x10 (125 = 12.5). Wire-compat mirror of fuel_instant_x10
    uint8_t  fuel_instant_x10;     // computed instant consumption, L/100km x10 (0..250 = 0.0..25.0 L/100km)
    uint8_t  fuel_avg_x10;         // average fuel consumption, L/100km x10, converted from 0x324 km/L
    uint8_t  raw_fuel_km_l_x10;    // raw 0x324 byte 1 (km/L x10)
    int16_t  steering_deg;      // negative = left
    int16_t  steering_rate_dps;
    int8_t   ambient_temp;
    uint8_t  throttle_pct;
    uint8_t  brake_pct;
    uint16_t wheel_fl_x10, wheel_fr_x10, wheel_rl_x10, wheel_rr_x10;

    bool brake_switch;      // 0x17C bit 32
    bool gas_pressed;       // 0x17C bit 39
    bool wheels_moving;     // 0x1B0 bit 12
    bool esp_disabled;      // 0x1A4 bit 28  UNVERIFIED on this chassis
    bool computer_braking;  // 0x1A4 bit 23  UNVERIFIED on this chassis

    // No source message for these has been found in any capture. They are kept
    // so the JSON contract is stable, and stay false rather than being fed
    // from 0x1A0, which does not exist on a 9th gen bus.
    bool abs_active;
    bool tc_active;
    bool cel;

    uint32_t last_update_ms[SIG_COUNT];
    uint32_t frames_decoded;
    uint32_t checksum_rejects;
} CanDecodeState;

// Honda's 4-bit checksum, stored in the low nibble of the final byte.
// Verified against 112,962 real frames: 44 of 45 IDs pass at exactly 100%
// (0x255 is the sole exception and uses a different scheme).
uint8_t honda_checksum(uint32_t id, const uint8_t *data, uint8_t dlc);
bool    honda_checksum_valid(uint32_t id, const uint8_t *data, uint8_t dlc);

void can_decode_init(CanDecodeState *st);

// Applies one frame. Returns true if it updated any signal. Frames whose Honda
// checksum fails are rejected (and counted) so a corrupted frame can never
// drive a gauge.
bool can_decode_frame(CanDecodeState *st, const CanFrame *f, uint32_t now_ms);

// True when the group's source message has not been seen for timeout_ms.
bool can_decode_is_stale(const CanDecodeState *st, enum CanSigGroup g,
                         uint32_t now_ms, uint32_t timeout_ms);

// Derived warning bits (engine running / shift / overheat), computed once per
// publish rather than per frame. Returns an ESPDASH_FLAG_* bitmask.
uint8_t can_decode_flags(const CanDecodeState *st);

#endif  // CAN_DECODE_H
