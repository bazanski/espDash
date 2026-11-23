#include "imu_bsp.h"
#include "board_config.h"
#include "i2c_lock.h"
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

// ---- registers --------------------------------------------------------
#define R_SMPLRT_DIV   0x19
#define R_CONFIG       0x1A
#define R_GYRO_CFG     0x1B
#define R_ACCEL_CFG    0x1C
#define R_ACCEL_CFG2   0x1D
#define R_INT_PIN_CFG  0x37
#define R_ACCEL_XOUT   0x3B
#define R_PWR_MGMT_1   0x6B
#define R_WHO_AM_I     0x75

#define AK_ADDR        0x0C
#define AK_WIA         0x00
#define AK_ST1         0x02
#define AK_HXL         0x03
#define AK_CNTL1       0x0A
#define AK_ASAX        0x10

#define ACCEL_LSB_PER_G   8192.0f   // +-4 g
#define GYRO_LSB_PER_DPS  65.5f     // +-500 dps
#define MAG_UT_PER_LSB    0.15f     // AK8963, 16-bit
#define SAMPLE_PERIOD_MS  5         // 200 Hz
#define DEG2RAD 0.017453292f
#define RAD2DEG 57.29578f

// ---- mounting ------------------------------------------------------------
// Everything published by this module (accel, gyro, mag, roll/pitch/heading,
// linear accel) is in the VEHICLE frame: X forward, Y left, Z up. Say which
// sensor axis points along each vehicle axis, as {sensor axis 0..2 = X/Y/Z, sign}.
// Identity = the board lies flat, chip face up, its +X pointing at the front of
// the car. The mapping must be a proper rotation (right-handed).
#ifndef IMU_MOUNT_MAP
#define IMU_MOUNT_MAP { {0, +1}, {1, +1}, {2, +1} }   // {vehicle X, Y, Z <- sensor}
#endif
static const int8_t kMount[3][2] = IMU_MOUNT_MAP;
static void to_vehicle(const float in[3], float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = in[kMount[i][0]] * kMount[i][1];
}

static ImuData      g_pub;                     // published snapshot
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool req_zero = false, req_gyro = false, req_mag_reset = false;

