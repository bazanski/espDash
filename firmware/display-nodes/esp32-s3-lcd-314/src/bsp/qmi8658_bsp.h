#ifndef QMI8658_BSP_H
#define QMI8658_BSP_H

#include <Arduino.h>
#include <Wire.h>

// Axis Orientation & Inversion Configuration Flags
#define INVERT_LAT_G   false  // False: Left tilt moves dot Left
#define INVERT_LONG_G  false  // False: Forward tilt / brake moves dot Down, Accel moves dot Up
#define SWAP_XY_AXES   false  // False: X = Lateral, Y = Longitudinal

struct GForceData {
    float lat_g = 0.0f;       // Lateral G (Left: +, Right: -)
    float long_g = 0.0f;      // Longitudinal G (Accel: +, Brake: -)
    float total_g = 0.0f;     // Combined G vector magnitude
    float peak_g = 0.0f;      // Session peak combined G
    float peak_lat_g = 0.0f;  // Session peak lateral G
    float peak_long_g = 0.0f; // Session peak longitudinal G
    bool valid = false;
};

// Initialize QMI8658 IMU on I2C (SDA=15, SCL=7)
bool qmi8658_init();

// Read current filtered G-Force data
GForceData qmi8658_read_gforce();

// Reset/zero tare calibration offset based on current stationary position
void qmi8658_auto_calibrate();

#endif // QMI8658_BSP_H
