// =========================================================================
// Host-side decode tests: replay real recorded traces through can_decode.
// =========================================================================
//   pio test -e native -d firmware/esp32-gateway
//
// The decisive test is test_wheel_speeds_8th_gen_10kmh: the source trace is a
// car recorded while driving at 10 km/h, so any wheel-speed bit packing that
// does not produce ~10 km/h is wrong by construction. The previous firmware's
// four-aligned-16-bit reading yields 166/328/666/1297 km/h on this same data.
// =========================================================================

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "can_decode.h"
#include "EspDashSignals.h"

#ifndef FIXTURE_DIR
#define FIXTURE_DIR "test/fixtures"
#endif

// -------------------------------------------------------------------------
// Replay harness
// -------------------------------------------------------------------------
typedef struct {
    // per-signal observed extremes
    float wheel_min[4], wheel_max[4];
    float max_wheel_spread;      // worst max-min across the four wheels
    double spread_sum;
    int    spread_n;
    uint16_t rpm_min, rpm_max;
    float speed_min, speed_max;
    int16_t steer_min, steer_max;
    float coolant_min, coolant_max;
    float fuel_min, fuel_max;
    float batt_min, batt_max;
    int   ambient_min, ambient_max;
    int   fuel_lvl_min, fuel_lvl_max;
    int   low_fuel_true, low_fuel_n;
    uint8_t gear_num_max;
    int   sport_true, econ_true;
    uint8_t brake_min, brake_max;
    uint8_t throttle_max;
    int   wheels_moving_true, wheels_moving_n;
    int   frames, decoded;
    uint32_t rejects;
} ReplayStats;

static void stats_init(ReplayStats *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < 4; i++) { s->wheel_min[i] = 1e9f; s->wheel_max[i] = -1e9f; }
    s->rpm_min = 0xFFFF;
    s->speed_min = 1e9f; s->speed_max = -1e9f;
    s->steer_min = 32767; s->steer_max = -32768;
    s->coolant_min = 1e9f; s->coolant_max = -1e9f;
    s->fuel_min = 1e9f; s->fuel_max = -1e9f;
    s->batt_min = 1e9f; s->batt_max = -1e9f;
    s->ambient_min = 127; s->ambient_max = -128;
    s->fuel_lvl_min = 999; s->fuel_lvl_max = -1;
    s->brake_min = 255;
}

