#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include "ui/ui.h"
#include "ui/screens.h"

// Shared ESP-NOW wire protocol
#include <EspDashProto.h>

// =========================================================================
// WAVESHARE 1.32" CIRCULAR AMOLED (466x466 CO5300 QSPI) OFFICIAL BSP PINS
// =========================================================================
#define AMOLED_CS      10
#define AMOLED_SCK     11
#define AMOLED_D0      12
#define AMOLED_D1      13
#define AMOLED_D2      14
#define AMOLED_D3      15
#define AMOLED_RST     8

// BOOT Button Pin (GPIO 0)
#define BOOT_BTN       0

// CST820 Touch Controller (I2C)
#define TOUCH_SDA      47
#define TOUCH_SCL      48
#define TOUCH_INT      6
#define TOUCH_RST      7

// Display Dimensions & Exact Centering Offset for 466x466 Active Area in 480 Framebuffer
#define DISPLAY_WIDTH   466
#define DISPLAY_HEIGHT  466
#define DISPLAY_COL_OFS 8   // Perfectly centered 8px column offset

// Hardware QSPI Data Bus & AMOLED Display Driver (Native Rotation 0)
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    AMOLED_CS, AMOLED_SCK, AMOLED_D0, AMOLED_D1, AMOLED_D2, AMOLED_D3
);

static Arduino_GFX *gfx = new Arduino_CO5300(
    bus, AMOLED_RST, 0 /* native 0 */, false /* IPS */,
    DISPLAY_WIDTH, DISPLAY_HEIGHT,
    DISPLAY_COL_OFS /* col_offset1 */, 0 /* row_offset1 */,
    DISPLAY_COL_OFS /* col_offset2 */, 0 /* row_offset2 */
);

// =========================================================================
// LVGL DISPLAY DRIVER INTEGRATION (TRUE 180° ROTATION VIA LVGL SOFTWARE ROTATION)
// =========================================================================
#define LV_BUF_LINES 40
static lv_color_t lv_disp_buf[DISPLAY_WIDTH * LV_BUF_LINES];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

// QSPI boundary alignment
static void my_rounder_cb(lv_disp_drv_t *disp, lv_area_t *area) {
    area->x1 = area->x1 & ~1;
    area->x2 = area->x2 | 1;
    if (area->x1 < 0) area->x1 = 0;
    if (area->x2 >= DISPLAY_WIDTH) area->x2 = DISPLAY_WIDTH - 1;
}

// =========================================================================
// TELEMETRY & LINK SUPERVISION
// =========================================================================
static EspDashTelemetry current_pkt = {0};
static uint16_t  current_payload_len = 0;
static uint32_t  last_pkt_rx_time = 0;
static uint16_t  last_seq = 0xFFFF;
static volatile uint32_t pkt_gaps = 0;
static volatile uint32_t pkt_count = 0;
static bool      ever_linked = false;

#define LINK_TIMEOUT_MS  1500
enum LinkState { LINK_SEARCHING, LINK_LIVE, LINK_LOST };

// ESP-NOW Receive Callback
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    uint16_t payload_len = 0;
    uint16_t seq = 0;
    const EspDashTelemetry *pkt = espdash_parse(incomingData, len, &payload_len, &seq);
    if (!pkt) return;

    if (ever_linked && seq != (uint16_t)(last_seq + 1)) {
        uint16_t missed = (uint16_t)(seq - last_seq - 1);
        if (missed && missed < 1000) pkt_gaps += missed;
    }

    last_seq = seq;
    last_pkt_rx_time = millis();
    pkt_count++;
    ever_linked = true;

    uint16_t copy = payload_len < sizeof(EspDashTelemetry) ? payload_len : sizeof(EspDashTelemetry);
    memset(&current_pkt, 0, sizeof(current_pkt));
    memcpy(&current_pkt, pkt, copy);
    current_payload_len = payload_len;
}

