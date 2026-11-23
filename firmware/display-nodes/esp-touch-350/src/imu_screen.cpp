#include "imu_screen.h"
#include <math.h>
#include <stdio.h>

static lv_obj_t *scr_imu;
static lv_obj_t *lbl_chip, *lbl_link, *lbl_state;
static lv_obj_t *lbl_roll, *lbl_pitch, *lbl_head, *lbl_head_hdr, *lbl_misc, *lbl_rate, *lbl_magstat, *lbl_lin;
static lv_obj_t *ball, *dot;
static lv_obj_t *lbl_acc[3], *lbl_gyr[3], *lbl_mag[3];

#define C_BG     0x0A0E14
#define C_CARD   0x131A22
#define C_EDGE   0x222E3C
#define C_HDR    0x90A4AE
#define C_CYAN   0x00E5FF
#define C_GREEN  0x00E676
#define C_YELLOW 0xFFD600
#define C_RED    0xFF1744

static lv_obj_t *make_card(lv_obj_t *p, int x, int y, int w, int h) {
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_EDGE), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 6, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *make_label(lv_obj_t *p, const char *txt, const lv_font_t *f, uint32_t col, int x, int y) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *p, const char *txt, int x, int y, int w, int h,
                             lv_event_cb_t cb, uint32_t col = C_CYAN) {
    lv_obj_t *b = lv_btn_create(p);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
    return b;
}

static void on_zero(lv_event_t *)     { imu_zero_attitude(); }
static void on_cal_gyro(lv_event_t *) { imu_recal_gyro(); }
static void on_cal_mag(lv_event_t *)  { imu_reset_mag_cal(); }

// Header plus three axis values; each value is preceded by a small axis tag.
static void make_row(lv_obj_t *card, const char *hdr, int y, lv_obj_t *out[3]) {
    make_label(card, hdr, &lv_font_montserrat_12, C_HDR, 4, y);
    static const char *tags[3] = {"X", "Y", "Z"};
    static const uint32_t cols[3] = {0xFF8A80, 0x69F0AE, 0x82B1FF};
    for (int i = 0; i < 3; i++) {
        make_label(card, tags[i], &lv_font_montserrat_12, cols[i], 4 + i * 70, y + 26);
        out[i] = make_label(card, "--", &lv_font_montserrat_16, 0xFFFFFF, 4 + i * 70 + 11, y + 22);
    }
}

lv_obj_t *imu_screen_build(lv_event_cb_t on_next, lv_event_cb_t on_rotate) {
    scr_imu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_imu, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr_imu, LV_OBJ_FLAG_SCROLLABLE);

    // ---- header ----
    lbl_chip = make_label(scr_imu, "IMU  ...", &lv_font_montserrat_18, C_CYAN, 14, 8);
    lbl_link = make_label(scr_imu, "", &lv_font_montserrat_12, C_HDR, 0, 0);
    lv_obj_set_pos(lbl_link, 156, 12);
    make_button(scr_imu, "NEXT: PFD >", 380, 5, 96, 26, on_next);

    // ---- left card: attitude + g-ball ----
    lv_obj_t *L = make_card(scr_imu, 12, 40, 226, 236);
    make_label(L, "ROLL", &lv_font_montserrat_12, C_HDR, 4, 0);
    lbl_roll = make_label(L, "--", &lv_font_montserrat_28, 0xFFFFFF, 4, 14);
    make_label(L, "PITCH", &lv_font_montserrat_12, C_HDR, 4, 52);
    lbl_pitch = make_label(L, "--", &lv_font_montserrat_28, 0xFFFFFF, 4, 66);
    lbl_head_hdr = make_label(L, "HEADING", &lv_font_montserrat_12, C_HDR, 4, 104);
    lbl_head = make_label(L, "--", &lv_font_montserrat_28, C_CYAN, 4, 118);

    // g-ball: gravity-removed acceleration, +-0.8 g full scale
    ball = lv_obj_create(L);
    lv_obj_set_size(ball, 92, 92);
    lv_obj_set_pos(ball, 114, 4);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ball, lv_color_hex(0x0D1219), 0);
    lv_obj_set_style_border_color(ball, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_border_width(ball, 2, 0);
    lv_obj_set_style_pad_all(ball, 0, 0);
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *hx = lv_obj_create(ball);   // crosshair
    lv_obj_set_size(hx, 76, 1); lv_obj_center(hx);
    lv_obj_set_style_bg_color(hx, lv_color_hex(0x37474F), 0); lv_obj_set_style_border_width(hx, 0, 0);
    lv_obj_t *vx = lv_obj_create(ball);
    lv_obj_set_size(vx, 1, 76); lv_obj_center(vx);
    lv_obj_set_style_bg_color(vx, lv_color_hex(0x37474F), 0); lv_obj_set_style_border_width(vx, 0, 0);
    dot = lv_obj_create(ball);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_pos(dot, 38, 38);
    make_label(L, "G-BALL", &lv_font_montserrat_12, C_HDR, 142, 98);
    lbl_lin = make_label(L, "", &lv_font_montserrat_12, 0xECEFF1, 112, 114);

    lbl_misc = make_label(L, "", &lv_font_montserrat_14, 0xECEFF1, 4, 162);
    lbl_rate = make_label(L, "", &lv_font_montserrat_12, C_HDR, 4, 184);
    lbl_magstat = make_label(L, "", &lv_font_montserrat_12, C_HDR, 4, 202);

    // ---- right card: raw sensor values ----
    lv_obj_t *R = make_card(scr_imu, 246, 40, 222, 236);
    make_row(R, "ACCEL  g", 2, lbl_acc);
    make_row(R, "GYRO  deg/s", 80, lbl_gyr);
    make_row(R, "MAG  uT", 158, lbl_mag);

    // ---- bottom bar ----
    make_button(scr_imu, "ZERO LEVEL", 12, 282, 108, 32, on_zero);
    make_button(scr_imu, "CAL GYRO", 126, 282, 100, 32, on_cal_gyro);
    make_button(scr_imu, "CAL MAG", 232, 282, 96, 32, on_cal_mag);
    make_button(scr_imu, "ROTATE 180", 352, 282, 116, 32, on_rotate);

    lbl_state = make_label(scr_imu, "", &lv_font_montserrat_12, C_YELLOW, 0, 0);
    lv_obj_align(lbl_state, LV_ALIGN_TOP_LEFT, 14, 30);
    lv_obj_add_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
    return scr_imu;
}

