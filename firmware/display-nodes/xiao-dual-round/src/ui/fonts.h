#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_segment7_80_manual;
extern const lv_font_t ui_font_segment7_28;
extern const lv_font_t ui_font_segment7_48;
extern const lv_font_t ui_font_segment7_80;
extern const lv_font_t ui_font_dseg7_classic_bold_24;
extern const lv_font_t ui_font_dseg14_classic_bold_20;
extern const lv_font_t ui_font_dseg14_classic_bold_28;

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