// Replays a fixture. `moving_only` restricts the wheel/spread statistics to
// samples where the car is actually rolling, so a stationary prologue does not
// mask a packing error.
static bool replay(const char *name, ReplayStats *out, bool moving_only) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("FIXTURE NOT FOUND: %s\n", path);
        return false;
    }

    CanDecodeState st;
    can_decode_init(&st);
    stats_init(out);

    char line[256];
    uint32_t now = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        CanFrame f;
        memset(&f, 0, sizeof(f));
        unsigned id, dlc;
        char *p = line;
        if (sscanf(p, "%x %u", &id, &dlc) != 2) continue;
        if (dlc > 8) continue;
        // advance past "ID DLC"
        p = strchr(p, ' '); if (!p) continue;
        p = strchr(p + 1, ' '); if (!p) continue;
        for (unsigned i = 0; i < dlc; i++) {
            unsigned b;
            if (sscanf(p, " %x", &b) != 1) { dlc = i; break; }
            f.data[i] = (uint8_t)b;
            p = strchr(p + 1, ' ');
            if (!p) { dlc = i + 1; break; }
        }
        f.id = id;
        f.dlc = (uint8_t)dlc;

        now += 1;  // 1 ms per frame keeps every signal fresh during replay
        out->frames++;
        if (can_decode_frame(&st, &f, now)) out->decoded++;

        float w[4] = { st.wheel_fl_x10 / 10.0f, st.wheel_fr_x10 / 10.0f,
                       st.wheel_rl_x10 / 10.0f, st.wheel_rr_x10 / 10.0f };
        bool rolling = w[0] > 1.0f || w[1] > 1.0f || w[2] > 1.0f || w[3] > 1.0f;
        if (!moving_only || rolling) {
            float lo = w[0], hi = w[0];
            for (int i = 0; i < 4; i++) {
                if (w[i] < out->wheel_min[i]) out->wheel_min[i] = w[i];
                if (w[i] > out->wheel_max[i]) out->wheel_max[i] = w[i];
                if (w[i] < lo) lo = w[i];
                if (w[i] > hi) hi = w[i];
            }
            if (rolling) {
                float sp = hi - lo;
                if (sp > out->max_wheel_spread) out->max_wheel_spread = sp;
                out->spread_sum += sp;
                out->spread_n++;
            }
        }

        if (st.rpm < out->rpm_min) out->rpm_min = st.rpm;
        if (st.rpm > out->rpm_max) out->rpm_max = st.rpm;
        float sp = st.speed_kmh_x10 / 10.0f;
        if (sp < out->speed_min) out->speed_min = sp;
        if (sp > out->speed_max) out->speed_max = sp;
        if (st.steering_deg < out->steer_min) out->steer_min = st.steering_deg;
        if (st.steering_deg > out->steer_max) out->steer_max = st.steering_deg;
        if (st.water_temp_x10 != 0) {
            float c = st.water_temp_x10 / 10.0f;
            if (c < out->coolant_min) out->coolant_min = c;
            if (c > out->coolant_max) out->coolant_max = c;
        }
        if (st.last_update_ms[SIG_FUEL_CONSUMPTION]) {
            if (st.fuel_consumption_x10 < out->fuel_min) out->fuel_min = st.fuel_consumption_x10;
            if (st.fuel_consumption_x10 > out->fuel_max) out->fuel_max = st.fuel_consumption_x10;
        }
        if (st.battery_mv) {
            float v = st.battery_mv / 1000.0f;
            if (v < out->batt_min) out->batt_min = v;
            if (v > out->batt_max) out->batt_max = v;
        }
        if (st.last_update_ms[SIG_AMBIENT]) {
            if (st.ambient_temp < out->ambient_min) out->ambient_min = st.ambient_temp;
            if (st.ambient_temp > out->ambient_max) out->ambient_max = st.ambient_temp;
        }
        if (st.fuel_level_valid) {
            if (st.fuel_level_pct < out->fuel_lvl_min) out->fuel_lvl_min = st.fuel_level_pct;
            if (st.fuel_level_pct > out->fuel_lvl_max) out->fuel_lvl_max = st.fuel_level_pct;
        }
        if (f.id == 0x294) {
            out->low_fuel_n++;
            if (st.low_fuel) out->low_fuel_true++;
        }
        if (st.gear_num > out->gear_num_max) out->gear_num_max = st.gear_num;
        if (st.sport_mode) out->sport_true++;
        if (st.econ_on) out->econ_true++;
        if (st.brake_pct < out->brake_min) out->brake_min = st.brake_pct;
        if (st.brake_pct > out->brake_max) out->brake_max = st.brake_pct;
        if (st.throttle_pct > out->throttle_max) out->throttle_max = st.throttle_pct;
        if (f.id == 0x1B0) {
            out->wheels_moving_n++;
            if (st.wheels_moving) out->wheels_moving_true++;
        }
    }
    out->rejects = st.checksum_rejects;
    fclose(fp);
    return true;
}

// =========================================================================
// Honda checksum
// =========================================================================
static void test_honda_checksum(void) {
    // Real frame from this car: 0x1DC 02 02 CB 12
    const uint8_t f1[4] = {0x02, 0x02, 0xCB, 0x12};
    TEST_ASSERT_TRUE(honda_checksum_valid(0x1DC, f1, 4));

    // Real frame: 0x324 7D 65 14 6F 00 00 00 15
    const uint8_t f2[8] = {0x7D, 0x65, 0x14, 0x6F, 0x00, 0x00, 0x00, 0x15};
    TEST_ASSERT_TRUE(honda_checksum_valid(0x324, f2, 8));

    // Corrupt one payload byte: must be rejected.
    uint8_t bad[8];
    memcpy(bad, f2, 8);
    bad[2] ^= 0xFF;
    TEST_ASSERT_FALSE(honda_checksum_valid(0x324, bad, 8));
}

static void test_checksum_gates_decode(void) {
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x324; f.dlc = 8;
    const uint8_t good[8] = {0x7D, 0x65, 0x14, 0x6F, 0x00, 0x00, 0x00, 0x15};
    memcpy(f.data, good, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 100));
    TEST_ASSERT_EQUAL_INT16(850, st.water_temp_x10);   // 125 - 40 = 85.0 C

    f.data[0] = 0x99;   // corrupt: checksum no longer matches
    TEST_ASSERT_FALSE(can_decode_frame(&st, &f, 200));
    TEST_ASSERT_EQUAL_INT16(850, st.water_temp_x10);   // unchanged
    TEST_ASSERT_EQUAL_UINT32(1, st.checksum_rejects);
}