static void setf(lv_obj_t *l, const char *fmt, float v) {
    char b[24];
    snprintf(b, sizeof(b), fmt, v);
    lv_label_set_text(l, b);
}

void imu_screen_update(const ImuData &d, const char *link_text, uint32_t link_color) {
    lv_label_set_text(lbl_link, link_text);
    lv_obj_set_style_text_color(lbl_link, lv_color_hex(link_color), 0);

    if (!d.present) {
        lv_label_set_text(lbl_chip, "IMU  not found");
        lv_obj_set_style_text_color(lbl_chip, lv_color_hex(C_RED), 0);
        lv_label_set_text(lbl_state, "No answer on I2C 0x68/0x69. Check SDA=IO38 SCL=IO39, 3V3, GND.");
        lv_obj_set_style_text_color(lbl_state, lv_color_hex(C_RED), 0);
        lv_obj_clear_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char b[64];
    snprintf(b, sizeof(b), "IMU  %s", d.chip);
    lv_label_set_text(lbl_chip, b);
    lv_obj_set_style_text_color(lbl_chip, lv_color_hex(C_CYAN), 0);

    if (d.gyro_calibrating) {
        lv_label_set_text(lbl_state, "Zeroing gyro - hold the unit still...");
        lv_obj_set_style_text_color(lbl_state, lv_color_hex(C_YELLOW), 0);
        lv_obj_clear_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lbl_state, LV_OBJ_FLAG_HIDDEN);
    }

    setf(lbl_roll, "%+.1f\xC2\xB0", d.roll);
    setf(lbl_pitch, "%+.1f\xC2\xB0", d.pitch);
    lv_label_set_text(lbl_head_hdr, (d.has_mag && d.mag_calibrated) ? "HEADING (compass)" : "HEADING (relative)");
    setf(lbl_head, "%.0f\xC2\xB0", d.heading);

    // g-ball
    const float full = 0.8f, rad = 38.0f;
    float bx = d.lin_x / full, by = -d.lin_y / full;
    float r = sqrtf(bx * bx + by * by);
    if (r > 1.0f) { bx /= r; by /= r; }
    lv_obj_set_pos(dot, (lv_coord_t)(38 + bx * rad), (lv_coord_t)(38 + by * rad));
    snprintf(b, sizeof(b), "X %+.2f  Y %+.2f g", d.lin_x, d.lin_y);
    lv_label_set_text(lbl_lin, b);

    snprintf(b, sizeof(b), "|a| %.2f g    %.1f \xC2\xB0""C", d.amag, d.temp_c);
    lv_label_set_text(lbl_misc, b);
    snprintf(b, sizeof(b), "fusion %.0f Hz  #%lu", d.rate_hz, (unsigned long)d.samples);
    lv_label_set_text(lbl_rate, b);
    if (!d.has_mag) {
        lv_label_set_text(lbl_magstat, "No magnetometer (MPU-6500)");
    } else if (d.mag_calibrated) {
        lv_label_set_text(lbl_magstat, "Compass calibrated");
    } else {
        snprintf(b, sizeof(b), "Compass: rotate unit %u%%", d.mag_progress);
        lv_label_set_text(lbl_magstat, b);
    }

    setf(lbl_acc[0], "%+.2f", d.ax); setf(lbl_acc[1], "%+.2f", d.ay); setf(lbl_acc[2], "%+.2f", d.az);
    setf(lbl_gyr[0], "%+.1f", d.gx); setf(lbl_gyr[1], "%+.1f", d.gy); setf(lbl_gyr[2], "%+.1f", d.gz);
    if (d.has_mag) {
        setf(lbl_mag[0], "%+.1f", d.mx); setf(lbl_mag[1], "%+.1f", d.my); setf(lbl_mag[2], "%+.1f", d.mz);
    } else {
        for (int i = 0; i < 3; i++) lv_label_set_text(lbl_mag[i], "n/a");
    }
}
