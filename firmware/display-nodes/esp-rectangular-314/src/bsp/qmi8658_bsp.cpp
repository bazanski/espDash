#include "qmi8658_bsp.h"
#include "user_config.h"

#define QMI8658_I2C_ADDR 0x6B

// QMI8658 Registers
#define QMI8658_WHO_AM_I 0x00
#define QMI8658_CTRL1    0x02
#define QMI8658_CTRL2    0x03
#define QMI8658_CTRL7    0x08
#define QMI8658_AX_L     0x35

static bool is_qmi_initialized = false;
static float offset_x = 0.0f;
static float offset_y = 0.0f;
static float offset_z = 0.0f;

static GForceData current_data;
static float filtered_lat = 0.0f;
static float filtered_long = 0.0f;

static uint8_t read_reg(uint8_t reg) {
    Wire.beginTransmission(QMI8658_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    Wire.requestFrom((uint8_t)QMI8658_I2C_ADDR, (size_t)1);
    if (Wire.available()) return Wire.read();
    return 0;
}

static bool write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(QMI8658_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

bool qmi8658_init() {
    Wire.begin((int)ESP32_SDA_NUM, (int)ESP32_SCL_NUM, 400000);
    delay(10);

    uint8_t chip_id = read_reg(QMI8658_WHO_AM_I);
    Serial.printf("[QMI8658] WHO_AM_I: 0x%02X\n", chip_id);

    if (chip_id != 0x05) {
        Serial.println("[QMI8658] Warning: Chip ID mismatch (expected 0x05).");
    }

    // Configure QMI8658:
    // CTRL1: 0x60 -> Auto-increment register address enabled
    write_reg(QMI8658_CTRL1, 0x60);
    
    // CTRL2: 0x23 -> Accel 4G range (8192 LSB/g), 250Hz ODR
    write_reg(QMI8658_CTRL2, 0x23);

    // CTRL7: 0x01 -> Enable Accelerometer
    write_reg(QMI8658_CTRL7, 0x01);

    delay(20);
    is_qmi_initialized = true;

    // Auto-zero tare calibration on startup
    qmi8658_auto_calibrate();
    return true;
}

void qmi8658_auto_calibrate() {
    if (!is_qmi_initialized) return;

    Serial.println("[QMI8658] Performing auto-zero tare calibration...");
    float sum_x = 0.0f, sum_y = 0.0f, sum_z = 0.0f;
    int samples = 50;

    for (int i = 0; i < samples; i++) {
        Wire.beginTransmission(QMI8658_I2C_ADDR);
        Wire.write(QMI8658_AX_L);
        if (Wire.endTransmission(false) == 0) {
            Wire.requestFrom((uint8_t)QMI8658_I2C_ADDR, (size_t)6);
            if (Wire.available() >= 6) {
                int16_t rx = (int16_t)(Wire.read() | (Wire.read() << 8));
                int16_t ry = (int16_t)(Wire.read() | (Wire.read() << 8));
                int16_t rz = (int16_t)(Wire.read() | (Wire.read() << 8));

                sum_x += (float)rx / 8192.0f;
                sum_y += (float)ry / 8192.0f;
                sum_z += (float)rz / 8192.0f;
            }
        }
        delay(5);
    }

    offset_x = sum_x / (float)samples;
    offset_y = sum_y / (float)samples;
    offset_z = sum_z / (float)samples;

    // Reset session peak readings on recalibration
    current_data.peak_g = 0.0f;
    current_data.peak_lat_g = 0.0f;
    current_data.peak_long_g = 0.0f;

    Serial.printf("[QMI8658] Calibration Offsets -> X: %.3fg, Y: %.3fg, Z: %.3fg\n", offset_x, offset_y, offset_z);
}

GForceData qmi8658_read_gforce() {
    if (!is_qmi_initialized) {
        current_data.valid = false;
        return current_data;
    }

    Wire.beginTransmission(QMI8658_I2C_ADDR);
    Wire.write(QMI8658_AX_L);
    if (Wire.endTransmission(false) != 0) {
        current_data.valid = false;
        return current_data;
    }

    Wire.requestFrom((uint8_t)QMI8658_I2C_ADDR, (size_t)6);
    if (Wire.available() < 6) {
        current_data.valid = false;
        return current_data;
    }

    int16_t raw_x = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t raw_y = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t raw_z = (int16_t)(Wire.read() | (Wire.read() << 8));

    // Convert raw 4G values (8192 LSB/g)
    float cur_x = (float)raw_x / 8192.0f;
    float cur_y = (float)raw_y / 8192.0f;
    float cur_z = (float)raw_z / 8192.0f;

    // Dynamic acceleration vector (subtracting resting gravity baseline)
    float dx = cur_x - offset_x;
    float dy = cur_y - offset_y;
    float dz = cur_z - offset_z;

    // Lateral G (Width axis of display: Left +, Right -)
    float lat_val = dx;

    // 3D Vector Gravity Projection for Longitudinal G (Forward Accel +, Brake -)
    // Works at ANY arbitrary display mounting pitch angle (Vertical, Angled, Flat)
    float g_norm = sqrtf(offset_x * offset_x + offset_y * offset_y + offset_z * offset_z);
    if (g_norm < 0.1f) g_norm = 1.0f;

    float gy_unit = offset_y / g_norm;
    float gz_unit = offset_z / g_norm;

    // Forward vehicle unit vector orthogonal to Earth's gravity in Y-Z plane
    float fwd_y = -gz_unit;
    float fwd_z = gy_unit;
    float fwd_norm = sqrtf(fwd_y * fwd_y + fwd_z * fwd_z);
    if (fwd_norm < 0.01f) {
        fwd_y = 1.0f;
        fwd_z = 0.0f;
    } else {
        fwd_y /= fwd_norm;
        fwd_z /= fwd_norm;
    }

    // Projected longitudinal G value along vehicle forward axis
    float long_val = (dy * fwd_y) + (dz * fwd_z);

#if SWAP_XY_AXES
    float tmp = lat_val;
    lat_val = long_val;
    long_val = tmp;
#endif

#if INVERT_LAT_G
    lat_val = -lat_val;
#endif

#if INVERT_LONG_G
    long_val = -long_val;
#endif

    // Exponential Moving Average (EMA) low-pass filter (alpha = 0.25)
    float alpha = 0.25f;
    filtered_lat = (alpha * lat_val) + ((1.0f - alpha) * filtered_lat);
    filtered_long = (alpha * long_val) + ((1.0f - alpha) * filtered_long);

    // Dead-zone noise filter (suppress engine vibration noise < 0.02g at rest)
    float out_lat = filtered_lat;
    float out_long = filtered_long;
    if (fabsf(out_lat) < 0.02f) out_lat = 0.0f;
    if (fabsf(out_long) < 0.02f) out_long = 0.0f;

    current_data.lat_g = out_lat;
    current_data.long_g = out_long;
    current_data.total_g = sqrtf(out_lat * out_lat + out_long * out_long);

    // Update peak hold records
    if (current_data.total_g > current_data.peak_g) {
        current_data.peak_g = current_data.total_g;
    }
    if (fabsf(current_data.lat_g) > fabsf(current_data.peak_lat_g)) {
        current_data.peak_lat_g = current_data.lat_g;
    }
    if (fabsf(current_data.long_g) > fabsf(current_data.peak_long_g)) {
        current_data.peak_long_g = current_data.long_g;
    }

    current_data.valid = true;
    return current_data;
}
