#include "tpms_gui.h"
#include <stdio.h>

static lv_obj_t *scr_tpms = nullptr;

// TPMS UI Widgets
static lv_obj_t *card_lf;
static lv_obj_t *card_rf;
static lv_obj_t *card_lr;
static lv_obj_t *card_rr;

static lv_obj_t *lbl_lf;
static lv_obj_t *lbl_rf;
static lv_obj_t *lbl_lr;
static lv_obj_t *lbl_rr;

static lv_obj_t *arc_lf;
static lv_obj_t *arc_rf;
static lv_obj_t *arc_lr;
static lv_obj_t *arc_rr;

static lv_obj_t *lbl_arc_lf;
static lv_obj_t *lbl_arc_rf;
static lv_obj_t *lbl_arc_lr;
static lv_obj_t *lbl_arc_rr;

static lv_obj_t *lbl_detail_lf;
static lv_obj_t *lbl_detail_rf;
static lv_obj_t *lbl_detail_lr;
static lv_obj_t *lbl_detail_rr;

static lv_obj_t *lbl_telemetry_panel;

static lv_obj_t *chart_tpms;
static lv_chart_series_t *ser_lf;
static lv_chart_series_t *ser_rf;
static lv_chart_series_t *ser_lr;
static lv_chart_series_t *ser_rr;

#define HISTORY_LEN 20

static float lf_press_history[HISTORY_LEN] = {0.0f};
static float rf_press_history[HISTORY_LEN] = {0.0f};
static float lr_press_history[HISTORY_LEN] = {0.0f};
static float rr_press_history[HISTORY_LEN] = {0.0f};

static void push_to_history(float* history, float value) {
    for (int i = 0; i < HISTORY_LEN - 1; i++) {
        history[i] = history[i + 1];
    }
    history[HISTORY_LEN - 1] = value;
}

