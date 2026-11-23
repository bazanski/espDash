#pragma once
#include <Arduino.h>

// MPU-6500 / MPU-9250 (GY-6500 / GY-9250) on the shared touch I2C bus.
struct ImuData {
    bool     present;          // chip answered WHO_AM_I
    bool     has_mag;          // AK8963 magnetometer found (MPU-9250 only)
    bool     mag_calibrated;   // enough rotation seen to trust the compass
    bool     gyro_calibrating; // zeroing gyro bias - keep still
    char     chip[16];         // "MPU-9250", "MPU-6500", ...
    uint8_t  whoami;
    uint8_t  addr;
    float    ax, ay, az;       // accel, g (sensor axes)
    float    gx, gy, gz;       // gyro, deg/s (bias removed)
    float    mx, my, mz;       // mag, uT (calibrated, in accel/gyro axes)
    float    temp_c;
    float    roll, pitch, yaw; // deg, relative to the last ZERO
    float    heading;          // deg 0..360 (needs calibrated mag, else relative)
    float    lin_x, lin_y, lin_z; // acceleration with gravity removed, g
    float    amag;             // |accel|, g
    uint8_t  mag_progress;     // 0..100 % of the rotation needed to calibrate
    float    rate_hz;          // measured fusion rate
    uint32_t samples;
};

void imu_init();                 // starts the sampling task (call after touch_init)
void imu_get(ImuData *out);      // consistent snapshot
void imu_zero_attitude();        // treat the current pose as level / heading 0
void imu_recal_gyro();           // re-zero gyro bias (device must be still)
void imu_reset_mag_cal();        // restart magnetometer calibration
