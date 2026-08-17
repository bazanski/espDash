#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_segment7;
extern const lv_font_t ui_font_segment7_56;
extern const lv_font_t ui_font_segment7_64;
extern const lv_font_t ui_font_dseg_mini_light_20;
extern const lv_font_t ui_font_dseg_regular_20;
extern const lv_font_t ui_font_dseg_regular_60;
extern const lv_font_t ui_font_dseg_regular_46;
extern const lv_font_t ui_font_segment7_80;
extern const lv_font_t ui_font_segment7_120;

#ifndef EXT_FONT_DESC_T
#define EXT_FONT_DESC_T
typedef struct _ext_font_desc_t {
    const char *name;
    const void *font_ptr;
} ext_font_desc_t;
#endif

extern ext_font_desc_t fonts[];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/