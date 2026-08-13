#include "can_decode.h"
#include <string.h>

// Flag bits duplicated from EspDashProto.h so this stays dependency-free for
// host builds. Kept in sync by can_decode_flags_match_proto() in the tests.
#define FLAG_ENGINE_RUNNING 0x01
#define FLAG_SHIFT_WARNING  0x02
#define FLAG_OVERHEAT       0x04
#define FLAG_ABS_ACTIVE     0x08
#define FLAG_TC_ACTIVE      0x10
#define FLAG_BRAKE_SWITCH   0x20
#define FLAG_CEL            0x40
#define FLAG_VSA_WARNING    0x80

// -------------------------------------------------------------------------
// Bit helpers
// -------------------------------------------------------------------------
static inline uint16_t be16(const uint8_t *d, int i) {
    return (uint16_t)(((uint16_t)d[i] << 8) | d[i + 1]);
}

static inline uint64_t be64_of(const uint8_t *d) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | d[i];
    return v;
}

// DBC big-endian (Motorola, "sawtooth") extraction. `start_bit` is the DBC
// start bit; the signal's MSB lives there and runs toward lower bit numbers.
static inline uint32_t mot_bits(uint64_t v, int start_bit, int length) {
    int msb_from_left = (start_bit / 8) * 8 + (7 - (start_bit % 8));
    int shift = 64 - (msb_from_left + length);
    return (uint32_t)((v >> shift) & ((1ULL << length) - 1));
}

