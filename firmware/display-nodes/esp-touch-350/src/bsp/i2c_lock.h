#pragma once
// The touch controller and the IMU share one I2C bus (IO38 SDA / IO39 SCL).
// The IMU is sampled from its own task, so every Wire transaction (the whole
// beginTransmission..requestFrom sequence) must hold this lock.
#include <Arduino.h>
#include <freertos/semphr.h>

static inline SemaphoreHandle_t i2c_mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}
static inline void i2c_lock()   { xSemaphoreTake(i2c_mutex(), portMAX_DELAY); }
static inline void i2c_unlock() { xSemaphoreGive(i2c_mutex()); }