// =========================================================================
// THE decisive test: a trace recorded at a known 10 km/h
// =========================================================================
static void test_wheel_speeds_8th_gen_10kmh(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic8_10kmh.txt", &s, true));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);

    printf("\n  [8th gen 10km/h] FL %.2f-%.2f  FR %.2f-%.2f  RL %.2f-%.2f  RR %.2f-%.2f\n",
           s.wheel_min[0], s.wheel_max[0], s.wheel_min[1], s.wheel_max[1],
           s.wheel_min[2], s.wheel_max[2], s.wheel_min[3], s.wheel_max[3]);
    printf("  [8th gen 10km/h] max spread %.2f km/h, XMISSION speed %.2f-%.2f km/h\n",
           s.max_wheel_spread, s.speed_min, s.speed_max);

    // Every wheel must land in a believable band around 10 km/h.
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE_MESSAGE(s.wheel_min[i] > 7.0f, "wheel speed implausibly low");
        TEST_ASSERT_TRUE_MESSAGE(s.wheel_max[i] < 11.0f, "wheel speed implausibly high");
    }
    // Four wheels on one car agree closely. The old 16-bit reading gives a
    // spread of ~1228 km/h here.
    TEST_ASSERT_TRUE_MESSAGE(s.max_wheel_spread < 1.0f, "wheels disagree - bit packing wrong");

    // And they must agree with the independent speed signal in 0x158.
    TEST_ASSERT_TRUE(s.speed_max > 8.0f && s.speed_max < 11.0f);
}

// =========================================================================
// Same generation as this car, with the car actually moving
// =========================================================================
static void test_9th_gen_si_drive(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic9_si_drive.txt", &s, true));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);

    printf("\n  [9th gen Si] rpm %u-%u  steer %d..%d deg  coolant %.1f-%.1f C\n",
           s.rpm_min, s.rpm_max, s.steer_min, s.steer_max, s.coolant_min, s.coolant_max);
    printf("  [9th gen Si] wheels max %.1f km/h, mean spread %.2f, throttle max %u%%\n",
           s.wheel_max[0], s.spread_n ? s.spread_sum / s.spread_n : 0.0, s.throttle_max);

    // RPM from 0x17C bytes 2-3.
    TEST_ASSERT_TRUE(s.rpm_max > 3000 && s.rpm_max < 8000);

    // Steering: -0.1 deg/count keeps this inside the sensor's +-500 range.
    // The old /9.0 scaling produced +-548..572 here.
    TEST_ASSERT_TRUE_MESSAGE(s.steer_min > -530, "steering under-range - wrong scale");
    TEST_ASSERT_TRUE_MESSAGE(s.steer_max < 530, "steering over-range - wrong scale");
    TEST_ASSERT_TRUE_MESSAGE(s.steer_min < -300, "steering never went left");
    TEST_ASSERT_TRUE_MESSAGE(s.steer_max > 300, "steering never went right");

    // Coolant on a warmed-up engine.
    TEST_ASSERT_TRUE(s.coolant_min > 80.0f && s.coolant_max < 100.0f);

    // Wheels agree with each other while rolling.
    TEST_ASSERT_TRUE_MESSAGE(s.spread_n > 100, "not enough rolling samples");
    TEST_ASSERT_TRUE_MESSAGE((s.spread_sum / s.spread_n) < 1.0,
                             "wheels disagree while rolling");

    // WHEELS_MOVING (0x1B0 bit 12) should be true for most of a driving trace.
    TEST_ASSERT_TRUE(s.wheels_moving_n > 0);
    float moving_frac = (float)s.wheels_moving_true / s.wheels_moving_n;
    printf("  [9th gen Si] WHEELS_MOVING true %.1f%% of samples\n", moving_frac * 100.0f);
    TEST_ASSERT_TRUE_MESSAGE(moving_frac > 0.80f, "WHEELS_MOVING bit looks wrong");
}

