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

    // 0x324 - coolant temp and TRIP-AVERAGE fuel consumption. opendbc labels this
    // CRUISE/HUD_SPEED_KPH, but that would mean 125 km/h on a stationary car;
    // coolant is dash-verified and gives 85-92 C on the Si trace.
    //
    // byte1 was originally decoded as fuel level % (d[1]/2) and marked
    // CONFIRMED. That was wrong, not just noisy: a 49-min real outing with no
    // refuel had the dash reading >90% -> >80% while this formula sat at
    // 41-52% throughout, under any linear scale of the byte. Tank level is not
    // on this bus at all - ruled out exhaustively on 2026-08-29 against a 10%
    // tank (docs/CAN_PROTOCOL_MAP.md section G).
    //
    // UNITS: the raw byte IS the value, x10 L/100km (94 -> 9.4). Not km/L.
    // Settled 2026-08-29 by 10 minutes of idling in Park, engine running, car
    // never moving: the byte climbs 89 -> 98. Burning fuel while covering zero
    // distance drags a trip-average L/100km upward, which is exactly that. The
    // km/L reading would mean efficiency IMPROVING from 8.9 to 9.8 km/L while
    // stationary, which is impossible.
    //
    // AVERAGE, NOT INSTANT: this was called instant consumption on the
    // strength of a speed-binned correlation. That correlation was a time
    // artifact - the slow bins came early in the trip and the fast ones later.
    // Two deliberate wide-open-throttle pulls to the rev limiter move this
    // byte by ONE count (canlog_0005), and across the 37-min drive it traces a
    // textbook trip average: 9.4 cold at zero distance, improving to 8.0 by
    // 24 km of motorway, then back up to 8.7 as the drive slowed into town.
    // An instantaneous reading would swing with every throttle input and go to
    // its maximum whenever the car stopped. Callers wanting a live figure must
    // compute one; this byte cannot provide it.
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
                st->fuel_consumption_x10 = d[1];
                touch(st, SIG_FUEL_CONSUMPTION, now_ms);
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
        // Byte 0 is the ENGAGED gear, distinct from the selector in byte 3.
        // Confirmed by rpm-per-km/h, which forms a clean ratio ladder across
        // an 886 s drive - 113 / 62 / 42 / 28 for gears 1-4, i.e. steps of
        // 1.83, 1.47, 1.49. Value 7 has 40% scatter and is the shift /
        // converter-unlocked transient, not a gear. The ladder holds in both
        // D and S, so manual shifts in the S gate are fully visible.
        if (dlc >= 5) {
            st->gear_num = (d[0] >= 1 && d[0] <= 5) ? d[0] : 0;
            st->sport_mode = (d[4] & 0x10) != 0;   // S gate; b2 is 0x08 vs 0x80
        }
        break;

    // 0x1A6 byte 3 - FUEL TANK LEVEL. The signal this project hunted for
    // across four sessions, found on 2026-08-30 by recording an actual
    // refuel: over 42 s of pumping the byte climbs smoothly 40 -> 105, stops
    // dead the moment the pump does, and then sits at exactly 105 for a
    // 512 s drive on the full tank (sd 0.24, float against its upper stop).
    // Every dash reading ever noted lines up against raw/105:
    //   2026-08-15 ">90%" -> 89%      2026-08-15 ">80%" -> 81%
    //   2026-08-29 "~10%" -> 17%      2026-08-30 lamp lit -> 13%
    //   2026-08-30 "full" -> 100%
    //
    // The byte is half-litres, so one count is also one percent of the
    // nominal 50 L tank and no scaling is needed - see can_decode.h. The
    // ceiling of 105 is a brimmed tank (52.5 L, filler neck included), which
    // is why the dash needle sits above F right after a fill. Clamped to 100
    // for display only.
    //
    // Byte 4 moves opposite to byte 3 but is NOT its complement (the sums
    // drift), so it is left alone rather than guessed at.
    //
    // Why this took so long: the float sloshes. Driving, byte 3 swings +-8
    // counts around the true level, so every "find the byte that changes
    // smoothly/monotonically" search discarded it as noise. Sloshing IS the
    // signature of a real float gauge - see CAN_PROTOCOL_MAP.md section I.
    case 0x1A6:
        if (dlc >= 4) {
            st->fuel_level_pct = (uint8_t)clamp_i((int)d[3], 0, 100);
            st->fuel_level_valid = true;
            touch(st, SIG_FUEL_LEVEL, now_ms);
            // Byte 0 low two bits are the headlight switch position. The
            // scripted lights capture ramps 0->1->2->3 and back down, exactly
            // matching DRL -> position -> low -> high -> off in reverse. The
            // HIGH bits of the same byte pulse for 140-200 ms per
            // steering-wheel button press, which is why only 0x03 is masked.
            st->lights = (uint8_t)(d[0] & 0x03);
        } else hit = false;
        break;

    // 0x294 byte 0 - instrument-cluster telltales. Bit 0 is the low-fuel
    // lamp, corroborated independently by 0x405 b0 bit 2 (both agree across
    // every capture). The tight test it had to pass: on 2026-08-29 the tank
    // read 17% with the lamp NOT yet lit, and on 2026-08-30 it read 13% with
    // the lamp LIT - only three raw counts apart. Bits that merely track the
    // level byte fail that; these two do not.
    // Bits 5/6 are the turn-signal stalk, ordered left-then-right against a
    // scripted capture.
    case 0x294:
        if (dlc >= 1) {
            st->low_fuel   = (d[0] & 0x01) != 0;
            st->turn_left  = (d[0] & 0x20) != 0;
            st->turn_right = (d[0] & 0x40) != 0;
        } else hit = false;
        break;

    // 0x221 byte 2 bit 7 - ECON mode. One 78 s OFF window in an 886 s drive,
    // matching a test where ECON was switched off mid-drive and back on.
    case 0x221:
        if (dlc >= 3) {
            st->econ_on = (d[2] & 0x80) != 0;
        } else hit = false;
        break;

    // 0x377 bytes 1-2 - DISTANCE TRAVELLED, 50 m per count.
    // Calibrated against speed integrated over a 37.8 km drive: 758 counts,
    // 49.81 m/count, residual under 1.1 counts (55 m) across 38 samples an
    // hour apart; independently 48.7 m/count on a second log. Frozen while
    // idling in Park. It is 16-bit and wraps every 3276 km, so it is a
    // DELTA source, not an odometer reading - see EspDashProto.h.
    case 0x377:
        if (dlc >= 3) {
            st->odo_50m = be16(d, 1);
            st->odo_valid = true;
            touch(st, SIG_ODO, now_ms);
        } else hit = false;
        break;

    // 0x510 byte 1 - blower fan speed. Constant 5 in every capture except the
    // scripted climate test, where it steps 5-1-5-1-5-4-3-2-5 exactly while
    // the dial was being turned. Honda's dial goes to 7; 1-5 were exercised.
    // Byte 0 (0x58/0x54/0x4C) is the mode/distribution dial - only three
    // positions were seen, not enough to map, so it stays undecoded.
    case 0x510:
        if (dlc >= 2) {
            st->fan_speed = (d[1] <= 7) ? d[1] : 0;
        } else hit = false;
        break;

    // 0x21E byte 3 - ambient air temperature, offset by 40 (0x49 -> 33 C).
    //
    // This was byte 4 read as a direct signed degree count, which survived
    // only because 0x20 = 32 happened to sit next to a 32 C day. Byte 4 is a
    // bitfield: cycling the climate controls (canlog_0006) drives it through
    // 0x00/0x21/0x60/0x80/0x81/0xA0 - i.e. -128 C to +96 C on the gauge - and
    // in the 37-min road log it steps cleanly from 0x20 to 0x60 mid-drive and
    // stays there. Byte 3 behaves like a real sensor instead: 0x47-0x49 over
    // that same drive (31-33 C, drifting down as the evening cools) and 0x49
    // on 2026-08-29, when the dash read exactly 33 C.
    //
    // Byte 2 is a second temperature on the same offset (19-24 C over the
    // drive, falling as the cabin cools) - in-car temperature, not decoded
    // here because it has nowhere to go in the wire protocol yet.
    //
    // 0x372 is NOT an alternative source: it is DLC 2 whose byte 1 is pure
    // counter+checksum, and whose byte 0 only ever takes {0, 32} even during
    // a drive - a flag (0x20) that coincidentally matched a 32 C reading.
    case 0x21E:
        if (dlc >= 4) {
            int t = (int)d[3] - 40;
            if (t > -50 && t < 90) {
                st->ambient_temp = (int8_t)t;
                touch(st, SIG_AMBIENT, now_ms);
            }
            // Byte 2 is a second sensor on the same -40 offset: 24 -> 19 C
            // over a 37-min drive as the cabin cooled, 18-19 C on a 33 C day.
            int c = (int)d[2] - 40;
            if (c > -50 && c < 90) st->cabin_temp = (int8_t)c;
        } else hit = false;
        break;

    // 0x305 is NOT battery voltage - retracted 2026-08-29, left inert.
    //
    // Byte 0 was read as 100 mV per count on the strength of one number: it
    // reads 0x8E = 142 = "14.2 V". But it reads exactly 142 in every
    // stationary capture weeks apart (2026-08-06, 2026-08-08, and all five
    // 2026-08-29 recordings across 10 minutes of idling) - never 141, never
    // 143. A real alternator rail wanders with load; this does not vary at
    // all. And across the 37-min drive it sat at 0x06 and 0x0E, i.e. 0.6 V
    // and 1.4 V, which no running car produces.
    //
    // The values are bit patterns, not a measurement: 0x8E/0x4E/0x0E/0x06
    // share a low nibble and differ in bits 6-7, and byte 1 moves between
    // 0xA8 and 0x14 the same way. opendbc calls this ID SEATBELT_STATUS,
    // which fits a flag that is set while parked with the belt off and clear
    // while driving. Battery voltage has no known source on this bus, so the
    // gauge now reads nothing rather than a constant fiction.

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
