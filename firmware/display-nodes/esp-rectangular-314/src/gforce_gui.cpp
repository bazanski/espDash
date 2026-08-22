#include "gforce_gui.h"
#include <stdio.h>
#include <math.h>

static lv_obj_t* scr_gforce = nullptr;
static lv_obj_t* meter_cont = nullptr;

#define TRAIL_LEN 15
static lv_obj_t* trail_dots[TRAIL_LEN] = {nullptr};
static lv_point_t trail_pos[TRAIL_LEN] = {
    {128, 128}, {128, 128}, {128, 128}, {128, 128}, {128, 128},
    {128, 128}, {128, 128}, {128, 128}, {128, 128}, {128, 128},
    {128, 128}, {128, 128}, {128, 128}, {128, 128}, {128, 128}
};

static lv_obj_t* dot_g = nullptr;
static lv_obj_t* dot_peak = nullptr;
static lv_obj_t* lbl_scale = nullptr;

static lv_obj_t* lbl_lat = nullptr;
static lv_obj_t* lbl_long = nullptr;
static lv_obj_t* lbl_total = nullptr;
static lv_obj_t* lbl_peak = nullptr;

static lv_obj_t* chart_g = nullptr;
static lv_chart_series_t* ser_lat = nullptr;
static lv_chart_series_t* ser_total = nullptr;

static float history_lat[10] = {0.0f};
static float history_total[10] = {0.0f};

