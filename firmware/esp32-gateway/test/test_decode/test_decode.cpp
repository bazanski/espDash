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
    float fuel_avg_min, fuel_avg_max;
    float batt_min, batt_max;
    int   ambient_min, ambient_max;
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
    s->fuel_avg_min = 1e9f; s->fuel_avg_max = -1e9f;
    s->batt_min = 1e9f; s->batt_max = -1e9f;
    s->ambient_min = 127; s->ambient_max = -128;
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
            if (st.fuel_avg_x10 < out->fuel_avg_min) out->fuel_avg_min = st.fuel_avg_x10;
            if (st.fuel_avg_x10 > out->fuel_avg_max) out->fuel_avg_max = st.fuel_avg_x10;
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
    // This fixture is a stationary/idle capture (2026-08-08) where 0x324 byte 1
    // contains raw 101-102 (10.1-10.2 km/L). Converted to European average fuel
    // consumption (10000 / km_l_x10) this yields 98-99 (9.8-9.9 L/100km).
    TEST_ASSERT_TRUE(s.fuel_avg_min >= 95.0f && s.fuel_avg_max <= 105.0f);
    TEST_ASSERT_TRUE(s.batt_min >= 14.0f && s.batt_max <= 14.4f);

    // Ambient now comes from 0x21E byte 4 only. 0x372 is a flag, not a
    // temperature, and must no longer be able to overwrite this.
    TEST_ASSERT_EQUAL_INT(32, s.ambient_min);
    TEST_ASSERT_EQUAL_INT(32, s.ambient_max);

    // Engine was revved but the car never moved.
    TEST_ASSERT_TRUE(s.rpm_max > 4500 && s.rpm_max < 5500);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_FLOAT(0.0f, s.wheel_max[i]);
    }
}

// =========================================================================
// Regressions for the specific bugs found
// =========================================================================
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
    f.data[4] = 2;                                  // 2 C
    f.data[6] = honda_checksum(0x21E, f.data, 7);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_EQUAL_INT8(2, st.ambient_temp);

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

static void test_fuel_avg_conversion(void) {
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 0x324; f.dlc = 8;
    f.data[0] = 125; // 85 C

    // Case 1: 8.0 km/L (raw 80) -> 12.5 L/100km (125 x10)
    f.data[1] = 80;
    f.data[7] = honda_checksum(0x324, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 10));
    TEST_ASSERT_EQUAL_UINT8(80, st.raw_fuel_km_l_x10);
    TEST_ASSERT_EQUAL_UINT8(125, st.fuel_avg_x10);

    // Case 2: 7.5 km/L (raw 75) -> 13.3 L/100km (133 x10)
    f.data[1] = 75;
    f.data[7] = honda_checksum(0x324, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 20));
    TEST_ASSERT_EQUAL_UINT8(133, st.fuel_avg_x10);

    // Case 3: 10.0 km/L (raw 100) -> 10.0 L/100km (100 x10)
    f.data[1] = 100;
    f.data[7] = honda_checksum(0x324, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 30));
    TEST_ASSERT_EQUAL_UINT8(100, st.fuel_avg_x10);

    // Case 4: 12.5 km/L (raw 125) -> 8.0 L/100km (80 x10)
    f.data[1] = 125;
    f.data[7] = honda_checksum(0x324, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 40));
    TEST_ASSERT_EQUAL_UINT8(80, st.fuel_avg_x10);

    // Case 5: 23.8 km/L (raw 238) -> 4.2 L/100km (42 x10)
    f.data[1] = 238;
    f.data[7] = honda_checksum(0x324, f.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f, 50));
    TEST_ASSERT_EQUAL_UINT8(42, st.fuel_avg_x10);
}

static void test_fuel_instant_calculation(void) {
    CanDecodeState st;
    can_decode_init(&st);
    CanFrame f158, f17c;
    memset(&f158, 0, sizeof(f158));
    memset(&f17c, 0, sizeof(f17c));
    f158.id = 0x158; f158.dlc = 8;
    f17c.id = 0x17C; f17c.dlc = 8;

    // Case 1: Idle (750 RPM, 0% throttle, 0 km/h) -> ~8 (0.8 L/h)
    f17c.data[0] = 0;   // 0% throttle
    f17c.data[2] = (750 >> 8) & 0xFF; f17c.data[3] = 750 & 0xFF;
    f17c.data[7] = honda_checksum(0x17C, f17c.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f17c, 10));

    f158.data[0] = 0; f158.data[1] = 0; // 0 km/h
    f158.data[7] = honda_checksum(0x158, f158.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f158, 20));

    TEST_ASSERT_TRUE(st.fuel_instant_x10 >= 6 && st.fuel_instant_x10 <= 10);
    TEST_ASSERT_EQUAL_UINT8(st.fuel_instant_x10, st.fuel_consumption_x10);

    // Case 2: Cruising at 60 km/h (2000 RPM, 15% throttle) -> ~5.2 L/100km (52 x10)
    // throttle raw = 15% * 139 = 21
    f17c.data[0] = 21;
    f17c.data[2] = (2000 >> 8) & 0xFF; f17c.data[3] = 2000 & 0xFF;
    f17c.data[7] = honda_checksum(0x17C, f17c.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f17c, 30));

    // 60 km/h -> raw = 6000
    f158.data[0] = (6000 >> 8) & 0xFF; f158.data[1] = 6000 & 0xFF;
    f158.data[7] = honda_checksum(0x158, f158.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f158, 40));

    TEST_ASSERT_TRUE(st.fuel_instant_x10 >= 45 && st.fuel_instant_x10 <= 60);

    // Case 3: Acceleration at 80 km/h (3500 RPM, 60% throttle) -> ~15.3 L/100km (153 x10)
    // throttle raw = 60% * 139 = 83
    f17c.data[0] = 83;
    f17c.data[2] = (3500 >> 8) & 0xFF; f17c.data[3] = 3500 & 0xFF;
    f17c.data[7] = honda_checksum(0x17C, f17c.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f17c, 50));

    // 80 km/h -> raw = 8000
    f158.data[0] = (8000 >> 8) & 0xFF; f158.data[1] = 8000 & 0xFF;
    f158.data[7] = honda_checksum(0x158, f158.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f158, 60));

    TEST_ASSERT_TRUE(st.fuel_instant_x10 >= 140 && st.fuel_instant_x10 <= 170);

    // Case 4: DFCO on Deceleration (2500 RPM, 0% throttle, 70 km/h) -> 0 L/100km
    f17c.data[0] = 0;
    f17c.data[2] = (2500 >> 8) & 0xFF; f17c.data[3] = 2500 & 0xFF;
    f17c.data[7] = honda_checksum(0x17C, f17c.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f17c, 70));

    f158.data[0] = (7000 >> 8) & 0xFF; f158.data[1] = 7000 & 0xFF;
    f158.data[7] = honda_checksum(0x158, f158.data, 8);
    TEST_ASSERT_TRUE(can_decode_frame(&st, &f158, 80));

    TEST_ASSERT_EQUAL_UINT8(0, st.fuel_instant_x10);
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
    RUN_TEST(test_brake_is_16_bit);
    RUN_TEST(test_steering_sign_and_scale);
    RUN_TEST(test_0x372_cannot_set_ambient);
    RUN_TEST(test_0x1A0_is_not_decoded);
    RUN_TEST(test_staleness);
    RUN_TEST(test_gear_mapping);
    RUN_TEST(test_flags);
    RUN_TEST(test_fuel_avg_conversion);
    RUN_TEST(test_fuel_instant_calculation);
    return UNITY_END();
}
