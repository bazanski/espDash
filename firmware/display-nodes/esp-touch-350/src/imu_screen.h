#pragma once
#include <lvgl.h>
#include "bsp/imu_bsp.h"

// Builds the IMU page (separate LVGL screen). on_next is called when the user
// taps the NEXT button; the caller owns the actual screen switch.
lv_obj_t *imu_screen_build(lv_event_cb_t on_next, lv_event_cb_t on_rotate);

// Refreshes every value on the IMU page. link_text/link_color describe the
// gateway link so it stays visible while the IMU page is up.
void imu_screen_update(const ImuData &d, const char *link_text, uint32_t link_color);