// ---- low level I2C (callers hold the bus lock) -------------------------
static bool wr(uint8_t a, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(a);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}
static bool rd(uint8_t a, uint8_t reg, uint8_t *buf, size_t n) {
    Wire.beginTransmission(a);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(a, (uint8_t)n) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

// ---- driver / fusion state (task-local) --------------------------------
static uint8_t imu_addr = 0;
static bool    has_mag = false;
static float   asa[3] = {1, 1, 1};
static bool    temp_is_6050 = false;
static char    chip_name[16] = "";
static uint8_t whoami = 0;

static float q0 = 1, q1 = 0, q2 = 0, q3 = 0;
static float gbias[3] = {0, 0, 0};
static float roll0 = 0, pitch0 = 0, yaw0 = 0;
static float mmin[3], mmax[3];
static bool  mag_cal_ok = false;
static uint32_t cal_started_ms = 0;
static bool  gyro_cal_active = false;
static int   gyro_cal_n = 0;
static double gyro_cal_sum[3];
static Preferences prefs;
static uint32_t last_mag_save = 0;

static void mag_reset_ranges() {
    for (int i = 0; i < 3; i++) { mmin[i] = 1e9f; mmax[i] = -1e9f; }
    mag_cal_ok = false;
}

static void mag_load() {
    prefs.begin("imu", true);
    if (prefs.getBytesLength("mag") == sizeof(float) * 6) {
        float v[6];
        prefs.getBytes("mag", v, sizeof(v));
        for (int i = 0; i < 3; i++) { mmin[i] = v[i]; mmax[i] = v[3 + i]; }
        mag_cal_ok = true;
        for (int i = 0; i < 3; i++) if (mmax[i] - mmin[i] < 30.0f) mag_cal_ok = false;
        Serial.printf("[IMU] Loaded saved mag calibration (%s)\n", mag_cal_ok ? "valid" : "incomplete");
    }
    prefs.end();
}
static void mag_save() {
    float v[6];
    for (int i = 0; i < 3; i++) { v[i] = mmin[i]; v[3 + i] = mmax[i]; }
    prefs.begin("imu", false);
    prefs.putBytes("mag", v, sizeof(v));
    prefs.end();
}

static void scan_bus() {
    Serial.print("[IMU] I2C scan:");
    i2c_lock();
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    i2c_unlock();
    Serial.println();
}

static bool detect_and_configure() {
    uint8_t id = 0;
    uint8_t found = 0;
    i2c_lock();
    for (uint8_t a = 0x68; a <= 0x69 && !found; a++) {
        if (rd(a, R_WHO_AM_I, &id, 1)) { found = a; }
    }
    if (!found) { i2c_unlock(); return false; }
    imu_addr = found;
    whoami = id;

    // Reset, then wake with the gyro PLL as clock.
    wr(imu_addr, R_PWR_MGMT_1, 0x80);
    i2c_unlock(); delay(100); i2c_lock();
    wr(imu_addr, R_PWR_MGMT_1, 0x01);
    i2c_unlock(); delay(20); i2c_lock();

    wr(imu_addr, R_SMPLRT_DIV, 0x00);   // 1 kHz base; we poll at 200 Hz
    wr(imu_addr, R_CONFIG, 0x03);       // gyro DLPF 41 Hz
    wr(imu_addr, R_GYRO_CFG, 0x08);     // +-500 dps
    wr(imu_addr, R_ACCEL_CFG, 0x08);    // +-4 g
    wr(imu_addr, R_ACCEL_CFG2, 0x03);   // accel DLPF 44 Hz

    switch (id) {
        case 0x68: strcpy(chip_name, "MPU-6050"); temp_is_6050 = true; break;
        case 0x70: strcpy(chip_name, "MPU-6500"); break;
        case 0x71: strcpy(chip_name, "MPU-9250"); break;
        case 0x73: strcpy(chip_name, "MPU-9255"); break;
        default:   snprintf(chip_name, sizeof(chip_name), "IMU 0x%02X", id); break;
    }

    // Magnetometer (AK8963) sits behind the IMU's bypass switch.
    has_mag = false;
    wr(imu_addr, R_INT_PIN_CFG, 0x02);
    i2c_unlock(); delay(10); i2c_lock();
    uint8_t wia = 0;
    if (rd(AK_ADDR, AK_WIA, &wia, 1) && wia == 0x48) {
        wr(AK_ADDR, AK_CNTL1, 0x00);                  // power down
        i2c_unlock(); delay(10); i2c_lock();
        wr(AK_ADDR, AK_CNTL1, 0x0F);                  // fuse ROM access
        i2c_unlock(); delay(10); i2c_lock();
        uint8_t a3[3];
        if (rd(AK_ADDR, AK_ASAX, a3, 3)) {
            for (int i = 0; i < 3; i++) asa[i] = ((float)a3[i] - 128.0f) / 256.0f + 1.0f;
        }
        wr(AK_ADDR, AK_CNTL1, 0x00);
        i2c_unlock(); delay(10); i2c_lock();
        wr(AK_ADDR, AK_CNTL1, 0x16);                  // 16-bit, 100 Hz continuous
        has_mag = true;
        if (id == 0x70) strcpy(chip_name, "MPU-6500+AK");
    }
    i2c_unlock();

    Serial.printf("[IMU] Found %s at 0x%02X (WHO_AM_I=0x%02X), magnetometer: %s\n",
                  chip_name, imu_addr, whoami, has_mag ? "AK8963 yes" : "none");
    return true;
}

// Mahony AHRS (x-io reference). Gyro in rad/s, accel/mag any units.
static void mahony(float gx, float gy, float gz, float ax, float ay, float az,
                   float mx, float my, float mz, bool use_mag, float dt, float twoKp) {
    float n = sqrtf(ax * ax + ay * ay + az * az);
    if (n < 1e-6f) return;
    ax /= n; ay /= n; az /= n;

    float hex = 0, hey = 0, hez = 0;
    float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
    float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
    float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

    if (use_mag) {
        float mn = sqrtf(mx * mx + my * my + mz * mz);
        if (mn > 1e-6f) {
            mx /= mn; my /= mn; mz /= mn;
            float hx = 2.0f * (mx * (0.5f - q2q2 - q3q3) + my * (q1q2 - q0q3) + mz * (q1q3 + q0q2));
            float hy = 2.0f * (mx * (q1q2 + q0q3) + my * (0.5f - q1q1 - q3q3) + mz * (q2q3 - q0q1));
            float bx = sqrtf(hx * hx + hy * hy);
            float bz = 2.0f * (mx * (q1q3 - q0q2) + my * (q2q3 + q0q1) + mz * (0.5f - q1q1 - q2q2));
            float wx = 2.0f * (bx * (0.5f - q2q2 - q3q3) + bz * (q1q3 - q0q2));
            float wy = 2.0f * (bx * (q1q2 - q0q3) + bz * (q0q1 + q2q3));
            float wz = 2.0f * (bx * (q0q2 + q1q3) + bz * (0.5f - q1q1 - q2q2));
            hex = (my * wz - mz * wy);
            hey = (mz * wx - mx * wz);
            hez = (mx * wy - my * wx);
        }
    }

    float vx = 2.0f * (q1q3 - q0q2);
    float vy = 2.0f * (q0q1 + q2q3);
    float vz = 2.0f * (q0q0 - 0.5f + q3q3);
    hex += (ay * vz - az * vy);
    hey += (az * vx - ax * vz);
    hez += (ax * vy - ay * vx);

    gx += twoKp * 0.5f * hex;
    gy += twoKp * 0.5f * hey;
    gz += twoKp * 0.5f * hez;

    gx *= 0.5f * dt; gy *= 0.5f * dt; gz *= 0.5f * dt;
    float qa = q0, qb = q1, qc = q2;
    q0 += -qb * gx - qc * gy - q3 * gz;
    q1 += qa * gx + qc * gz - q3 * gy;
    q2 += qa * gy - qb * gz + q3 * gx;
    q3 += qa * gz + qb * gy - qc * gx;
    n = sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 /= n; q1 /= n; q2 /= n; q3 /= n;
}

static void imu_task(void *) {
    uint32_t last_us = 0, rate_t0 = millis(), rate_n = 0, fused_n = 0;
    float rate_hz = 0;
    bool present = false;
    bool scanned = false;
    uint32_t last_try = 0;
    float mcal[3] = {0, 0, 0};          // latest calibrated mag (accel frame)
    bool  mag_fresh = false;
    uint32_t warm_until = 0;
    uint32_t tick = 0;
    TickType_t wake = xTaskGetTickCount();

    mag_reset_ranges();
    mag_load();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        uint32_t now_ms = millis();

        if (!present) {
            if (now_ms - last_try >= 2000) {
                last_try = now_ms;
                if (!scanned) { scan_bus(); scanned = true; }
                if (detect_and_configure()) {
                    present = true;
                    q0 = 1; q1 = q2 = q3 = 0;
                    gyro_cal_active = true; gyro_cal_n = 0;
                    gyro_cal_sum[0] = gyro_cal_sum[1] = gyro_cal_sum[2] = 0;
                    warm_until = millis() + 3000;
                    last_us = micros();
                } else {
                    ImuData d; memset(&d, 0, sizeof(d));
                    portENTER_CRITICAL(&g_mux); g_pub = d; portEXIT_CRITICAL(&g_mux);
                }
            }
            continue;
        }

        // ---- read accel / temp / gyro in one burst ----
        uint8_t b[14];
        i2c_lock();
        bool ok = rd(imu_addr, R_ACCEL_XOUT, b, 14);
        i2c_unlock();
        if (!ok) {
            static uint8_t fails = 0;
            if (++fails > 20) { fails = 0; present = false; Serial.println("[IMU] Lost - retrying"); }
            continue;
        }
        auto s16 = [&](int i) { return (int16_t)((b[i] << 8) | b[i + 1]); };
        float ax = s16(0) / ACCEL_LSB_PER_G, ay = s16(2) / ACCEL_LSB_PER_G, az = s16(4) / ACCEL_LSB_PER_G;
        int16_t traw = s16(6);
        float temp_c = temp_is_6050 ? traw / 340.0f + 36.53f : traw / 333.87f + 21.0f;
        float gx = s16(8) / GYRO_LSB_PER_DPS, gy = s16(10) / GYRO_LSB_PER_DPS, gz = s16(12) / GYRO_LSB_PER_DPS;
        {   // sensor axes -> vehicle axes
            float a_s[3] = {ax, ay, az}, g_s[3] = {gx, gy, gz}, a_v[3], g_v[3];
            to_vehicle(a_s, a_v); to_vehicle(g_s, g_v);
            ax = a_v[0]; ay = a_v[1]; az = a_v[2];
            gx = g_v[0]; gy = g_v[1]; gz = g_v[2];
        }

        uint32_t now_us = micros();
        float dt = (now_us - last_us) * 1e-6f;
        last_us = now_us;
        if (dt <= 0 || dt > 0.1f) dt = SAMPLE_PERIOD_MS * 1e-3f;

        // ---- magnetometer at 100 Hz (every other cycle) ----
        if (has_mag && (tick++ & 1) == 0) {
            uint8_t st1 = 0, m[8];
            i2c_lock();
            bool r = rd(AK_ADDR, AK_ST1, &st1, 1);
            if (r && (st1 & 0x01)) r = rd(AK_ADDR, AK_HXL, m, 7);   // HXL..ST2
            else r = false;
            i2c_unlock();
            if (r && !(m[6] & 0x08)) {                              // no overflow
                float rx = (int16_t)(m[0] | (m[1] << 8)) * MAG_UT_PER_LSB * asa[0];
                float ry = (int16_t)(m[2] | (m[3] << 8)) * MAG_UT_PER_LSB * asa[1];
                float rz = (int16_t)(m[4] | (m[5] << 8)) * MAG_UT_PER_LSB * asa[2];
                // AK8963 axes -> accel/gyro axes: X<->Y swapped, Z flipped.
                float v[3] = {ry, rx, -rz};
                for (int i = 0; i < 3; i++) {
                    if (v[i] < mmin[i]) mmin[i] = v[i];
                    if (v[i] > mmax[i]) mmax[i] = v[i];
                }
                bool okspan = true;
                for (int i = 0; i < 3; i++) if (mmax[i] - mmin[i] < 30.0f) okspan = false;
                if (okspan && !mag_cal_ok) { mag_cal_ok = true; Serial.println("[IMU] Magnetometer calibrated"); mag_save(); last_mag_save = now_ms; }
                if (mag_cal_ok && now_ms - last_mag_save > 30000) { mag_save(); last_mag_save = now_ms; }
                for (int i = 0; i < 3; i++) mcal[i] = v[i] - (mmax[i] + mmin[i]) * 0.5f;
                if (mag_cal_ok) {   // simple soft-iron: normalise each half-span to the mean
                    float avg = ((mmax[0] - mmin[0]) + (mmax[1] - mmin[1]) + (mmax[2] - mmin[2])) / 6.0f;
                    for (int i = 0; i < 3; i++) {
                        float h = (mmax[i] - mmin[i]) * 0.5f;
                        if (h > 1.0f) mcal[i] *= avg / h;
                    }
                }
                mag_fresh = true;
            }
        }

        // ---- UI requests ----
        if (req_gyro) { req_gyro = false; gyro_cal_active = true; gyro_cal_n = 0; gyro_cal_sum[0] = gyro_cal_sum[1] = gyro_cal_sum[2] = 0; }
        if (req_mag_reset) { req_mag_reset = false; mag_reset_ranges(); mag_fresh = false; Serial.println("[IMU] Mag calibration restarted - rotate the unit through all orientations"); }

        // ---- gyro bias calibration (keep still ~1 s) ----
        if (gyro_cal_active) {
            float mag_g = sqrtf(gx * gx + gy * gy + gz * gz);
            if (mag_g > 15.0f) { gyro_cal_n = 0; gyro_cal_sum[0] = gyro_cal_sum[1] = gyro_cal_sum[2] = 0; }
            else { gyro_cal_sum[0] += gx; gyro_cal_sum[1] += gy; gyro_cal_sum[2] += gz; gyro_cal_n++; }
            if (gyro_cal_n >= 200) {
                for (int i = 0; i < 3; i++) gbias[i] = gyro_cal_sum[i] / gyro_cal_n;
                gyro_cal_active = false;
                Serial.printf("[IMU] Gyro bias set: %.2f %.2f %.2f dps\n", gbias[0], gbias[1], gbias[2]);
            }
        }
        gx -= gbias[0]; gy -= gbias[1]; gz -= gbias[2];

        // ---- fusion ----
        float mv[3];
        to_vehicle(mcal, mv);
        bool use_mag = has_mag && mag_cal_ok && mag_fresh;
        float kp = (now_ms < warm_until) ? 10.0f : 1.0f;     // converge fast at start
        mahony(gx * DEG2RAD, gy * DEG2RAD, gz * DEG2RAD, ax, ay, az,
               mv[0], mv[1], mv[2], use_mag, dt, kp);
        mag_fresh = false;

        float roll  = atan2f(q0 * q1 + q2 * q3, 0.5f - q1 * q1 - q2 * q2) * RAD2DEG;
        float pitch = asinf(fmaxf(-1.f, fminf(1.f, -2.0f * (q1 * q3 - q0 * q2)))) * RAD2DEG;
        float yaw   = atan2f(q1 * q2 + q0 * q3, 0.5f - q2 * q2 - q3 * q3) * RAD2DEG;

        if (req_zero) { req_zero = false; roll0 = roll; pitch0 = pitch; yaw0 = yaw; Serial.println("[IMU] Attitude zeroed"); }

        auto wrap180 = [](float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; };

        // gravity direction in the sensor frame, then dynamic acceleration
        float gvx = 2.0f * (q1 * q3 - q0 * q2);
        float gvy = 2.0f * (q0 * q1 + q2 * q3);
        float gvz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

        ImuData d;
        d.present = true;
        d.has_mag = has_mag;
        d.mag_calibrated = mag_cal_ok;
        d.gyro_calibrating = gyro_cal_active;
        strncpy(d.chip, chip_name, sizeof(d.chip)); d.chip[sizeof(d.chip) - 1] = 0;
        d.whoami = whoami; d.addr = imu_addr;
        d.ax = ax; d.ay = ay; d.az = az;
        d.gx = gx; d.gy = gy; d.gz = gz;
        d.mx = mv[0]; d.my = mv[1]; d.mz = mv[2];
        d.temp_c = temp_c;
        d.roll = wrap180(roll - roll0);
        d.pitch = wrap180(pitch - pitch0);
        d.yaw = wrap180(yaw - yaw0);
        // Compass heading when the magnetometer is trusted, otherwise a heading
        // relative to the last ZERO (gyro-integrated, drifts slowly).
        float hsrc = (mag_cal_ok && has_mag) ? yaw : (yaw - yaw0);
        float hd = fmodf(360.0f - hsrc, 360.0f); if (hd < 0) hd += 360.0f;
        d.heading = hd;
        d.lin_x = ax - gvx; d.lin_y = ay - gvy; d.lin_z = az - gvz;
        d.amag = sqrtf(ax * ax + ay * ay + az * az);
        float prog = 100.0f;
        if (has_mag && !mag_cal_ok) {
            prog = 0;
            for (int i = 0; i < 3; i++) {
                float sp = mmax[i] - mmin[i]; if (sp < 0) sp = 0;
                prog += fminf(sp / 30.0f, 1.0f) * 33.3f;
            }
        }
        d.mag_progress = (uint8_t)fminf(prog, 100.0f);
        fused_n++; rate_n++;
        if (now_ms - rate_t0 >= 1000) { rate_hz = rate_n * 1000.0f / (now_ms - rate_t0); rate_n = 0; rate_t0 = now_ms; }
        d.rate_hz = rate_hz;
        d.samples = fused_n;

        portENTER_CRITICAL(&g_mux);
        g_pub = d;
        portEXIT_CRITICAL(&g_mux);
    }
}

void imu_init() {
    memset(&g_pub, 0, sizeof(g_pub));
    xTaskCreatePinnedToCore(imu_task, "imu", 6144, NULL, 2, NULL, 0);
}

void imu_get(ImuData *out) {
    portENTER_CRITICAL(&g_mux);
    *out = g_pub;
    portEXIT_CRITICAL(&g_mux);
}
void imu_zero_attitude() { req_zero = true; }
void imu_recal_gyro()    { req_gyro = true; }
void imu_reset_mag_cal() { req_mag_reset = true; }
