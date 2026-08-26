#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)

#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_USE_LOG 0
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_ARC 1
#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_SLIDER 1
#define LV_USE_BTN 1

#ifndef LV_LABEL_LONG_undefined
#define LV_LABEL_LONG_undefined LV_LABEL_LONG_WRAP
#endif
#ifndef LV_ARC_MODE_undefined
#define LV_ARC_MODE_undefined LV_ARC_MODE_NORMAL
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t g_telemetry_rpm;
extern uint32_t g_telemetry_throttle;
#ifdef __cplusplus
}
#endif

#ifndef get_var_3500
#define get_var_3500() ((int32_t)g_telemetry_rpm)
#endif
#ifndef get_var_0
#define get_var_0() ((int32_t)g_telemetry_throttle)
#endif
#ifndef set_var_3500
#define set_var_3500(x) (void)(x)
#endif
#ifndef set_var_0
#define set_var_0(x) (void)(x)
#endif

#endif /* LV_CONF_H */
