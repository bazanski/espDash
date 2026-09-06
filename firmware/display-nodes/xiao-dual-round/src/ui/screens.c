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

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 480);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_line_create(parent_obj);
            lv_obj_set_pos(obj, 0, 239);
            lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
            static lv_point_t line_points[] = {
                { 0, 0 },
                { 240, 0 }
            };
            lv_line_set_points(obj, line_points, 2);
            lv_obj_set_style_line_color(obj, lv_color_hex(0x000000), LV_PART_MAIN);
        }
        {
            // rpm_arc (Screen A outer arc, 220x220 centered at 120, 120)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.rpm_arc = obj;
            lv_obj_set_pos(obj, 10, 10);
            lv_obj_set_size(obj, 220, 220);
            lv_arc_set_range(obj, 0, 8000);
            lv_arc_set_value(obj, 0);
        }
        {
            // throttle_arc (Screen A middle arc, 180x180 centered at 120, 120)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.throttle_arc = obj;
            lv_obj_set_pos(obj, 30, 30);
            lv_obj_set_size(obj, 180, 180);
            lv_arc_set_range(obj, 0, 100);
            lv_arc_set_value(obj, 0);
        }
        {
            // eff_arc (Screen A inner arc, 140x140 centered at 120, 120)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.eff_arc = obj;
            lv_obj_set_pos(obj, 50, 50);
            lv_obj_set_size(obj, 140, 140);
            lv_arc_set_range(obj, 0, 20);
            lv_arc_set_value(obj, 0);
        }
        {
            // speed_value (Screen A center, full width centered)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.speed_value = obj;
            lv_obj_set_pos(obj, 0, 78);
            lv_obj_set_size(obj, 240, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_segment7_48, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // speed_label (Screen A "km/h", full width centered)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.speed_label = obj;
            lv_obj_set_pos(obj, 0, 122);
            lv_obj_set_size(obj, 240, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "km/h");
        }
        {
            // eff_value (Screen A metric 1 value: right edge 117)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.eff_value = obj;
            lv_obj_set_pos(obj, 10, 159);
            lv_obj_set_size(obj, 107, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e676), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0.0");
        }
        {
            // eff_label (Screen A metric 1 label: left edge 121, total row centered at ~118-120)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.eff_label = obj;
            lv_obj_set_pos(obj, 121, 159);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "l/100");
        }
        {
            // throttle_value (Screen A metric 2 value: right edge 122)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.throttle_value = obj;
            lv_obj_set_pos(obj, 10, 179);
            lv_obj_set_size(obj, 112, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // throttle_label (Screen A metric 2 label: left edge 126, total row centered at 119)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.throttle_label = obj;
            lv_obj_set_pos(obj, 126, 179);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "%");
        }
        {
            // rpm _value (Screen A metric 3 value: right edge 122)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.rpm__value = obj;
            lv_obj_set_pos(obj, 10, 197);
            lv_obj_set_size(obj, 112, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e676), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // rpm _label (Screen A metric 3 label: left edge 126, total row centered at 117-120)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.rpm__label = obj;
            lv_obj_set_pos(obj, 126, 197);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "rpm");
        }
        {
            // coolant_arc (Screen B outer arc, 220x220 centered at 120, 360)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.coolant_arc = obj;
            lv_obj_set_pos(obj, 10, 250);
            lv_obj_set_size(obj, 220, 220);
            lv_arc_set_range(obj, 40, 120);
            lv_arc_set_value(obj, 40);
        }
        {
            // brake_arc (Screen B middle arc, 180x180 centered at 120, 360)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.brake_arc = obj;
            lv_obj_set_pos(obj, 30, 270);
            lv_obj_set_size(obj, 180, 180);
            lv_arc_set_range(obj, 0, 100);
            lv_arc_set_value(obj, 0);
        }
        {
            // fuel_arc (Screen B inner arc, 140x140 centered at 120, 360)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.fuel_arc = obj;
            lv_obj_set_pos(obj, 50, 290);
            lv_obj_set_size(obj, 140, 140);
            lv_arc_set_range(obj, 0, 100);
            lv_arc_set_value(obj, 0);
        }
        {
            // left_distance_value (Screen B center, full width centered)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.left_distance_value = obj;
            lv_obj_set_pos(obj, 0, 318);
            lv_obj_set_size(obj, 240, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &ui_font_segment7_48, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // left_distance_label (Screen B "km", full width centered)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.left_distance_label = obj;
            lv_obj_set_pos(obj, 0, 362);
            lv_obj_set_size(obj, 240, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "km");
        }
        {
            // fuel_value (Screen B metric 1 value: right edge 122)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.fuel_value = obj;
            lv_obj_set_pos(obj, 10, 401);
            lv_obj_set_size(obj, 112, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e676), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // fuel_label (Screen B metric 1 label: left edge 126, total row centered at 119)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.fuel_label = obj;
            lv_obj_set_pos(obj, 126, 401);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "%");
        }
        {
            // brake_value (Screen B metric 2 value: right edge 122)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.brake_value = obj;
            lv_obj_set_pos(obj, 10, 420);
            lv_obj_set_size(obj, 112, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // brake_label (Screen B metric 2 label: left edge 126, total row centered at 119-124)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.brake_label = obj;
            lv_obj_set_pos(obj, 126, 420);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "%");
        }
        {
            // coolant_value (Screen B metric 3 value: right edge 120)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.coolant_value = obj;
            lv_obj_set_pos(obj, 10, 439);
            lv_obj_set_size(obj, 110, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x00e676), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "0");
        }
        {
            // coolant_label (Screen B metric 3 label: left edge 124, total row centered at 120)
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.coolant_label = obj;
            lv_obj_set_pos(obj, 124, 439);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0x6b7d96), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "°C");
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
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
    { "segment7_80_manual", &ui_font_segment7_80_manual },
    { "Segment7_28", &ui_font_segment7_28 },
    { "Segment7_48", &ui_font_segment7_48 },
    { "Segment7_80", &ui_font_segment7_80 },
    { "DSEG7_Classic_Bold_24", &ui_font_dseg7_classic_bold_24 },
    { "DSEG14_Classic_Bold_20", &ui_font_dseg14_classic_bold_20 },
    { "DSEG14_Classic_Bold_28", &ui_font_dseg14_classic_bold_28 },
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
}