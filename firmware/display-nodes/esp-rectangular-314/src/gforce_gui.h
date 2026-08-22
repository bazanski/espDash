#ifndef GFORCE_GUI_H
#define GFORCE_GUI_H

#include "lvgl.h"
#include "bsp/qmi8658_bsp.h"

// Initialize and build LVGL widgets for the G-Force Meter screen (820x320 landscape)
lv_obj_t* create_gforce_gui();

// Update G-Force Meter screen widgets with new IMU data
void update_gforce_gui(const GForceData& gdata);

#endif // GFORCE_GUI_H
