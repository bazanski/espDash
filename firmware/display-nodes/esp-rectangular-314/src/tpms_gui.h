#ifndef TPMS_GUI_H
#define TPMS_GUI_H

#include "lvgl.h"
#include "tpms_parser.h"

// Build the 820x320 landscape TPMS screen
lv_obj_t* create_tpms_gui();

// Update TPMS screen widgets with latest readings
void update_tpms_gui();

#endif // TPMS_GUI_H
