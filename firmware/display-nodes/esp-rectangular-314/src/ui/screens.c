#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

screen_main_state_t screen_main_state;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_main() {
    screen_main_state_t *state = &screen_main_state;
    (void)state;
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 820, 320);
    {
        lv_obj_t *parent_obj = obj;
        {
            // left_tiers
            lv_obj_t *obj = lv_meter_create(parent_obj);
            objects.left_tiers = obj;
            lv_obj_set_pos(obj, 45, 428);
            lv_obj_set_size(obj, 300, 300);
            {
                lv_meter_scale_t *scale = lv_meter_add_scale(obj);
                state->lb_tier = scale;
                lv_meter_set_scale_ticks(obj, scale, 21, 1, 5, lv_color_hex(0xa0a0a0));
                lv_meter_set_scale_major_ticks(obj, scale, 5, 3, 10, lv_color_hex(0x000000), 10);
                lv_meter_set_scale_range(obj, scale, 0, 4, 140, -30);
                {
                    lv_meter_indicator_t *indicator = lv_meter_add_needle_line(obj, scale, 3, lv_color_hex(0x0030ff), -30);
                    state->lb_tier_value = indicator;
                    lv_meter_set_indicator_value(obj, indicator, 2.6);
                }
            }
            {
                lv_meter_scale_t *scale = lv_meter_add_scale(obj);
                state->lf_tier = scale;
                lv_meter_set_scale_ticks(obj, scale, 33, 1, 5, lv_color_hex(0xa0a0a0));
                lv_meter_set_scale_major_ticks(obj, scale, 8, 3, 10, lv_color_hex(0x000000), 10);
                lv_meter_set_scale_range(obj, scale, 0, 4, 140, 150);
                {
                    lv_meter_indicator_t *indicator = lv_meter_add_needle_line(obj, scale, 3, lv_color_hex(0x0030ff), -30);
                    state->lf_tier_value = indicator;
                    lv_meter_set_indicator_value(obj, indicator, 1.8);
                }
            }
        }
        {
            // history_chart
            lv_obj_t *obj = lv_chart_create(parent_obj);
            objects.history_chart = obj;
            lv_obj_set_pos(obj, 109, 32);
            lv_obj_set_size(obj, 634, 257);
        }
        {
            // throttle_bar
            lv_obj_t *obj = lv_bar_create(parent_obj);
            objects.throttle_bar = obj;
            lv_obj_set_pos(obj, 28, 32);
            lv_obj_set_size(obj, 10, 257);
            lv_bar_set_value(obj, 43, LV_ANIM_ON);
        }
        {
            // brake_bar
            lv_obj_t *obj = lv_bar_create(parent_obj);
            objects.brake_bar = obj;
            lv_obj_set_pos(obj, 55, 32);
            lv_obj_set_size(obj, 10, 257);
            lv_bar_set_value(obj, 15, LV_ANIM_ON);
        }
        {
            // rpm_bar
            lv_obj_t *obj = lv_bar_create(parent_obj);
            objects.rpm_bar = obj;
            lv_obj_set_pos(obj, 82, 32);
            lv_obj_set_size(obj, 10, 257);
            lv_bar_set_range(obj, 0, 7000);
            lv_bar_set_value(obj, 2500, LV_ANIM_ON);
        }
        {
            // status_label
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.status_label = obj;
            lv_obj_set_pos(obj, 115, 38);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "Recording is ON/OFF");
        }
        {
            // warning_label
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.warning_label = obj;
            lv_obj_set_pos(obj, 115, 60);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "Recording is ON/OFF");
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
    screen_main_state_t *state = &screen_main_state;
    (void)state;
}

void create_screen_tyres() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.tyres = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 820, 320);
    
    tick_screen_tyres();
}

void tick_screen_tyres() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
    tick_screen_tyres,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 2) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_main();
    create_screen_tyres();
}