static lv_obj_t* create_card_container(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_make(17, 24, 39), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_make(31, 41, 55), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

lv_obj_t* create_gforce_gui() {
    scr_gforce = lv_obj_create(NULL);
    lv_obj_set_pos(scr_gforce, 0, 0);
    lv_obj_set_size(scr_gforce, 820, 320);
    lv_obj_set_style_pad_all(scr_gforce, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr_gforce, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr_gforce, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr_gforce, lv_color_hex(0x0f111a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_gforce, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(scr_gforce, lv_color_white(), 0);

    // 1. Left Column: Circular Bullseye Target (256x256px, Pos X=16, Y=32) -> right edge 272
    meter_cont = lv_obj_create(scr_gforce);
    lv_obj_set_size(meter_cont, 256, 256);
    lv_obj_set_pos(meter_cont, 16, 32);
    lv_obj_set_style_bg_color(meter_cont, lv_color_make(10, 15, 25), LV_PART_MAIN);
    lv_obj_set_style_border_color(meter_cont, lv_color_make(0, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_border_width(meter_cont, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(meter_cont, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(meter_cont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(meter_cont, LV_OBJ_FLAG_SCROLLABLE);

    // Concentric grid circles (5 rings: 236, 188, 140, 94, 46 px)
    const uint16_t ring_sizes[5] = {236, 188, 140, 94, 46};
    for (int i = 0; i < 5; i++) {
        lv_obj_t* ring = lv_obj_create(meter_cont);
        lv_obj_set_size(ring, ring_sizes[i], ring_sizes[i]);
        lv_obj_center(ring);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(ring, (i == 0) ? lv_color_make(0, 240, 255) : lv_color_make(45, 65, 95), LV_PART_MAIN);
        lv_obj_set_style_border_width(ring, (i == 0) ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Crosshairs (+) lines passing exactly through dead-center (128, 128)
    static lv_point_t line_h[2] = {{0, 128}, {256, 128}};
    static lv_point_t line_v[2] = {{128, 0}, {128, 256}};
    
    lv_obj_t* ch_h = lv_line_create(meter_cont);
    lv_line_set_points(ch_h, line_h, 2);
    lv_obj_set_style_line_color(ch_h, lv_color_make(60, 80, 110), LV_PART_MAIN);
    lv_obj_set_style_line_width(ch_h, 1, LV_PART_MAIN);

    lv_obj_t* ch_v = lv_line_create(meter_cont);
    lv_line_set_points(ch_v, line_v, 2);
    lv_obj_set_style_line_color(ch_v, lv_color_make(60, 80, 110), LV_PART_MAIN);
    lv_obj_set_style_line_width(ch_v, 1, LV_PART_MAIN);

    // Trail dots (15-segment fading motion comet trail)
    for (int i = 0; i < TRAIL_LEN; i++) {
        uint8_t size = (i <= 11) ? (18 - (i * 3 / 2)) : 3;
        lv_opa_t opa = (lv_opa_t)(210 - (i * 13));
        trail_dots[i] = lv_obj_create(meter_cont);
        lv_obj_set_size(trail_dots[i], size, size);
        lv_obj_set_style_bg_color(trail_dots[i], lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(trail_dots[i], opa, LV_PART_MAIN);
        lv_obj_set_style_border_width(trail_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(trail_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_pos(trail_dots[i], 128 - size/2, 128 - size/2);
    }

    // Peak G Hold Dot (Yellow 12x12px)
    dot_peak = lv_obj_create(meter_cont);
    lv_obj_set_size(dot_peak, 12, 12);
    lv_obj_set_style_bg_color(dot_peak, lv_palette_main(LV_PALETTE_YELLOW), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot_peak, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dot_peak, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(dot_peak, 122, 122);

    // Live G Vector Ball Dot (22x22px)
    dot_g = lv_obj_create(meter_cont);
    lv_obj_set_size(dot_g, 22, 22);
    lv_obj_set_style_bg_color(dot_g, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_set_style_border_color(dot_g, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(dot_g, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(dot_g, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_pos(dot_g, 117, 117);

    // 2. Center Column: Digital Telemetry Card Container (Pos X=282, Y=32, W=224, H=256) -> right edge 506
    lv_obj_t* card_telemetry = create_card_container(scr_gforce, 282, 32, 224, 256);

    lv_obj_t* lbl_title = lv_label_create(card_telemetry);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_recolor(lbl_title, true);
    lv_label_set_text(lbl_title, "#00FF00 == G-FORCE METER ==#\n#00FFFF WIDE-RADAR TELEMETRY#");
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);

    lbl_scale = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_scale, 4, 42);
    lv_label_set_recolor(lbl_scale, true);
    lv_label_set_text(lbl_scale, "#00FF00 RADAR: 0.6G (0.12G/Ring)#");

    lbl_lat = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_lat, 4, 74);
    lv_label_set_recolor(lbl_lat, true);
    lv_label_set_text(lbl_lat, "#00FF00 LATERAL G:#  0.00g (CENTER)");

    lbl_long = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_long, 4, 110);
    lv_label_set_recolor(lbl_long, true);
    lv_label_set_text(lbl_long, "#00FFFF LONG G:#     0.00g (STATIONARY)");

    lbl_total = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_total, 4, 146);
    lv_label_set_recolor(lbl_total, true);
    lv_label_set_text(lbl_total, "#38BDF8 TOTAL G:#    0.00g");

    lbl_peak = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_peak, 4, 182);
    lv_label_set_recolor(lbl_peak, true);
    lv_label_set_text(lbl_peak, "#FFFF00 PEAK G:#     0.00g (Max)");

    lv_obj_t* lbl_sensor = lv_label_create(card_telemetry);
    lv_obj_set_pos(lbl_sensor, 4, 216);
    lv_label_set_recolor(lbl_sensor, true);
    lv_label_set_text(lbl_sensor, "#888888 SENSOR: QMI8658 6-AXIS#");

    // 3. Right Column: Rolling Real-Time Line Chart (Pos X=516, Y=32, W=220, H=256) -> right edge 736!
    lv_obj_t* chart_card = create_card_container(scr_gforce, 516, 32, 220, 256);

    lv_obj_t* lbl_chart_hdr = lv_label_create(chart_card);
    lv_obj_align(lbl_chart_hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_chart_hdr, true);
    lv_label_set_text(lbl_chart_hdr, "#00FFFF LAT G# | #FFFF00 TOTAL G#");

    chart_g = lv_chart_create(chart_card);
    lv_obj_set_size(chart_g, 204, 208);
    lv_obj_align(chart_g, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(chart_g, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_g, 10);
    lv_chart_set_range(chart_g, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_bg_color(chart_g, lv_color_make(15, 15, 20), LV_PART_MAIN);
    lv_obj_set_style_line_color(chart_g, lv_color_make(60, 60, 80), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_g, 2, LV_PART_ITEMS);

    ser_lat = lv_chart_add_series(chart_g, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    ser_total = lv_chart_add_series(chart_g, lv_palette_main(LV_PALETTE_YELLOW), LV_CHART_AXIS_PRIMARY_Y);

    return scr_gforce;
}

void update_gforce_gui(const GForceData& gdata) {
    if (!scr_gforce) return;

    // Smart Dynamic Radar Scale: Default 0.6G precision mode, expanding to 1.0G if G > 0.65G
    static float max_scale = 0.60f;
    if (gdata.total_g > 0.65f || gdata.peak_g > 0.65f) {
        max_scale = 1.00f;
    } else if (gdata.total_g < 0.50f && gdata.peak_g <= 0.60f) {
        max_scale = 0.60f;
    }

    float px_per_g = 118.0f / max_scale;

    // Update Scale Legend Label
    char scale_buf[128];
    if (max_scale > 0.80f) {
        snprintf(scale_buf, sizeof(scale_buf), "#FF8000 RADAR: 1.0G (0.20G/Ring)#");
    } else {
        snprintf(scale_buf, sizeof(scale_buf), "#00FF00 RADAR: 0.6G (0.12G/Ring)#");
    }
    lv_label_set_text(lbl_scale, scale_buf);

    // Calculate current G point (Center of 256x256 target is 128, 128; 22px dot offset is 117)
    int dot_x = (int)(117.0f + (gdata.lat_g * px_per_g));
    int dot_y = (int)(117.0f - (gdata.long_g * px_per_g));

    // Clamp dot inside target bounds
    if (dot_x < 2) dot_x = 2;
    if (dot_x > 232) dot_x = 232;
    if (dot_y < 2) dot_y = 2;
    if (dot_y > 232) dot_y = 232;

    // Dynamic G-Severity Color (Green < 0.5g, Cyan/Yellow 0.5g-1.0g, Red > 1.0g)
    lv_color_t active_color;
    if (gdata.total_g < 0.5f) {
        active_color = lv_palette_main(LV_PALETTE_GREEN);
    } else if (gdata.total_g < 1.0f) {
        active_color = lv_palette_main(LV_PALETTE_CYAN);
    } else {
        active_color = lv_palette_main(LV_PALETTE_RED);
    }

    // Update Live Vector Ball Position & Color
    lv_obj_set_pos(dot_g, dot_x, dot_y);
    lv_obj_set_style_bg_color(dot_g, active_color, LV_PART_MAIN);

    // Update Motion Trail Dots (15-segment fading comet tail)
    for (int i = TRAIL_LEN - 1; i > 0; i--) {
        trail_pos[i] = trail_pos[i - 1];
    }
    trail_pos[0].x = dot_x + 11; // Center of 22px ball
    trail_pos[0].y = dot_y + 11;

    for (int i = 0; i < TRAIL_LEN; i++) {
        uint8_t size = (i <= 11) ? (18 - (i * 3 / 2)) : 3;
        int tx = trail_pos[i].x - (size / 2);
        int ty = trail_pos[i].y - (size / 2);
        lv_obj_set_pos(trail_dots[i], tx, ty);
        lv_obj_set_style_bg_color(trail_dots[i], active_color, LV_PART_MAIN);
    }

    // Peak dot position (size 12x12 -> offset 6px from 128)
    int peak_x = (int)(122.0f + (gdata.peak_lat_g * px_per_g));
    int peak_y = (int)(122.0f - (gdata.peak_long_g * px_per_g));
    if (peak_x < 5) peak_x = 5;
    if (peak_x > 239) peak_x = 239;
    if (peak_y < 5) peak_y = 5;
    if (peak_y > 239) peak_y = 239;

    lv_obj_set_pos(dot_peak, peak_x, peak_y);

    // Update labels
    char buf[128];
    const char* lat_dir = (gdata.lat_g > 0.05f) ? "LEFT" : ((gdata.lat_g < -0.05f) ? "RIGHT" : "CENTER");
    snprintf(buf, sizeof(buf), "#00FF00 LATERAL G:#  %+.2fg (%s)", gdata.lat_g, lat_dir);
    lv_label_set_text(lbl_lat, buf);

    const char* long_dir = (gdata.long_g > 0.05f) ? "ACCEL" : ((gdata.long_g < -0.05f) ? "BRAKE" : "STATIONARY");
    snprintf(buf, sizeof(buf), "#00FFFF LONG G:#     %+.2fg (%s)", gdata.long_g, long_dir);
    lv_label_set_text(lbl_long, buf);

    snprintf(buf, sizeof(buf), "#38BDF8 TOTAL G:#    %.2fg", gdata.total_g);
    lv_label_set_text(lbl_total, buf);

    snprintf(buf, sizeof(buf), "#FFFF00 PEAK G:#     %.2fg (Max)", gdata.peak_g);
    lv_label_set_text(lbl_peak, buf);

    // Update rolling history chart
    for (int i = 0; i < 9; i++) {
        history_lat[i] = history_lat[i + 1];
        history_total[i] = history_total[i + 1];
    }
    history_lat[9] = fabsf(gdata.lat_g);
    history_total[9] = gdata.total_g;

    for (int i = 0; i < 10; i++) {
        lv_chart_set_value_by_id(chart_g, ser_lat, i, (lv_coord_t)(history_lat[i] * 100.0f));
        lv_chart_set_value_by_id(chart_g, ser_total, i, (lv_coord_t)(history_total[i] * 100.0f));
    }
    lv_chart_refresh(chart_g);
}
