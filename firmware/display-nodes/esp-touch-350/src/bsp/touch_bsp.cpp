#include "touch_bsp.h"
#include "board_config.h"
#include "display_bsp.h"
#include <Wire.h>
#include "i2c_lock.h"

#define LANDSCAPE_WIDTH  480
#define LANDSCAPE_HEIGHT 320

#define TOUCH_REG_INFO        0x0010
#define TOUCH_REG_MAX_TOUCHES 0x0009
#define TOUCH_REG_POINT0      0x0014

static bool touch_initialized = false;
static uint8_t max_points = 1;
static int16_t last_touch_x = 0;
static int16_t last_touch_y = 0;

static bool touch_i2c_read_locked(uint16_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    size_t count = Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)len);
    if (count != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
    return true;
}

static bool touch_i2c_read(uint16_t reg, uint8_t *buf, size_t len) {
    i2c_lock();
    bool ok = touch_i2c_read_locked(reg, buf, len);
    i2c_unlock();
    return ok;
}

void touch_init() {
    pinMode(TOUCH_PIN_RST, OUTPUT);
    digitalWrite(TOUCH_PIN_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_PIN_RST, HIGH);
    delay(100);

    Wire.begin(TOUCH_PIN_SDA, TOUCH_PIN_SCL, 400000);

    uint8_t pts = 0;
    if (touch_i2c_read(TOUCH_REG_MAX_TOUCHES, &pts, 1) && pts > 0 && pts <= 5) {
        max_points = pts;
        Serial.printf("[Touch] ST77922 touch detected, max points: %d\n", max_points);
        touch_initialized = true;
    } else {
        Serial.println("[Touch] Warning: ST77922 touch controller didn't report max points");
        touch_initialized = true; // Still allow attempts
    }
}

bool touch_read_point(int16_t *out_x, int16_t *out_y) {
    if (!touch_initialized) return false;

    uint8_t info = 0;
    if (!touch_i2c_read(TOUCH_REG_INFO, &info, 1)) return false;

    if (info & 0x08) {
        uint8_t buf[7 * 5] = {0};
        size_t read_bytes = 7 * max_points;
        if (read_bytes > sizeof(buf)) read_bytes = sizeof(buf);

        if (touch_i2c_read(TOUCH_REG_POINT0, buf, read_bytes)) {
            for (int i = 0; i < max_points; i++) {
                if (buf[i * 7] & 0x80) {
                    int16_t raw_x = ((buf[i * 7] & 0x3F) << 8) | buf[i * 7 + 1];
                    int16_t raw_y = ((buf[i * 7 + 2] & 0x3F) << 8) | buf[i * 7 + 3];

                    // Bound to screen limits
                    if (raw_x < 0) raw_x = 0;
                    if (raw_x >= LCD_WIDTH) raw_x = LCD_WIDTH - 1;
                    if (raw_y < 0) raw_y = 0;
                    if (raw_y >= LCD_HEIGHT) raw_y = LCD_HEIGHT - 1;

                    *out_x = raw_x;
                    *out_y = raw_y;
                    return true;
                }
            }
        }
    }
    return false;
}

void touch_lvgl_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int16_t raw_x, raw_y;
    if (touch_read_point(&raw_x, &raw_y)) {
        int16_t lx, ly;
        if (display_is_rotation_inverted()) {
            lx = (LANDSCAPE_WIDTH - 1) - raw_y;
            ly = raw_x;
        } else {
            lx = raw_y;
            ly = (LANDSCAPE_HEIGHT - 1) - raw_x;
        }

        if (lx < 0) lx = 0;
        if (lx >= LANDSCAPE_WIDTH) lx = LANDSCAPE_WIDTH - 1;
        if (ly < 0) ly = 0;
        if (ly >= LANDSCAPE_HEIGHT) ly = LANDSCAPE_HEIGHT - 1;

        data->point.x = lx;
        data->point.y = ly;
        data->state = LV_INDEV_STATE_PRESSED;
        last_touch_x = lx;
        last_touch_y = ly;
    } else {
        data->point.x = last_touch_x;
        data->point.y = last_touch_y;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