// Helper to configure sleek, transparent gauge arc styling and remove intrusive knobs
static void setup_arc_style(lv_obj_t *arc, int16_t range_min, int16_t range_max, uint32_t track_color, uint32_t indic_color, lv_coord_t arc_w) {
    if (!arc) return;

    // Prevent touch interaction capturing arc
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Make widget container completely transparent so concentric arcs don't overlap/clip
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);

    // Track arc
    lv_obj_set_style_arc_color(arc, lv_color_hex(track_color), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arc_w, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    // Active indicator arc
    lv_obj_set_style_arc_color(arc, lv_color_hex(indic_color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arc_w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    // Completely hide knob handle
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

    // Angles: 270-degree sweep from 135 deg to 45 deg
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_angles(arc, 135, 135);
    lv_arc_set_range(arc, range_min, range_max);
    lv_arc_set_value(arc, range_min);
}

// =========================================================================
// HONDA CIVIC 2014 9TH GEN 1.8L (R18) SPECIFIC ECO-COACHING COLOR LOGIC
// =========================================================================
// 1. Fuel Efficiency (L/100km):
//    - GREEN:  <= 6.5 L/100km  (R18 Atkinson cycle i-VTEC cruising sweet spot)
//    - LIME:   6.5 - 8.8 L/100km (Normal efficient cruising)
//    - GOLD:   8.8 - 12.5 L/100km (Moderate city acceleration / incline)
//    - ORANGE: 12.5 - 16.5 L/100km (Heavy load / brisk acceleration)
//    - RED:    > 16.5 L/100km (Open loop / heavy fuel enrichment)
static uint32_t get_civic_efficiency_color(float l_per_100km) {
    if (l_per_100km <= 6.5f) {
        return 0x00e676; // Bright Pure Green
    } else if (l_per_100km <= 8.8f) {
        return 0x76ff03; // Lime Green
    } else if (l_per_100km <= 12.5f) {
        return 0xffd600; // Gold / Yellow
    } else if (l_per_100km <= 16.5f) {
        return 0xff9100; // Orange
    } else {
        return 0xff1744; // Vivid Red
    }
}

// 2. Throttle Position (% pedal):
//    - GREEN:  <= 25% (Optimal R18 i-VTEC economy pedal zone)
//    - YELLOW: 25 - 45% (Moderate acceleration)
//    - ORANGE: 45 - 70% (Brisk acceleration / climbing)
//    - RED:    > 70% (Wide open throttle)
static uint32_t get_civic_throttle_color(uint8_t thr_pct) {
    if (thr_pct <= 25) {
        return 0x00e676; // Bright Green
    } else if (thr_pct <= 45) {
        return 0xffd600; // Gold / Yellow
    } else if (thr_pct <= 70) {
        return 0xff9100; // Orange
    } else {
        return 0xff1744; // Vivid Red
    }
}

// 3. Engine RPM (R18 1.8L i-VTEC):
//    - GREEN:  <= 2500 RPM (Low pumping loss / optimal BSFC thermal efficiency)
//    - CYAN:   2500 - 3500 RPM (Standard highway cruise powerband)
//    - YELLOW: 3500 - 4800 RPM (Torque rise / upshift alert for eco driving)
//    - ORANGE: 4800 - 6200 RPM (High power demand)
//    - RED:    > 6200 RPM (Approaching 6,800 RPM redline)
static uint32_t get_civic_rpm_color(uint16_t rpm) {
    if (rpm <= 2500) {
        return 0x00e676; // Bright Green
    } else if (rpm <= 3500) {
        return 0x00e5ff; // Cyan
    } else if (rpm <= 4800) {
        return 0xffd600; // Yellow
    } else if (rpm <= 6200) {
        return 0xff9100; // Orange
    } else {
        return 0xff1744; // Red
    }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=================================================================");
    Serial.println(" 🏎️ espDash Waveshare 1.32 AMOLED Gauge Display Node");
    Serial.println(" DISPLAY: 466x466 CO5300 Circular AMOLED (USB on Bottom, Rotated 180)");
    Serial.println(" NODE: esp-round-amoled-touch (Civic 9G 1.8L Eco Coach)");
    Serial.println("=================================================================");

    // Hardware Reset Pulse for AMOLED Display (GPIO 8)
    pinMode(AMOLED_RST, OUTPUT);
    digitalWrite(AMOLED_RST, LOW);
    delay(100);
    digitalWrite(AMOLED_RST, HIGH);
    delay(150);

    // Initialize I2C for Touch Screen (SDA: 47, SCL: 48)
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    // Initialize AMOLED Display Driver
    if (!gfx->begin()) {
        Serial.println("[ERROR] GFX AMOLED Display initialization failed!");
    }
    gfx->displayOn();
    gfx->fillScreen(BLACK);

    // Initialize LVGL 8
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, lv_disp_buf, NULL, DISPLAY_WIDTH * LV_BUF_LINES);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.rounder_cb = my_rounder_cb;
    disp_drv.draw_buf = &draw_buf;

    // Enable true 180° rotation in LVGL so USB connector is at the bottom (no hardware mirroring)
    disp_drv.sw_rotate = 1;
    disp_drv.rotated = LV_DISP_ROT_180;

    lv_disp_drv_register(&disp_drv);

    // Initialize EEZ Studio UI
    ui_init();

    // AMOLED pure black background
    if (objects.main) {
        lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(objects.main, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(objects.main, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Configure Arc Styles:
    // 1. rpm_arc (outer, 400x400): 0 to 8000 RPM, track width 14px
    setup_arc_style(objects.rpm_arc, 0, 8000, 0x161b26, 0x00e676, 14);

    // 2. throttle_arc (middle, 340x340): 0 to 100%, track width 12px
    setup_arc_style(objects.throttle_arc, 0, 100, 0x161b26, 0x00e676, 12);

    // 3. eff_arc (inner, 280x280): 0 to 20.0 L/100km (0..200 range for 0.1 res), track width 10px
    setup_arc_style(objects.eff_arc, 0, 200, 0x161b26, 0x00e676, 10);

    // 4. Center Speed Digital Readout (Segment7_120 font)
    if (objects.speed_value) {
        lv_obj_clear_flag(objects.speed_value, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.speed_value, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.speed_value, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_label_set_text(objects.speed_value, "0");
        lv_obj_align(objects.speed_value, LV_ALIGN_CENTER, 0, -25);
    }

    // 5. speed_label ("km/h")
    if (objects.speed_label) {
        lv_obj_clear_flag(objects.speed_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.speed_label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.speed_label, lv_color_hex(0x00e5ff), LV_PART_MAIN);
        lv_label_set_text(objects.speed_label, "km/h");
        lv_obj_align(objects.speed_label, LV_ALIGN_CENTER, 0, 48);
    }

    // 6. Bottom Numerical Data Rows (Updated to EEZ Studio y-positions: 326, 349, 372)
    // Row 1: Fuel Efficiency (e.g. "10.3" "l/100")
    if (objects.eff_value) {
        lv_obj_clear_flag(objects.eff_value, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.eff_value, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.eff_value, 108, 326);
        lv_obj_set_size(objects.eff_value, 124, 24);
        lv_obj_set_style_text_align(objects.eff_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_text(objects.eff_value, "0.0");
    }
    if (objects.eff_label) {
        lv_obj_clear_flag(objects.eff_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.eff_label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.eff_label, 240, 326);
        lv_obj_set_size(objects.eff_label, 70, 24);
        lv_obj_set_style_text_align(objects.eff_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.eff_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    }

    // Row 2: Throttle Position (e.g. "45" "%")
    if (objects.throttle_value) {
        lv_obj_clear_flag(objects.throttle_value, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.throttle_value, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.throttle_value, 108, 349);
        lv_obj_set_size(objects.throttle_value, 124, 24);
        lv_obj_set_style_text_align(objects.throttle_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_text(objects.throttle_value, "0");
    }
    if (objects.throttle_label) {
        lv_obj_clear_flag(objects.throttle_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.throttle_label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.throttle_label, 240, 349);
        lv_obj_set_size(objects.throttle_label, 70, 24);
        lv_obj_set_style_text_align(objects.throttle_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.throttle_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    }

    // Row 3: Engine RPM (e.g. "2450" "rpm")
    if (objects.rpm__value) {
        lv_obj_clear_flag(objects.rpm__value, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.rpm__value, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.rpm__value, 108, 372);
        lv_obj_set_size(objects.rpm__value, 124, 24);
        lv_obj_set_style_text_align(objects.rpm__value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_text(objects.rpm__value, "0");
    }
    if (objects.rpm__label) {
        lv_obj_clear_flag(objects.rpm__label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.rpm__label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.rpm__label, 240, 372);
        lv_obj_set_size(objects.rpm__label, 70, 24);
        lv_obj_set_style_text_align(objects.rpm__label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.rpm__label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    }

    // Row 4: Status / Meaning Label ("Efficiency Monitor")
    if (objects.status_label) {
        lv_obj_clear_flag(objects.status_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(objects.status_label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(objects.status_label, 95, 394);
        lv_obj_set_size(objects.status_label, 276, 24);
        lv_obj_set_style_text_align(objects.status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.status_label, lv_color_hex(0x607d8b), LV_PART_MAIN);
        lv_label_set_text(objects.status_label, "Efficiency Monitor");
    }

    // Setup WiFi Radio for ESP-NOW
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() == ESP_OK) {
        Serial.println("[ESP-NOW] Initialized successfully.");
        esp_now_register_recv_cb(OnDataRecv);
    } else {
        Serial.println("[ERROR] ESP-NOW initialization failed!");
    }
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    uint32_t now = millis();

    // Link Supervision & Demo Telemetry Generation
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);
    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

    EspDashTelemetry active_pkt = {0};
    if (is_demo) {
        float phase = now * 0.0015f;
        // Smooth demo simulation across various driving states
        float rpm_val = 2600.0f + sinf(phase) * 1800.0f + sinf(phase * 2.5f) * 400.0f;
        active_pkt.rpm = (uint16_t)constrain((int)rpm_val, 0, 8000);

        float spd_val = 80.0f + sinf(phase * 0.8f) * 60.0f;
        active_pkt.speed_kmh_x10 = (uint16_t)(constrain((int)spd_val, 0, 300) * 10);

        float thr_val = 38.0f + sinf(phase * 1.4f) * 32.0f;
        active_pkt.throttle_pct = (uint8_t)constrain((int)thr_val, 0, 100);

        // Sweep fuel consumption across all target/warning bands (3.0 to 18.0 L/100km)
        float eff_val = 85.0f + sinf(phase * 0.6f) * 70.0f;
        active_pkt.fuel_consumption_x10 = (uint8_t)constrain((int)eff_val, 0, 200);
    } else {
        active_pkt = current_pkt;
    }

    // State trackers to prevent unnecessary style invalidation and draw buffer starvation
    static uint32_t last_rpm_col = 0;
    static uint32_t last_thr_col = 0;
    static uint32_t last_eff_col = 0;
    static uint16_t last_disp_spd = 0xFFFF;
    static uint16_t last_disp_rpm = 0xFFFF;
    static uint8_t  last_disp_thr = 0xFF;
    static uint8_t  last_disp_eff = 0xFF;

    // 1. Digital Speed Readout (Center) - auto-aligning to geometric center on digit changes
    uint16_t spd = (active_pkt.speed_kmh_x10 + 5) / 10;
    if (spd != last_disp_spd) {
        last_disp_spd = spd;
        if (objects.speed_value) {
            lv_label_set_text_fmt(objects.speed_value, "%u", spd);
            lv_obj_align(objects.speed_value, LV_ALIGN_CENTER, 0, -25);
        }
    }

    // 2. Engine RPM (Arc + Number) - Civic R18 Eco Zone Color-Coded
    uint16_t r = constrain((int)active_pkt.rpm, 0, 8000);
    uint32_t rpm_col = get_civic_rpm_color(r);

    if (r != last_disp_rpm) {
        last_disp_rpm = r;
        if (objects.rpm_arc) lv_arc_set_value(objects.rpm_arc, r);
        if (objects.rpm__value) lv_label_set_text_fmt(objects.rpm__value, "%u", r);
    }
    if (rpm_col != last_rpm_col) {
        last_rpm_col = rpm_col;
        if (objects.rpm_arc) lv_obj_set_style_arc_color(objects.rpm_arc, lv_color_hex(rpm_col), LV_PART_INDICATOR);
        if (objects.rpm__value) lv_obj_set_style_text_color(objects.rpm__value, lv_color_hex(rpm_col), LV_PART_MAIN);
    }

    // 3. Throttle Position (Arc + Number) - Civic R18 Eco Zone Color-Coded
    uint8_t t = constrain((int)active_pkt.throttle_pct, 0, 100);
    uint32_t thr_col = get_civic_throttle_color(t);

    if (t != last_disp_thr) {
        last_disp_thr = t;
        if (objects.throttle_arc) lv_arc_set_value(objects.throttle_arc, t);
        if (objects.throttle_value) lv_label_set_text_fmt(objects.throttle_value, "%u", t);
    }
    if (thr_col != last_thr_col) {
        last_thr_col = thr_col;
        if (objects.throttle_arc) lv_obj_set_style_arc_color(objects.throttle_arc, lv_color_hex(thr_col), LV_PART_INDICATOR);
        if (objects.throttle_value) lv_obj_set_style_text_color(objects.throttle_value, lv_color_hex(thr_col), LV_PART_MAIN);
    }

    // 4. Fuel Efficiency (Arc + Float Number like "10.3") - Civic R18 Eco Zone Color-Coded
    uint8_t eff_raw = constrain((int)active_pkt.fuel_consumption_x10, 0, 200);
    float eff_f = eff_raw / 10.0f;
    uint32_t eff_col = get_civic_efficiency_color(eff_f);

    if (eff_raw != last_disp_eff) {
        last_disp_eff = eff_raw;
        if (objects.eff_arc) lv_arc_set_value(objects.eff_arc, eff_raw);
        if (objects.eff_value) lv_label_set_text_fmt(objects.eff_value, "%u.%u", eff_raw / 10, eff_raw % 10);
    }
    if (eff_col != last_eff_col) {
        last_eff_col = eff_col;
        if (objects.eff_arc) lv_obj_set_style_arc_color(objects.eff_arc, lv_color_hex(eff_col), LV_PART_INDICATOR);
        if (objects.eff_value) lv_obj_set_style_text_color(objects.eff_value, lv_color_hex(eff_col), LV_PART_MAIN);
    }

    // Handle LVGL UI ticking and rendering
    ui_tick();
    lv_timer_handler();

    // Periodic telemetry log
    static uint32_t last_log = 0;
    if (now - last_log >= 2000) {
        last_log = now;
        const char *st = (link == LINK_LIVE) ? "LIVE" : ((link == LINK_LOST) ? "LOST" : "DEMO/SEARCHING");
        Serial.printf("[CIVIC-ECO] %s | RPM:%u | Spd:%.1f | Thr:%u%% | Eff:%.1f L/100km\n",
                      st, active_pkt.rpm, active_pkt.speed_kmh_x10 / 10.0f,
                      active_pkt.throttle_pct, eff_f);
    }

    delay(15); // ~60 FPS smooth rendering loop
}