static inline int clamp_i(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Convert a 0.01 km/h raw value to our 0.1 km/h fixed point, with rounding.
static inline uint16_t kph100_to_x10(uint32_t raw) {
    return (uint16_t)((raw + 5) / 10);
}

// -------------------------------------------------------------------------
// Honda checksum
// -------------------------------------------------------------------------
uint8_t honda_checksum(uint32_t id, const uint8_t *data, uint8_t dlc) {
    uint32_t s = 0;
    uint32_t a = id;
    while (a > 0) {
        s += a & 0xF;
        a >>= 4;
    }
    for (uint8_t i = 0; i < dlc; i++) {
        uint8_t x = data[i];
        if (i == dlc - 1) x >>= 4;  // the checksum nibble itself is excluded
        s += (uint32_t)(x & 0xF) + (uint32_t)(x >> 4);
    }
    return (uint8_t)((8 - s) & 0xF);
}

bool honda_checksum_valid(uint32_t id, const uint8_t *data, uint8_t dlc) {
    if (dlc == 0) return false;
    return (data[dlc - 1] & 0x0F) == honda_checksum(id, data, dlc);
}

// -------------------------------------------------------------------------
void can_decode_init(CanDecodeState *st) {
    memset(st, 0, sizeof(*st));
    st->gear = 0xFF;  // unknown until 0x188 arrives
}

static inline void touch(CanDecodeState *st, enum CanSigGroup g, uint32_t now) {
    st->last_update_ms[g] = now;
}

bool can_decode_frame(CanDecodeState *st, const CanFrame *f, uint32_t now_ms) {
    const uint8_t *d = f->data;
    const uint8_t dlc = f->dlc;

    // 8-byte Honda CAN frames carry a 4-bit counter+checksum in the last byte.
    // Short frames (DLC < 8) carry pure data without a checksum nibble.
    if (dlc >= 8 && !honda_checksum_valid(f->id, d, dlc)) {
        st->checksum_rejects++;
        return false;
    }

    bool hit = true;
    switch (f->id) {

    // 0x158 ENGINE_DATA - vehicle speed + RPM + odometer.
    // Previously misread as "byte0 - 40 = coolant", which it never was.
    case 0x158:
        if (dlc >= 8) {
            st->speed_kmh_x10 = kph100_to_x10(be16(d, 0));
            touch(st, SIG_SPEED, now_ms);
            // b2-3 also carries RPM, but it reads 0 on the 2015 Si trace while
            // 0x17C is populated on every car seen. 0x17C stays primary.
        } else hit = false;
        break;

    // 0x17C POWERTRAIN_DATA - pedal position, RPM, brake switch.
    // Primary RPM source: valid on this car (644-5115), the 2015 Si
    // (616-3653), and independently documented for the 8th gen by carhack.
    case 0x17C:
        if (dlc >= 8) {
            st->rpm = be16(d, 2);
            touch(st, SIG_RPM, now_ms);
            st->throttle_pct = (uint8_t)clamp_i(
                (int)((d[0] / PEDAL_GAS_FULL_SCALE) * 100.0f + 0.5f), 0, 100);
            touch(st, SIG_THROTTLE, now_ms);
            st->gas_pressed   = (d[4] & 0x80) != 0;  // bit 39
            st->brake_switch  = (d[4] & 0x01) != 0;  // bit 32
        } else hit = false;
        break;

    // 0x1DC - secondary RPM (b1-2). Tracks 0x17C within a few rpm on both
    // cars. Used only when 0x17C has gone stale.
    case 0x1DC:
        if (dlc >= 3) {
            if (can_decode_is_stale(st, SIG_RPM, now_ms, 500)) {
                st->rpm = be16(d, 1);
                touch(st, SIG_RPM, now_ms);
            }
        } else hit = false;
        break;

    // 0x1D0 WHEEL_SPEEDS - four 15-bit fields at 0.01 km/h, packed
    // 15+15+15+15+4(checksum) = 64 bits. NOT four aligned 16-bit words: that
    // reading yields 166/328/666/1297 km/h on a trace recorded at 10 km/h.
    case 0x1D0:
        if (dlc >= 8) {
            uint64_t v = be64_of(d);
            st->wheel_fl_x10 = kph100_to_x10(mot_bits(v, 7, 15));
            st->wheel_fr_x10 = kph100_to_x10(mot_bits(v, 8, 15));
            st->wheel_rl_x10 = kph100_to_x10(mot_bits(v, 25, 15));
            st->wheel_rr_x10 = kph100_to_x10(mot_bits(v, 42, 15));
            touch(st, SIG_WHEELS, now_ms);
            // Fall back to front-left if 0x158 is absent, so the speedo still
            // reads on a bus where ENGINE_DATA is not populated.
            if (can_decode_is_stale(st, SIG_SPEED, now_ms, 500)) {
                st->speed_kmh_x10 = st->wheel_fl_x10;
                touch(st, SIG_SPEED, now_ms);
            }
        } else hit = false;
        break;

    // 0x156 STEERING_SENSORS - angle is a signed 16-bit, magnitude scaled by
    // 0.1. The old /9.0 gave +-536 deg, beyond the sensor's +-500 spec; /10
    // fixed the magnitude (confirmed on the 2015 Si trace: -493..+514 deg).
    // The SIGN was set from opendbc's documented factor (-0.1) but that was
    // only ever checked by magnitude, never against a real left/right turn.
    // On-car test on this chassis (2026-08-09) showed it inverted, so the
    // negation opendbc uses does not carry over here - dropped.
    case 0x156:
        if (dlc >= 4) {
            int16_t raw = (int16_t)be16(d, 0);
            st->steering_deg = (int16_t)(raw / 10);
            st->steering_rate_dps = (int16_t)be16(d, 2);
            touch(st, SIG_STEER, now_ms);
        } else hit = false;
        break;

    // 0x1A4 VSA_STATUS - USER_BRAKE is a 16-bit field spanning bytes 0-1.
    // Reading byte 1 alone showed ~38% brake at rest and wrapped to 0 under
    // hard braking once the raw value passed 255.
    case 0x1A4:
        if (dlc >= 8) {
            float phys = be16(d, 0) * 0.015625f - 1.609375f;
            if (phys < 0.0f) phys = 0.0f;
            st->brake_pct = (uint8_t)clamp_i(
                (int)((phys / USER_BRAKE_FULL_SCALE) * 100.0f + 0.5f), 0, 100);
            touch(st, SIG_BRAKE, now_ms);
            st->computer_braking = (d[2] & 0x80) != 0;  // bit 23, UNVERIFIED
            st->esp_disabled     = (d[3] & 0x10) != 0;  // bit 28, UNVERIFIED
        } else hit = false;
        break;

    // 0x1B0 STANDSTILL - WHEELS_MOVING at bit 12. Confirmed on the 9th gen:
    // high 89.3% of a driving trace and agreeing with 0x1D0 speed on 88.2%
    // of paired samples.
    case 0x1B0:
        if (dlc >= 2) st->wheels_moving = (d[1] & 0x10) != 0;
        else hit = false;
        break;

    // 0x324 - coolant and fuel. opendbc labels this CRUISE/HUD_SPEED_KPH, but
    // that would mean 125 km/h on a stationary car; the coolant/fuel reading
    // is dash-verified here and gives 85-92 C / 28-31.5% on the Si trace.
    case 0x324:
        if (dlc >= 2) {
            if (d[0] > 0) {
                int t = (int)d[0] - 40;
                if (t > 0 && t < 140) {
                    st->water_temp_x10 = (int16_t)(t * 10);
                    touch(st, SIG_COOLANT, now_ms);
                }
            }
            if (d[1] <= 200) {
                st->fuel_pct = (uint8_t)clamp_i(d[1] / 2, 0, 100);
                touch(st, SIG_FUEL, now_ms);
            }
        } else hit = false;
        break;

    // 0x188 - automatic gearbox selector. Absent from the 2015 Si (manual),
    // so this mapping rests entirely on this car's own data - originally the
    // shifter-movement capture, corrected by an on-car road test (2026-08-09)
    // that showed every position off by one against the physical selector.
    // The corrected byte values are a clean single-bit progression for
    // P/R/N/D, with S falling through as zero - a much more plausible
    // encoding than the scattered one it replaces.
    case 0x188:
        if (dlc >= 4) {
            switch (d[3]) {
                case 0x01: st->gear = 0; break;  // P
                case 0x02: st->gear = 1; break;  // R
                case 0x04: st->gear = 2; break;  // N
                case 0x08: st->gear = 3; break;  // D
                case 0x00: st->gear = 4; break;  // S
                default: hit = false; break;
            }
            if (hit) touch(st, SIG_GEAR, now_ms);
        } else hit = false;
        break;

    // 0x21E byte 4 - ambient air temperature, direct degrees C.
    // 0x372 is NOT an alternative source: it is DLC 2 whose byte 1 is pure
    // counter+checksum, and whose byte 0 only ever takes {0, 32} even during
    // a drive - a flag (0x20) that coincidentally matched a 32 C reading.
    case 0x21E:
        if (dlc >= 5) {
            st->ambient_temp = (int8_t)d[4];
            touch(st, SIG_AMBIENT, now_ms);
        } else hit = false;
        break;

    // 0x305 byte 0 - 12V battery, 100 mV per count. Dash-verified at 14.2 V.
    case 0x305:
        if (dlc >= 1) {
            if (d[0] >= 50 && d[0] <= 200) {
                st->battery_mv = (uint16_t)(d[0] * 100);
                touch(st, SIG_BATTERY, now_ms);
            }
        } else hit = false;
        break;

    default:
        hit = false;
        break;
    }

    if (hit) st->frames_decoded++;
    return hit;
}

bool can_decode_is_stale(const CanDecodeState *st, enum CanSigGroup g,
                         uint32_t now_ms, uint32_t timeout_ms) {
    uint32_t last = st->last_update_ms[g];
    if (last == 0) return true;             // never seen
    return (uint32_t)(now_ms - last) > timeout_ms;
}

uint8_t can_decode_flags(const CanDecodeState *st) {
    uint8_t f = 0;
    if (st->rpm > 400)               f |= FLAG_ENGINE_RUNNING;
    if (st->rpm > 6800)              f |= FLAG_SHIFT_WARNING;
    if (st->water_temp_x10 > 1050)   f |= FLAG_OVERHEAT;
    if (st->brake_switch)            f |= FLAG_BRAKE_SWITCH;
    if (st->abs_active)              f |= FLAG_ABS_ACTIVE;
    if (st->tc_active)               f |= FLAG_TC_ACTIVE;
    if (st->cel)                     f |= FLAG_CEL;
    if (st->esp_disabled)            f |= FLAG_VSA_WARNING;
    return f;
}

// -------------------------------------------------------------------------
// NOT DECODED, and why
// -------------------------------------------------------------------------
// oil_temp : not present on the broadcast bus in any of the four captures.
//            It exists only as a Mode-22 diagnostic PID, which would require
//            transmitting a request. The gateway is TWAI_MODE_LISTEN_ONLY by
//            design, so this stays 0.
// abs / tc : 0x1A0, the ID the previous firmware read, does not exist on a
//            9th gen bus (it is present only on the 2008 8th gen car). No
//            capture available contains an ABS or TC activation, so there is
//            nothing to correlate against. See docs/CAPTURE_ABS_TC.md.
// cel      : no candidate identified.