// =========================================================================
// This car: the dash-verified values must not move
// =========================================================================
static void test_user_2014_capture_regression(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic9_user_2014.txt", &s, false));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);

    printf("\n  [this car] coolant %.1f-%.1f C  fuel consumption %.0f-%.0f (x10 L/100km)  "
           "batt %.2f-%.2f V  ambient %d-%d C\n",
           s.coolant_min, s.coolant_max, s.fuel_min, s.fuel_max,
           s.batt_min, s.batt_max, s.ambient_min, s.ambient_max);
    printf("  [this car] rpm %u-%u  wheels max %.1f km/h\n",
           s.rpm_min, s.rpm_max, s.wheel_max[0]);

    // Dash-verified against the instrument cluster; these are the anchors.
    TEST_ASSERT_TRUE(s.coolant_min >= 82.0f && s.coolant_max <= 88.0f);
    // This fixture is a stationary/idle capture (2026-08-08). It is NOT a
    // dash-verified anchor for fuel like the others here - it was originally
    // written as one, decoding byte1/2 as fuel level %, before a 2026-08-15
    // real drive with no refuel proved that formula wrong at the absolute
    // value, not just noisy (see CAN_PROTOCOL_MAP.md). Re-pinned to the raw
    // byte the fixture actually contains (101-102), now understood as instant
    // consumption x10 L/100km - a plausible idle-range reading (10.1-10.2),
    // not a claim this fixture proves the formula, only a regression guard.
    TEST_ASSERT_TRUE(s.fuel_min >= 100.0f && s.fuel_max <= 103.0f);
    // Battery voltage is no longer decoded: 0x305 byte 0 is a bitfield that
    // happens to equal 142 while parked (see can_decode.cpp). Nothing may set
    // it, so the min/max sentinels must come back untouched.
    TEST_ASSERT_EQUAL_FLOAT(1e9f, s.batt_min);

    // Ambient now comes from 0x21E byte 3 with a -40 offset. This fixture
    // carries 0x49 throughout, and the dash read 33 C. 0x372 is a flag, not a
    // temperature, and must no longer be able to overwrite this.
    TEST_ASSERT_EQUAL_INT(33, s.ambient_min);
    TEST_ASSERT_EQUAL_INT(33, s.ambient_max);

    // Engine was revved but the car never moved.
    TEST_ASSERT_TRUE(s.rpm_max > 4500 && s.rpm_max < 5500);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, s.wheel_max[i]);
    }
}

// =========================================================================
// Regressions for the specific bugs found
// =========================================================================
// =========================================================================
// This car: the 2026-08-29 wide-open-throttle capture
// =========================================================================
static void test_wot_pedal_scale(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic9_user_2014_wot.txt", &s, false));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);

    printf("\n  [this car WOT] rpm %u-%u  throttle max %u%%  ambient %d-%d C\n",
           s.rpm_min, s.rpm_max, s.throttle_max, s.ambient_min, s.ambient_max);

    // Two full-throttle pulls into the limiter, in Park.
    TEST_ASSERT_TRUE_MESSAGE(s.rpm_max > 5000 && s.rpm_max < 5600,
                             "capture should reach the rev limiter");

    // The whole point of the fixture. Raw 0x17C b0 plateaus at 211-213 here,
    // so on a 0-255 scale the pedal reads 83-84 % - the kickdown detent, with
    // the remaining travel only reachable by pushing through it (the road log
    // gets to 255). The retired 139.0f scale turned this into a clipped 100 %,
    // and would have done so from raw 139 upward - just 55 % of real travel.
    TEST_ASSERT_TRUE_MESSAGE(s.throttle_max >= 80 && s.throttle_max <= 88,
                             "WOT should read low-80s %, not clip at 100");

    // Ambient from 0x21E byte 3: the dash read 33 C that day.
    TEST_ASSERT_EQUAL_INT(33, s.ambient_max);

    // 0x324 byte 1 is a TRIP AVERAGE, not an instantaneous reading. Two pulls
    // to the rev limiter in this very capture move it by a single count. If a
    // future change ever makes this byte swing with throttle, it has stopped
    // being decoded as what it is - see CAN_PROTOCOL_MAP.md section H.
    TEST_ASSERT_TRUE_MESSAGE(s.fuel_max - s.fuel_min <= 2.0f,
                             "0x324 b1 must barely move under WOT - it is an average");
    TEST_ASSERT_TRUE_MESSAGE(s.fuel_min >= 85.0f && s.fuel_max <= 95.0f,
                             "raw byte is the value, x10 L/100km - no km/L conversion");

    // Stationary throughout - the car never left Park.
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, s.wheel_max[i]);
    }
}

