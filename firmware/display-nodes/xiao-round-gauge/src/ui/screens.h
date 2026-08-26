#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *battery_voltage_value;
    lv_obj_t *rpm_level_arc;
    lv_obj_t *brake_pressure_arc;
    lv_obj_t *gas_pedal_arc;
    lv_obj_t *gas_pedal_value;
    lv_obj_t *brake_pressure_value;
    lv_obj_t *speed_value;
    lv_obj_t *speed_type_label;
    lv_obj_t *coolant_temp_value;
    lv_obj_t *fuel_level_value;
    lv_obj_t *gear_value;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/