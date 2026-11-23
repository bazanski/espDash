#pragma once

#include <Arduino.h>
#include <lvgl.h>

void touch_init();
bool touch_read_point(int16_t *x, int16_t *y);
void touch_lvgl_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