static lv_obj_t* create_card_container(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_make(17, 24, 39), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_make(31, 41, 55), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t* create_tire_arc(lv_obj_t* parent, int x, int y, int size, lv_color_t color) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 30); // 0.0 to 3.0 BAR
    lv_arc_set_value(arc, 0);

    lv_obj_set_style_arc_color(arc, lv_color_make(30, 41, 59), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

lv_obj_t* create_tpms_gui() {
    scr_tpms = lv_obj_create(NULL);
    lv_obj_set_pos(scr_tpms, 0, 0);
    lv_obj_set_size(scr_tpms, 820, 320);
    lv_obj_set_style_pad_all(scr_tpms, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr_tpms, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr_tpms, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr_tpms, lv_color_hex(0x0f111a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr_tpms, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(scr_tpms, lv_color_white(), 0);

    // 1. Top Cockpit Header Card (X=16, Y=10, W=720, H=28) -> right edge 736
    lv_obj_t* hdr_card = create_card_container(scr_tpms, 16, 10, 720, 28);
    lv_obj_t* lbl_title = lv_label_create(hdr_card);
    lv_obj_center(lbl_title);
    lv_label_set_recolor(lbl_title, true);
    lv_label_set_text(lbl_title, "#00F0FF HONDA CIVIC# | #10B981 TPMS COCKPIT & TIRE HEALTH#");
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);

    // 2. Front Left (FL) Card: X=16, Y=44, W=164, H=126 -> right edge 180
    card_lf = create_card_container(scr_tpms, 16, 44, 164, 126);
    lbl_lf = lv_label_create(card_lf);
    lv_obj_align(lbl_lf, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_lf, true);
    lv_label_set_text(lbl_lf, "#00FF00 FL# #888888(FRONT L)#");

    arc_lf = create_tire_arc(card_lf, 4, 18, 58, lv_palette_main(LV_PALETTE_GREEN));
    lv_arc_set_value(arc_lf, 24);
    lbl_arc_lf = lv_label_create(card_lf);
    lv_obj_align_to(lbl_arc_lf, arc_lf, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_recolor(lbl_arc_lf, true);
    lv_label_set_text(lbl_arc_lf, "#00FF00 2.40#\n#00FF00BAR#");
    lv_obj_set_style_text_align(lbl_arc_lf, LV_TEXT_ALIGN_CENTER, 0);

    lbl_detail_lf = lv_label_create(card_lf);
    lv_obj_set_pos(lbl_detail_lf, 66, 20);
    lv_label_set_recolor(lbl_detail_lf, true);
    lv_label_set_text(lbl_detail_lf, "#38BDF8 25°C#\n#A855F7 100%#\n#10B981 OPT#");

    // 3. Rear Left (RL) Card: X=16, Y=176, W=164, H=126 -> right edge 180
    card_lr = create_card_container(scr_tpms, 16, 176, 164, 126);
    lbl_lr = lv_label_create(card_lr);
    lv_obj_align(lbl_lr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_lr, true);
    lv_label_set_text(lbl_lr, "#FFFF00 RL# #888888(REAR L)#");

    arc_lr = create_tire_arc(card_lr, 4, 18, 58, lv_palette_main(LV_PALETTE_YELLOW));
    lv_arc_set_value(arc_lr, 24);
    lbl_arc_lr = lv_label_create(card_lr);
    lv_obj_align_to(lbl_arc_lr, arc_lr, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_recolor(lbl_arc_lr, true);
    lv_label_set_text(lbl_arc_lr, "#FFFF00 2.40#\n#FFFF00BAR#");
    lv_obj_set_style_text_align(lbl_arc_lr, LV_TEXT_ALIGN_CENTER, 0);

    lbl_detail_lr = lv_label_create(card_lr);
    lv_obj_set_pos(lbl_detail_lr, 66, 20);
    lv_label_set_recolor(lbl_detail_lr, true);
    lv_label_set_text(lbl_detail_lr, "#38BDF8 25°C#\n#A855F7 100%#\n#10B981 OPT#");

    // 4. Center Area: Rolling Pressure Line Chart (X=188, Y=44, W=376, H=174) -> right edge 564
    lv_obj_t* chart_card = create_card_container(scr_tpms, 188, 44, 376, 174);
    lv_obj_t* lbl_car = lv_label_create(chart_card);
    lv_obj_align(lbl_car, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_car, true);
    lv_label_set_text(lbl_car, "#00FF00 FL# #00FFFF FR# #FFFF00 RL# #FF8000 RR# #888888(Pressure BAR)#");

    chart_tpms = lv_chart_create(chart_card);
    lv_obj_set_size(chart_tpms, 360, 136);
    lv_obj_align(chart_tpms, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(chart_tpms, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_tpms, HISTORY_LEN);
    lv_chart_set_range(chart_tpms, LV_CHART_AXIS_PRIMARY_Y, 0, 35); // 0.0 - 3.5 BAR
    lv_obj_set_style_bg_color(chart_tpms, lv_color_make(11, 15, 25), LV_PART_MAIN);
    lv_obj_set_style_line_color(chart_tpms, lv_color_make(50, 60, 80), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_tpms, 2, LV_PART_ITEMS);

    ser_lf = lv_chart_add_series(chart_tpms, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    ser_rf = lv_chart_add_series(chart_tpms, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);
    ser_lr = lv_chart_add_series(chart_tpms, lv_palette_main(LV_PALETTE_YELLOW), LV_CHART_AXIS_PRIMARY_Y);
    ser_rr = lv_chart_add_series(chart_tpms, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);

    // 5. Center Bottom: Dedicated Telemetry & Health Panel (X=188, Y=224, W=376, H=78) -> right edge 564
    lv_obj_t* telem_card = create_card_container(scr_tpms, 188, 224, 376, 78);
    lbl_telemetry_panel = lv_label_create(telem_card);
    lv_obj_align(lbl_telemetry_panel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_telemetry_panel, true);
    lv_label_set_text(lbl_telemetry_panel,
        "FL: #00FF00 2.40 BAR# | #38BDF8 25°C# | #A855F7 100%#\n"
        "FR: #00FFFF 2.40 BAR# | #38BDF8 25°C# | #A855F7 100%#\n"
        "RL: #FFFF00 2.40 BAR# | #38BDF8 25°C# | #A855F7 100%#\n"
        "RR: #FF8000 2.40 BAR# | #38BDF8 25°C# | #A855F7 100%#"
    );

    // 6. Front Right (FR) Card: X=572, Y=44, W=164, H=126 -> right edge 736
    card_rf = create_card_container(scr_tpms, 572, 44, 164, 126);
    lbl_rf = lv_label_create(card_rf);
    lv_obj_align(lbl_rf, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_rf, true);
    lv_label_set_text(lbl_rf, "#00FFFF FR# #888888(FRONT R)#");

    arc_rf = create_tire_arc(card_rf, 4, 18, 58, lv_palette_main(LV_PALETTE_CYAN));
    lv_arc_set_value(arc_rf, 24);
    lbl_arc_rf = lv_label_create(card_rf);
    lv_obj_align_to(lbl_arc_rf, arc_rf, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_recolor(lbl_arc_rf, true);
    lv_label_set_text(lbl_arc_rf, "#00FFFF 2.40#\n#00FFFFBAR#");
    lv_obj_set_style_text_align(lbl_arc_rf, LV_TEXT_ALIGN_CENTER, 0);

    lbl_detail_rf = lv_label_create(card_rf);
    lv_obj_set_pos(lbl_detail_rf, 66, 20);
    lv_label_set_recolor(lbl_detail_rf, true);
    lv_label_set_text(lbl_detail_rf, "#38BDF8 25°C#\n#A855F7 100%#\n#10B981 OPT#");

    // 7. Rear Right (RR) Card: X=572, Y=176, W=164, H=126 -> right edge 736
    card_rr = create_card_container(scr_tpms, 572, 176, 164, 126);
    lbl_rr = lv_label_create(card_rr);
    lv_obj_align(lbl_rr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_recolor(lbl_rr, true);
    lv_label_set_text(lbl_rr, "#FF8000 RR# #888888(REAR R)#");

    arc_rr = create_tire_arc(card_rr, 4, 18, 58, lv_palette_main(LV_PALETTE_ORANGE));
    lv_arc_set_value(arc_rr, 24);
    lbl_arc_rr = lv_label_create(card_rr);
    lv_obj_align_to(lbl_arc_rr, arc_rr, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_recolor(lbl_arc_rr, true);
    lv_label_set_text(lbl_arc_rr, "#FF8000 2.40#\n#FF8000BAR#");
    lv_obj_set_style_text_align(lbl_arc_rr, LV_TEXT_ALIGN_CENTER, 0);

    lbl_detail_rr = lv_label_create(card_rr);
    lv_obj_set_pos(lbl_detail_rr, 66, 20);
    lv_label_set_recolor(lbl_detail_rr, true);
    lv_label_set_text(lbl_detail_rr, "#38BDF8 25°C#\n#A855F7 100%#\n#10B981 OPT#");

    return scr_tpms;
}

static void get_age_str(uint32_t age_sec, char* out_str, size_t max_len) {
    if (age_sec <= 99) {
        snprintf(out_str, max_len, "(%lus)", (unsigned long)age_sec);
    } else {
        uint32_t m = age_sec / 60;
        uint32_t s = age_sec % 60;
        snprintf(out_str, max_len, "(%lum %lus)", (unsigned long)m, (unsigned long)s);
    }
}

void update_tpms_gui() {
    if (!scr_tpms) return;

    uint32_t now = millis();
    char arc_buf[64];
    char detail_buf[96];
    char telem_buf[384];

    auto format_tire_card = [&](TPMSData& tire, lv_obj_t* arc, lv_obj_t* lbl_arc, lv_obj_t* lbl_detail, lv_chart_series_t* ser, float* press_hist, const char* default_hex_color) {
        if (!tire.active) {
            lv_arc_set_value(arc, 0);
            snprintf(arc_buf, sizeof(arc_buf), "#888888 ---\nBAR#");
            lv_label_set_text(lbl_arc, arc_buf);
            lv_label_set_text(lbl_detail, "#888888 SEARCH#\n#888888 -- °C#\n#888888 --%#");
            push_to_history(press_hist, 0.0f);
        } else {
            uint32_t age_sec = (now - tire.last_update_time) / 1000;
            char age_str[16];
            get_age_str(age_sec, age_str, sizeof(age_str));

            int arc_val = (int)(tire.pressure_bar * 10.0f);
            if (arc_val > 30) arc_val = 30;
            if (arc_val < 0) arc_val = 0;
            lv_arc_set_value(arc, arc_val);

            const char* status_str = "OPT";
            const char* color_code = default_hex_color;

            if (tire.pressure_bar < 1.8f) {
                status_str = "LOW";
                color_code = "#EF4444";
            } else if (tire.pressure_bar > 2.8f) {
                status_str = "HIGH";
                color_code = "#F59E0B";
            }

            snprintf(arc_buf, sizeof(arc_buf), "%s %.2f\nBAR#", color_code, tire.pressure_bar);
            lv_label_set_text(lbl_arc, arc_buf);

            snprintf(detail_buf, sizeof(detail_buf), "#38BDF8 %.0f°C#\n#A855F7 %d%%# %s\n%s %s#",
                     tire.temperature_c, tire.battery_pct, age_str, color_code, status_str);
            lv_label_set_text(lbl_detail, detail_buf);

            push_to_history(press_hist, tire.pressure_bar);
        }

        for (int i = 0; i < HISTORY_LEN; i++) {
            lv_chart_set_value_by_id(chart_tpms, ser, i, (lv_coord_t)(press_hist[i] * 10.0f));
        }
    };

    format_tire_card(tire_lf, arc_lf, lbl_arc_lf, lbl_detail_lf, ser_lf, lf_press_history, "#00FF00");
    format_tire_card(tire_rf, arc_rf, lbl_arc_rf, lbl_detail_rf, ser_rf, rf_press_history, "#00FFFF");
    format_tire_card(tire_lr, arc_lr, lbl_arc_lr, lbl_detail_lr, ser_lr, lr_press_history, "#FFFF00");
    format_tire_card(tire_rr, arc_rr, lbl_arc_rr, lbl_detail_rr, ser_rr, rr_press_history, "#FF8000");

    lv_chart_refresh(chart_tpms);

    // Format Telemetry Health Summary Card
    snprintf(telem_buf, sizeof(telem_buf),
        "FL: #00FF00 %.2f BAR# | #38BDF8 %.1f°C# | #A855F7 %d%% (%.2fV)#\n"
        "FR: #00FFFF %.2f BAR# | #38BDF8 %.1f°C# | #A855F7 %d%% (%.2fV)#\n"
        "RL: #FFFF00 %.2f BAR# | #38BDF8 %.1f°C# | #A855F7 %d%% (%.2fV)#\n"
        "RR: #FF8000 %.2f BAR# | #38BDF8 %.1f°C# | #A855F7 %d%% (%.2fV)#",
        tire_lf.pressure_bar, tire_lf.temperature_c, tire_lf.battery_pct, tire_lf.battery_v,
        tire_rf.pressure_bar, tire_rf.temperature_c, tire_rf.battery_pct, tire_rf.battery_v,
        tire_lr.pressure_bar, tire_lr.temperature_c, tire_lr.battery_pct, tire_lr.battery_v,
        tire_rr.pressure_bar, tire_rr.temperature_c, tire_rr.battery_pct, tire_rr.battery_v
    );
    lv_label_set_text(lbl_telemetry_panel, telem_buf);
}