// =========================================================================
// Fuel tank level - the two ends of the gauge, from real captures
// =========================================================================
static void test_fuel_level_refuel(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic9_user_2014_refuel.txt", &s, false));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);
    printf("\n  [refuel] fuel level swept %d%% -> %d%%\n", s.fuel_lvl_min, s.fuel_lvl_max);

    // Caught mid-fill: 0x1A6 b3 runs 40 -> 105 raw, i.e. 38% -> 100%.
    TEST_ASSERT_TRUE_MESSAGE(s.fuel_lvl_min >= 35 && s.fuel_lvl_min <= 42,
                             "refuel should start around 38%");
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, s.fuel_lvl_max,
                             "a full tank must read exactly 100% - check FUEL_LEVEL_FULL_RAW");
    // The lamp was already out at 38%; it must not be asserted anywhere here.
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s.low_fuel_true,
                             "low-fuel lamp must be off above the warning threshold");
}

static void test_fuel_level_low_and_lamp(void) {
    ReplayStats s;
    TEST_ASSERT_TRUE(replay("civic9_user_2014_lowfuel.txt", &s, false));
    TEST_ASSERT_EQUAL_UINT32(0, s.rejects);
    printf("  [low fuel] level %d-%d%%, lamp asserted on %d/%d frames of 0x294\n",
           s.fuel_lvl_min, s.fuel_lvl_max, s.low_fuel_true, s.low_fuel_n);

    // Dash had the low-fuel lamp lit and showed 39-40 km to empty.
    TEST_ASSERT_TRUE_MESSAGE(s.fuel_lvl_max <= 16, "near-empty tank must read low");
    TEST_ASSERT_TRUE_MESSAGE(s.fuel_lvl_min >= 8, "...but not zero - the lamp lights with reserve left");
    TEST_ASSERT_TRUE_MESSAGE(s.low_fuel_n > 0, "fixture must contain 0x294");
    TEST_ASSERT_EQUAL_INT_MESSAGE(s.low_fuel_n, s.low_fuel_true,
                             "the lamp was lit for this entire capture");
}

// =========================================================================
// The signal catalog: screens are laid out from this table, so a row that
// does not line up with its enum silently mislabels a gauge.
// =========================================================================
static void test_signal_catalog_is_consistent(void) {
    EspDashTelemetry t;
    memset(&t, 0, sizeof(t));
    const uint16_t plen = (uint16_t)sizeof(t);

    for (int i = 0; i < ESPDASH_SIG_COUNT; i++) {
        const EspDashSignalInfo *si = espdash_signal_info((EspDashSignalId)i);
        TEST_ASSERT_NOT_NULL(si);
        TEST_ASSERT_NOT_NULL(si->key);
        TEST_ASSERT_NOT_NULL(si->label);
        TEST_ASSERT_NOT_NULL(si->unit);
        TEST_ASSERT_TRUE_MESSAGE(si->key[0] != '\0', "every signal needs a key");
        TEST_ASSERT_TRUE_MESSAGE(si->max_x10 > si->min_x10, "gauge range must be non-empty");
        TEST_ASSERT_TRUE_MESSAGE(si->decimals <= 1, "only 0 or 1 decimals are formatted");
        if (si->kind == ESPDASH_KIND_ENUM) {
            TEST_ASSERT_NOT_NULL_MESSAGE(si->labels, "enum signal needs labels");
            TEST_ASSERT_TRUE(si->label_count > 0);
        }
        // Lookup by key must land back on the same id, or a layout driven by
        // strings would silently render a different signal.
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, (int)espdash_signal_by_key(si->key),
                                      "key lookup must round-trip");
        // Must never write past the buffer or leave it unterminated.
        char buf[8];
        int n = espdash_signal_format(&t, plen, (EspDashSignalId)i, 0xFFFF, buf, sizeof(buf));
        TEST_ASSERT_TRUE(n >= 0);
        TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
    }
    // Unknown keys must be rejected, not fall through to signal 0.
    TEST_ASSERT_EQUAL_INT((int)ESPDASH_SIG_COUNT, (int)espdash_signal_by_key("nope"));
    TEST_ASSERT_EQUAL_INT((int)ESPDASH_SIG_COUNT, (int)espdash_signal_by_key(NULL));
}

