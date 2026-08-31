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
// Raw accelerator-pedal value at wide-open throttle. Retuned on this car
// (2026-08-29) by the WOT run the previous two values were only guesses at:
// 97 was the most this car reached while revving stationary, and 139 was the
// 2015 Si trace's maximum. Both clip badly. Two deliberate full-throttle pulls
// to the rev limiter in Park (canlog_0005) plateau at 211-213 for 2.5 s each -
// the pedal against its kickdown detent - and the 37-min road log (canlog_0004)
// holds 214 then 255 for 4.5 s through a single kickdown. So the field is a
// plain 0-255 scale, and 139 was reporting 100 % from just 55 % of travel.
#define PEDAL_GAS_FULL_SCALE 255.0f

// 0x1A6 byte 3 is fuel in HALF-LITRES, which makes one count also one percent
// of the nominal 50 L tank. Two independent numbers say so:
//
//  - Litres per count, measured against the odometer: over one 77.7 km outing
//    (true distance from 0x377, gaps included) at the trip computer's own
//    8.4 L/100km, 6.6 L burned for a 13-count drop = 0.50 L per count.
//  - That puts the observed ceiling of 105 counts at 52.5 L: a 50 L tank plus
//    ~2.5 L of filler neck. Nothing about a "105 = 100%" reading ever
//    explained why the ceiling is 105 rather than 100 or 255.
//
// So a brimmed tank genuinely sits ABOVE 100% - which is exactly what Gleb saw
// on the dash after filling, and the observation that prompted this. The gauge
// is clamped for display because "104%" reads like a fault, but the raw byte
// is not rescaled: at the low end, where a driver actually needs the number,
// raw IS the percentage with no fudge factor.
//
// Cross-check: the low-fuel lamp lights at 13-14 counts = 6.5-7.0 L remaining,
// which is Honda's reserve for this car.
#define FUEL_LEVEL_L_PER_COUNT  0.5f
#define FUEL_LEVEL_FULL_RAW     100.0f   // nominal 100% = 50 L; 105 = brimmed

// USER_BRAKE physical value at full braking effort, in opendbc's units
// (raw * 0.015625 - 1.609375). 6.6 is the maximum observed in the 2026-08-08
// capture; retune after a hard-braking run.
#define USER_BRAKE_FULL_SCALE 6.6f

// Signal groups, for staleness tracking. A gauge should show "--" rather than
// a frozen value when its source message stops arriving.
enum CanSigGroup {
    SIG_RPM = 0, SIG_SPEED, SIG_WHEELS, SIG_STEER, SIG_BRAKE, SIG_THROTTLE,
    SIG_COOLANT, SIG_FUEL_CONSUMPTION, SIG_BATTERY, SIG_AMBIENT, SIG_GEAR,
    SIG_FUEL_LEVEL, SIG_ODO, SIG_COUNT
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
    uint8_t  fuel_consumption_x10; // TRIP-AVERAGE consumption, L/100km x10
                                    // (94 = 9.4). Name kept for wire
                                    // compatibility; the value is an average,
                                    // not an instantaneous figure - see the
                                    // 0x324 case in can_decode.cpp. NOT tank
                                    // level: that byte was misread as fuel %
                                    // until 2026-08-15, and tank level is not
                                    // broadcast on this bus at all.
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

    // ---- added 2026-08-30, from the refuel + full-tank captures ---------
    // FUEL_LEVEL_FULL_RAW is the top of the gauge, not the top of the byte:
    // 0x1A6 b3 has never exceeded 105 in any capture, and pins there exactly
    // on a full tank. Assuming a 0-255 range is what made this signal
    // invisible to an earlier search - see CAN_PROTOCOL_MAP.md section I.
    uint8_t  fuel_level_pct;    // 0-100 %, valid once fuel_level_valid
    bool     fuel_level_valid;  // false until 0x1A6 has been seen
    bool     low_fuel;          // dash low-fuel lamp lit (0x294 b0 bit 0)
    uint8_t  gear_num;          // engaged gear 1-5, 0 = none/shifting
    bool     econ_on;           // ECON mode engaged (0x221 b2 bit 7)
    bool     sport_mode;        // gearbox in the S gate (0x188 b4 bit 4)
    bool     turn_left;         // 0x294 b0 bit 5
    bool     turn_right;        // 0x294 b0 bit 6

    // ---- completing the decoded set, wired 2026-08-30 -------------------
    uint16_t odo_50m;           // 0x377 BE16(b1,b2): distance in 50 m units.
                                // 16-bit and wraps every 3276 km - CONSUMERS
                                // MUST TAKE DELTAS, never read it as a total.
    bool     odo_valid;
    int8_t   cabin_temp;        // 0x21E b2 - 40, in-car temperature
    uint8_t  fan_speed;         // 0x510 b1, blower 0-7 (1-5 observed)
    uint8_t  lights;            // 0x1A6 b0 & 0x03: 0=DRL 1=position
                                //   2=low beam 3=high beam

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