static void test_signal_catalog_gates_on_payload_len(void) {
    // A node built against this catalog must show "--" for a field an older
    // gateway does not send - never a zero that reads as a real measurement.
    EspDashTelemetry t;
    memset(&t, 0, sizeof(t));
    t.fuel_level_pct = 42;
    t.flags2 = ESPDASH_FLAG2_FUEL_VALID;
    char buf[16];
    bool ok = false;

    espdash_signal_x10(&t, (uint16_t)sizeof(t), ESPDASH_SIG_FUEL_LEVEL, 0xFFFF, &ok);
    TEST_ASSERT_TRUE_MESSAGE(ok, "a full-length packet carries fuel level");

    // A v2.1 sender: 30 bytes, no fuel_level_pct at all.
    espdash_signal_format(&t, 30, ESPDASH_SIG_FUEL_LEVEL, 0xFFFF, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("--", buf, "old sender must render as unknown");
    espdash_signal_format(&t, 30, ESPDASH_SIG_SPEED, 0xFFFF, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0", buf, "...but v2.0 fields still work");

    // FUEL_VALID clear means "no reading", even at full length.
    t.flags2 = 0;
    espdash_signal_format(&t, (uint16_t)sizeof(t), ESPDASH_SIG_FUEL_LEVEL, 0xFFFF, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("--", buf, "invalid reading must not render as 0%");
}

static void test_odometer_delta_survives_wrap(void) {
    // The wire counter is 16-bit and wraps every 3276 km. A trip spanning the
    // rollover must still report forward progress, not 3276 km backwards.
    EspDashTelemetry t;
    memset(&t, 0, sizeof(t));
    t.flags2 = ESPDASH_FLAG2_ODO_VALID;
    bool ok = false;

    t.odo_50m = 65500;                       // 20 counts (1.0 km) before the wrap
    int32_t v = espdash_signal_x10(&t, (uint16_t)sizeof(t), ESPDASH_SIG_ODO_KM, 65500, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(0, v);

    t.odo_50m = 16;                          // wrapped: 52 counts on = 2.6 km
    v = espdash_signal_x10(&t, (uint16_t)sizeof(t), ESPDASH_SIG_ODO_KM, 65500, &ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(26, v, "2.6 km across the 16-bit rollover");
}

static void test_brake_is_16_bit(void) {
    // Reading byte 1 alone showed ~38% brake at rest and wrapped to 0 once the
    // raw value passed 255. Rest is raw ~100; hard braking reaches ~526.
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x1A4; f.dlc = 8;

    // At rest: raw 0x0064 = 100 -> ~0 %
    f.data[0] = 0x00; f.data[1] = 0x64;
    f.data[7] = honda_checksum(0x1A4, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_TRUE_MESSAGE(st.brake_pct <= 2, "brake should read ~0 at rest");

    // Hard braking: raw 0x020E = 526. Byte 1 alone is 0x0E = 14, which the old
    // formula turned into 0 %.
    f.data[0] = 0x02; f.data[1] = 0x0E;
    f.data[7] = honda_checksum(0x1A4, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 20));
    printf("\n  [brake] raw 526 -> %u%%\n", st.brake_pct);
    TEST_ASSERT_TRUE_MESSAGE(st.brake_pct > 80, "16-bit brake field not being read");
}

static void test_steering_sign_and_scale(void) {
    // Sign confirmed by an on-car test (2026-08-09): raw positive is a
    // rightward turn. opendbc's documented -0.1 factor does NOT carry over to
    // this chassis and was only ever checked here by magnitude, not direction
    // - a reminder that a reference trace proves magnitude, not sign.
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x156; f.dlc = 6;

    // raw +1000 -> +100.0 deg
    f.data[0] = 0x03; f.data[1] = 0xE8;
    f.data[5] = honda_checksum(0x156, f.data, 6);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_EQUAL_INT16(100, st.steering_deg);

    // raw -1000 -> -100.0 deg
    f.data[0] = 0xFC; f.data[1] = 0x18;
    f.data[5] = honda_checksum(0x156, f.data, 6);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 20));
    TEST_ASSERT_EQUAL_INT16(-100, st.steering_deg);
}

static void test_0x372_cannot_set_ambient(void) {
    // 0x372 byte 0 only ever takes {0, 32}: it is a flag, not a temperature.
    // It used to overwrite the real reading from 0x21E.
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));

    f.id = 0x21E; f.dlc = 7;
    f.data[3] = 42;                                 // 42 - 40 = 2 C
    f.data[4] = 0x60;                               // byte 4 is a bitfield now
    f.data[6] = honda_checksum(0x21E, f.data, 7);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_EQUAL_INT8_MESSAGE(2, st.ambient_temp,
                                   "ambient must come from byte 3, not byte 4");

    memset(&f, 0, sizeof(f));
    f.id = 0x372; f.dlc = 2;
    f.data[0] = 32;
    f.data[1] = honda_checksum(0x372, f.data, 2);
    can_decode_frame(&st, &f, 20);
    TEST_ASSERT_EQUAL_INT8_MESSAGE(2, st.ambient_temp, "0x372 must not touch ambient");
}

static void test_0x1A0_is_not_decoded(void) {
    // 0x1A0 does not exist on a 9th gen bus; it must not drive ABS/TC.
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x1A0; f.dlc = 8;
    f.data[0] = 0x06; f.data[1] = 0x08;
    f.data[7] = honda_checksum(0x1A0, f.data, 8);
    TEST_ASSERT_FALSE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_FALSE(st.abs_active);
    TEST_ASSERT_FALSE(st.tc_active);
}

static void test_staleness(void) {
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x17C; f.dlc = 8;
    f.data[2] = 0x02; f.data[3] = 0xCB;             // 715 rpm
    f.data[7] = honda_checksum(0x17C, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 1000));
    TEST_ASSERT_EQUAL_UINT16(715, st.rpm);

    TEST_ASSERT_FALSE(can_decode_is_stale(&st, SIG_RPM, 1500, 2000));
    TEST_ASSERT_TRUE(can_decode_is_stale(&st, SIG_RPM, 4000, 2000));
    // A signal never seen is stale from the start.
    TEST_ASSERT_TRUE(can_decode_is_stale(&st, SIG_FUEL_CONSUMPTION, 1000, 2000));
}

// Corrected by an on-car road test (2026-08-09): every position was off by
// one against the physical selector. This is the only signal with no
// external reference at all - the 2015 Si trace is a manual gearbox and
// never carries 0x188 - so a driving car is the only oracle for it.
static void test_gear_mapping(void) {
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x188; f.dlc = 6;

    struct { uint8_t raw; uint8_t expect; const char *label; } cases[] = {
        {0x01, 0, "P"}, {0x02, 1, "R"}, {0x04, 2, "N"}, {0x08, 3, "D"}, {0x00, 4, "S"},
    };
    for (auto &c : cases) {
        memset(f.data, 0, 8);
        f.data[3] = c.raw;
        f.data[5] = honda_checksum(0x188, f.data, 6);
        TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.expect, st.gear, c.label);
    }
}

static void test_flags(void) {
    CanDecodeState st;
    can_decode_init(&st);

    st.rpm = 300;
    TEST_ASSERT_EQUAL_UINT8(0x00, can_decode_flags(&st) & 0x01);
    st.rpm = 800;
    TEST_ASSERT_EQUAL_UINT8(0x01, can_decode_flags(&st) & 0x01);
    st.rpm = 7000;
    TEST_ASSERT_EQUAL_UINT8(0x02, can_decode_flags(&st) & 0x02);
    st.water_temp_x10 = 1100;
    TEST_ASSERT_EQUAL_UINT8(0x04, can_decode_flags(&st) & 0x04);

    // The VSA bit used to be cleared by a mask running once per received frame,
    // immediately after being set, so it could never be observed.
    st.esp_disabled = true;
    TEST_ASSERT_EQUAL_UINT8(0x80, can_decode_flags(&st) & 0x80);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_honda_checksum);
    RUN_TEST(test_checksum_gates_decode);
    RUN_TEST(test_wheel_speeds_8th_gen_10kmh);
    RUN_TEST(test_9th_gen_si_drive);
    RUN_TEST(test_user_2014_capture_regression);
    RUN_TEST(test_wot_pedal_scale);
    RUN_TEST(test_fuel_level_refuel);
    RUN_TEST(test_fuel_level_low_and_lamp);
    RUN_TEST(test_signal_catalog_is_consistent);
    RUN_TEST(test_signal_catalog_gates_on_payload_len);
    RUN_TEST(test_odometer_delta_survives_wrap);
    RUN_TEST(test_brake_is_16_bit);
    RUN_TEST(test_steering_sign_and_scale);
    RUN_TEST(test_0x372_cannot_set_ambient);
    RUN_TEST(test_0x1A0_is_not_decoded);
    RUN_TEST(test_staleness);
    RUN_TEST(test_gear_mapping);
    RUN_TEST(test_flags);
    return UNITY_END();
}
