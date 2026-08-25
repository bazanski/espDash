#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// =========================================================================
// ESP-NOW TELEMETRY - shared wire protocol
// =========================================================================
// The packet layout lives in firmware/shared/EspDashProto. Do NOT paste a
// copy of the struct in here: that is exactly how the gateway and this node
// drifted apart before.
#include <EspDashProto.h>

static EspDashTelemetry current_pkt = {0};
static uint16_t  current_payload_len = 0;   // what the sender actually sent
static uint32_t  last_pkt_rx_time = 0;
static uint16_t  last_seq = 0;
static volatile uint32_t pkt_gaps = 0;      // missed sequence numbers
static volatile uint32_t pkt_count = 0;     // valid packets since boot
static bool      ever_linked = false;

// ---- ESP-NOW channel ----------------------------------------------------
// With ESPDASH_NODE_WIFI=0 (the default, and the only mode this node
// supports) this node never associates to Wi-Fi, so it locks to
// ESPDASH_ESPNOW_CHANNEL in setup() - the same fixed constant the gateway
// uses - and never needs to move. Ported verbatim from xiao-round-gauge;
// see that node's main.cpp:33-49 for the full channel-hop rationale (this
// node doesn't compile that branch in, ESPDASH_NODE_WIFI is not wired up
// as a runtime option here the way it is on node 2 - always off).
static uint8_t  espnow_channel = ESPDASH_ESPNOW_CHANNEL;
static bool     channel_locked = false;
#define LINK_TIMEOUT_MS  1500   // no valid packet => link considered lost

// LINK_LOST is deliberately distinct from LINK_SEARCHING: a gauge that had a
// gateway and lost it is a fault worth showing, whereas one that has never
// seen a gateway is just still looking.
enum LinkState { LINK_LIVE, LINK_SEARCHING, LINK_LOST };

// The channel the radio is actually on, which is not necessarily
// espnow_channel: when Wi-Fi is associated it owns the channel and the scan
// never runs, so the scan variable would misreport it.
static uint8_t actual_channel() {
    uint8_t ch = 0;
    wifi_second_chan_t sec;
    if (esp_wifi_get_channel(&ch, &sec) != ESP_OK) return espnow_channel;
    return ch;
}

// =========================================================================
// ESP-NOW RECEIVE CALLBACK - ported verbatim from xiao-round-gauge:146-176
// =========================================================================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    uint16_t plen = 0, seq = 0;
    const EspDashTelemetry *t = espdash_parse(incomingData, len, &plen, &seq);
    if (!t) return;   // not ours, or an incompatible major version

    // Copy only what the sender actually provided, leaving any newer trailing
    // fields we do not know about at zero. This is what lets an old node keep
    // working against a newer gateway.
    uint16_t copy = plen < sizeof(EspDashTelemetry) ? plen : sizeof(EspDashTelemetry);
    memset(&current_pkt, 0, sizeof(current_pkt));
    memcpy(&current_pkt, t, copy);
    current_payload_len = plen;

    if (ever_linked) {
        // Count how many packets were actually missed, not just how many times
        // a discontinuity occurred - the difference matters when diagnosing.
        uint16_t missed = (uint16_t)(seq - last_seq - 1);
        if (missed && missed < 1000) pkt_gaps += missed;
    }
    last_seq = seq;
    last_pkt_rx_time = millis();
    ever_linked = true;
    pkt_count++;

    if (!channel_locked) {
        channel_locked = true;
        espnow_channel = actual_channel();
        Serial.printf("[ESP-NOW] Locked to channel %u (proto payload %u bytes)\n",
                      espnow_channel, plen);
    }
}

// =========================================================================
// REUSED HELPERS - verbatim from xiao-round-gauge
// =========================================================================
static const char* get_gear_str(uint8_t g) {
    switch(g) {
        case 0: return "P";
        case 1: return "R";
        case 2: return "N";
        case 3: return "D";
        case 4: return "S";
        case 5: return "1";
        case 6: return "2";
        case 7: return "3";
        case 8: return "4";
        case 9: return "5";
        case 10: return "6";
        default: return "D";
    }
}

// CIVIC 9G 1.8L ECO-COACHING COLOR LOGIC - xiao-round-gauge/src/main.cpp:198-219
static uint32_t get_civic_rpm_color(uint16_t rpm) {
    if (rpm <= 2500) return 0x00e676; // Bright Green
    else if (rpm <= 3500) return 0x00e5ff; // Cyan
    else if (rpm <= 4800) return 0xffd600; // Yellow
    else if (rpm <= 6200) return 0xff9100; // Orange
    else return 0xff1744; // Red
}

static uint32_t get_civic_throttle_color(uint8_t thr_pct) {
    if (thr_pct <= 25) return 0x00e676; // Bright Green
    else if (thr_pct <= 45) return 0xffd600; // Gold / Yellow
    else if (thr_pct <= 70) return 0xff9100; // Orange
    else return 0xff1744; // Red
}

static uint32_t get_civic_efficiency_color(float l_per_100km) {
    if (l_per_100km <= 6.5f) return 0x00e676; // Bright Pure Green
    else if (l_per_100km <= 8.8f) return 0x76ff03; // Lime Green
    else if (l_per_100km <= 12.5f) return 0xffd600; // Gold / Yellow
    else if (l_per_100km <= 16.5f) return 0xff9100; // Orange
    else return 0xff1744; // Vivid Red
}

static void setup_arc_style(lv_obj_t *arc, int16_t range_min, int16_t range_max, uint32_t track_color, uint32_t indic_color, lv_coord_t arc_w) {
    if (!arc) return;
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
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

    // Hide knob handle completely
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
}

// =========================================================================
// DUAL PANEL BUS/DISPLAY - the new part this node exists for
// =========================================================================
// Pin assignments per the approved plan (snoopy-enchanting-bachman.md).
// Shared MOSI/SCLK/RST/BL is deliberate: reuses xiao-round-gauge's existing
// pin assignments for the shared lines unchanged.
#define PIN_MOSI 9
#define PIN_SCLK 7
#define PIN_RST  4
#define PIN_BL   43
#define PIN_CS_A 2
#define PIN_DC_A 3
#define PIN_CS_B 1
#define PIN_DC_B 5

#define PANEL_H  240   // one physical panel's height
#define VDISP_W  240   // virtual LVGL display: 240 wide...
#define VDISP_H  480   // ...480 tall = panel A (rows 0-239) + panel B (rows 240-479)

#define SPI_HZ   40000000UL   // 40MHz to start - see plan for the 80MHz bench-experiment note

// Two independent Arduino_ESP32SPI bus objects sharing one SPI peripheral
// (is_shared_interface = true), each with its own DC/CS. Constructor order
// verified against the actual installed GFX 1.4.9 header
// (esp-round-amoled-touch/.pio/libdeps/.../databus/Arduino_ESP32SPI.h):
//   Arduino_ESP32SPI(dc, cs, sck, mosi, miso, spi_num, is_shared_interface)
// Note DC precedes CS positionally - easy to get backwards by ear from the
// plan's prose ("its own DC and CS"), which is why this is spelled out here.
static Arduino_DataBus *busA = new Arduino_ESP32SPI(
    PIN_DC_A, PIN_CS_A, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED, FSPI, true);
static Arduino_DataBus *busB = new Arduino_ESP32SPI(
    PIN_DC_B, PIN_CS_B, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED, FSPI, true);

// GFX_NOT_DEFINED as RST on BOTH panels. RST is driven manually, once, in
// setup() below (single shared physical pin). Verified in the installed
// Arduino_GC9A01.cpp::tftInit(): when _rst == GFX_NOT_DEFINED, tftInit()
// skips the hardware reset pulse entirely (no software-reset command is
// sent either - it relies on our external pulse having already happened).
// If either constructor were given PIN_RST directly instead, that panel's
// begin() would pulse reset itself - and bringing up B would re-pulse reset
// out from under an already-initialized A.
static Arduino_GC9A01 *gfxA = new Arduino_GC9A01(busA, GFX_NOT_DEFINED, 0, false, 240, 240);
static Arduino_GC9A01 *gfxB = new Arduino_GC9A01(busB, GFX_NOT_DEFINED, 0, false, 240, 240);

// ---- The splitting flush_cb ---------------------------------------------
// From the approved plan, verbatim design: color_p arrives row-major with
// stride == area width, which for a vertical split is always 240 (the panel
// width) - so a straddling area splits into two tightly packed contiguous
// chunks by pointer arithmetic, no copy needed.
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    uint16_t *buf = (uint16_t *)&color_p->full;

    if (area->y2 < PANEL_H) {                 // entirely panel A
        gfxA->draw16bitRGBBitmap(area->x1, area->y1, buf, w, h);
    } else if (area->y1 >= PANEL_H) {         // entirely panel B
        gfxB->draw16bitRGBBitmap(area->x1, area->y1 - PANEL_H, buf, w, h);
    } else {                                  // straddles the seam
        uint32_t rows_a = PANEL_H - area->y1;
        gfxA->draw16bitRGBBitmap(area->x1, area->y1, buf, w, rows_a);
        gfxB->draw16bitRGBBitmap(area->x1, 0, buf + w * rows_a, w, h - rows_a);
    }
    lv_disp_flush_ready(d);
}

// ---- Per-frame FPS counter ----------------------------------------------
// LVGL calls monitor_cb once per completed refresh cycle (a "frame", in the
// sense of one round of dirty-rect flushing), independent of flush_cb which
// may fire multiple times per frame for multiple dirty rects. This is what
// step 5 of the plan measures on hardware; here it's just the counter and
// the [LINK] log integration - no hardware to confirm the actual number
// against the 50Hz estimate.
static volatile uint32_t g_frame_count = 0;
static void disp_monitor_cb(lv_disp_drv_t *disp_drv, uint32_t time_ms, uint32_t px) {
    (void)disp_drv; (void)time_ms; (void)px;
    g_frame_count++;
}

// ============================================================================
// PLACEHOLDER UI - DELETE THIS ENTIRE BLOCK WHEN THE EEZ EXPORT LANDS
// ============================================================================
// Stands in for `src/ui/` (an EEZ Studio export - user-authored, not present
// yet; see docs/ARCHITECTURE.md S6). It exists only so this firmware compiles
// and the hardware can be brought up end-to-end before the real UI exists.
//
// To swap it out once the EEZ export lands:
//   1. Delete everything from this marker down to the matching
//      "END PLACEHOLDER" marker below, including the global lv_obj_t*
//      handles and build_placeholder_ui() itself.
//   2. #include "ui/ui.h" and "ui/screens.h" at the top of this file
//      (xiao-round-gauge/src/main.cpp:8-9 is the pattern).
//   3. In setup(), replace the `build_placeholder_ui();` call (in the
//      "#else" branch below) with `ui_init();`.
//   4. In loop(), replace the placeholder update block (also behind
//      "#else") with `ui_tick();` plus lv_label_set_text_fmt() /
//      lv_arc_set_value() calls against the EEZ-generated `objects.*` tree.
//      xiao-round-gauge/src/main.cpp:704-746 is the exact template to
//      follow: same telemetry fields, same get_civic_*_color() thresholds,
//      just against EEZ object names instead of the g_* handles below.
//   5. lv_conf.h has a matching note about the EEZ native-variable
//      extern/get_var_*/set_var_* block xiao-round-gauge's lv_conf.h
//      carries - re-add the equivalent once the export generates its own
//      variable ids.
// ============================================================================
static lv_obj_t *g_rpm_arc = nullptr;
static lv_obj_t *g_speed_label = nullptr;
static lv_obj_t *g_gear_label = nullptr;
static lv_obj_t *g_link_label = nullptr;
static lv_obj_t *g_inst_fuel_label = nullptr;
static lv_obj_t *g_avg_fuel_label = nullptr;
static lv_obj_t *g_coolant_label = nullptr;
static lv_obj_t *g_batt_label = nullptr;
static lv_obj_t *g_ambient_label = nullptr;

static void build_placeholder_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f19), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- Panel A (rows 0-239): driving ----
    g_rpm_arc = lv_arc_create(scr);
    lv_obj_set_size(g_rpm_arc, 200, 200);
    lv_obj_set_pos(g_rpm_arc, 20, 20);
    lv_arc_set_range(g_rpm_arc, 0, 8000);
    lv_arc_set_bg_angles(g_rpm_arc, 135, 45);
    lv_arc_set_rotation(g_rpm_arc, 0);
    lv_arc_set_value(g_rpm_arc, 0);
    setup_arc_style(g_rpm_arc, 0, 8000, 0x161b26, 0x00e676, 10);

    g_speed_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_speed_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_speed_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_text(g_speed_label, "0");
    lv_obj_align(g_speed_label, LV_ALIGN_TOP_MID, 0, 90);

    g_gear_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_gear_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_gear_label, lv_color_hex(0xffd600), LV_PART_MAIN);
    lv_label_set_text(g_gear_label, "P");
    lv_obj_align(g_gear_label, LV_ALIGN_TOP_MID, 0, 160);

    g_link_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_link_label, lv_color_hex(0x00e5ff), LV_PART_MAIN);
    lv_label_set_text(g_link_label, "SEARCHING...");
    lv_obj_align(g_link_label, LV_ALIGN_TOP_MID, 0, 6);

    // ---- Panel B (rows 240-479, i.e. virtual y offset +240): efficiency ----
    g_inst_fuel_label = lv_label_create(scr);
    lv_obj_set_style_text_font(g_inst_fuel_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_inst_fuel_label, lv_color_hex(0x00e676), LV_PART_MAIN);
    lv_label_set_text(g_inst_fuel_label, "0.0");
    lv_obj_align(g_inst_fuel_label, LV_ALIGN_TOP_MID, 0, 240 + 30);

    g_avg_fuel_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_avg_fuel_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    lv_label_set_text(g_avg_fuel_label, "avg --.- L/100km");
    lv_obj_align(g_avg_fuel_label, LV_ALIGN_TOP_MID, 0, 240 + 90);

    g_coolant_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_coolant_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    lv_label_set_text(g_coolant_label, "--C");
    lv_obj_align(g_coolant_label, LV_ALIGN_TOP_MID, -55, 240 + 140);

    g_batt_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_batt_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    lv_label_set_text(g_batt_label, "--.-V");
    lv_obj_align(g_batt_label, LV_ALIGN_TOP_MID, 55, 240 + 140);

    g_ambient_label = lv_label_create(scr);
    lv_obj_set_style_text_color(g_ambient_label, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    lv_label_set_text(g_ambient_label, "amb --C");
    lv_obj_align(g_ambient_label, LV_ALIGN_TOP_MID, 0, 240 + 180);
}

// Updates the placeholder widgets from live/demo telemetry. Deleted along
// with the rest of this block when the EEZ export lands (step 4 above).
static void update_placeholder_ui(const EspDashTelemetry &pkt, LinkState link) {
    static uint32_t last_rpm_col = 0;

    uint32_t rpm_col = get_civic_rpm_color(pkt.rpm);
    if (g_rpm_arc) {
        lv_arc_set_value(g_rpm_arc, pkt.rpm);
        if (rpm_col != last_rpm_col) {
            last_rpm_col = rpm_col;
            lv_obj_set_style_arc_color(g_rpm_arc, lv_color_hex(rpm_col), LV_PART_INDICATOR);
        }
    }

    if (g_speed_label) lv_label_set_text_fmt(g_speed_label, "%d", pkt.speed_kmh_x10 / 10);
    if (g_gear_label) lv_label_set_text(g_gear_label, get_gear_str(pkt.gear));

    if (g_link_label) {
        const char *txt; uint32_t col;
        switch (link) {
            case LINK_LIVE:      txt = "LIVE";       col = 0x00e5ff; break;
            case LINK_LOST:      txt = "LOST";       col = 0xff1744; break;
            default:              txt = "SEARCHING..."; col = 0xffd600; break;
        }
        lv_label_set_text(g_link_label, txt);
        lv_obj_set_style_text_color(g_link_label, lv_color_hex(col), LV_PART_MAIN);
    }

    if (g_inst_fuel_label) {
        uint8_t f = pkt.fuel_consumption_x10;
        lv_label_set_text_fmt(g_inst_fuel_label, "%d.%d", f / 10, f % 10);
        lv_obj_set_style_text_color(g_inst_fuel_label, lv_color_hex(get_civic_efficiency_color(f / 10.0f)), LV_PART_MAIN);
    }

    if (g_avg_fuel_label) {
        // Guard fuel_avg_x10 (v2.2) against an older gateway that never sent
        // it: current_payload_len is 0 for demo packets (they never go
        // through espdash_parse), so ESPDASH_HAS() correctly reads as "not
        // present" during the demo sweep too, and the label shows "--".
        if (ESPDASH_HAS(current_payload_len, fuel_avg_x10) && pkt.fuel_avg_x10 > 0) {
            lv_label_set_text_fmt(g_avg_fuel_label, "avg %d.%d L/100km",
                                   pkt.fuel_avg_x10 / 10, pkt.fuel_avg_x10 % 10);
        } else {
            lv_label_set_text(g_avg_fuel_label, "avg --.- L/100km");
        }
    }

    if (g_coolant_label) {
        float c = pkt.water_temp_x10 / 10.0f;
        bool overheat = c > 105.0f;
        lv_label_set_text_fmt(g_coolant_label, "%d\xC2\xB0" "C", (int)c);
        lv_obj_set_style_text_color(g_coolant_label, lv_color_hex(overheat ? 0xff1744 : 0x8c9eb5), LV_PART_MAIN);
    }

    if (g_batt_label) {
        uint16_t mv = pkt.battery_mv;
        if (mv > 0) {
            bool low = mv < 12000;
            lv_label_set_text_fmt(g_batt_label, "%d.%dV", mv / 1000, (mv % 1000) / 100);
            lv_obj_set_style_text_color(g_batt_label, lv_color_hex(low ? 0xff1744 : 0x8c9eb5), LV_PART_MAIN);
        } else {
            lv_label_set_text(g_batt_label, "--.-V");
        }
    }

    if (g_ambient_label) lv_label_set_text_fmt(g_ambient_label, "amb %d\xC2\xB0" "C", pkt.ambient_temp);
}

#if XIAO_DUAL_SEAM_TEST
// Draws through the real flush_cb path (unlike a raw pre-LVGL gfxA/gfxB
// test), so a correct render here proves the SPLIT is correct - not just
// that both panels are wired up. See plan step 3.
static void build_seam_test_pattern() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Full-height vertical bar spanning both panels (virtual y 0-479).
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00ff00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_size(bar, 40, VDISP_H);
    lv_obj_set_pos(bar, 100, 0);

    // Box straddling the seam: virtual y 210-270, i.e. rows 210-239 on
    // panel A and rows 0-30 on panel B.
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xff00ff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_size(box, 60, 60);
    lv_obj_set_pos(box, 90, 210);
}
#endif
// ============================================================================
// END PLACEHOLDER
// ============================================================================

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=================================================================");
    Serial.println(" espDash XIAO DUAL ROUND GAUGE (2x GC9A01, shared SPI bus)");
    Serial.println(" Virtual display: 240x480 (panel A rows 0-239, panel B rows 240-479)");
    Serial.println("=================================================================");

    // ---- Wi-Fi off, ESP-NOW channel-locked --------------------------------
    // No Wi-Fi is ever attempted: no association, so no chance of parking the
    // radio on a channel that doesn't match the gateway's fixed
    // ESPDASH_ESPNOW_CHANNEL. Order matters here - disconnect(true, true)'s
    // second argument stops the radio outright, so WiFi.mode(WIFI_STA) must
    // come AFTER it to restart it into STA mode; reversed, ESP-NOW fails
    // every send with ESP_ERR_ESPNOW_IF (found and fixed on the gateway the
    // same way, see xiao-round-gauge/src/main.cpp:505-518).
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // ---- Deassert BOTH chip selects before touching either panel ----------
    // Arduino_ESP32SPI::begin() drives its own CS high, but only its own - so
    // during gfxA->begin() panel B's CS would still be an unconfigured,
    // floating input. If it floats low, panel B latches panel A's entire init
    // command sequence and comes up misconfigured, with no obvious symptom
    // beyond "panel B looks wrong". Park both high first; begin() re-asserting
    // them high afterwards is harmless.
    pinMode(PIN_CS_A, OUTPUT);
    digitalWrite(PIN_CS_A, HIGH);
    pinMode(PIN_CS_B, OUTPUT);
    digitalWrite(PIN_CS_B, HIGH);

    // ---- Shared RST: pulse once, manually, before either panel begin()s ---
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, HIGH);
    delay(20);
    digitalWrite(PIN_RST, LOW);
    delay(20);
    digitalWrite(PIN_RST, HIGH);
    delay(120);

    pinMode(PIN_BL, OUTPUT);
    digitalWrite(PIN_BL, HIGH);

    gfxA->begin(SPI_HZ);
    gfxB->begin(SPI_HZ);
    gfxA->fillScreen(BLACK);
    gfxB->fillScreen(BLACK);

    // ---- LVGL: one 240x480 virtual display, split in flush_cb -------------
    lv_init();
    static lv_color_t lv_draw_buf[VDISP_W * 48];   // 240x48 partial buffer, SRAM only - no full framebuffer
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, lv_draw_buf, NULL, VDISP_W * 48);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = VDISP_W;
    disp_drv.ver_res = VDISP_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.monitor_cb = disp_monitor_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

#if XIAO_DUAL_SEAM_TEST
    build_seam_test_pattern();
#else
    build_placeholder_ui();
#endif

    // ---- ESP-NOW ------------------------------------------------------------
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
    }
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    uint32_t now = millis();

    // ---- ESP-NOW link supervision ------------------------------------------
    // No channel-hop loop here (unlike xiao-round-gauge's ESPDASH_NODE_WIFI=1
    // branch): this node never turns Wi-Fi on, so it never leaves the fixed
    // ESPDASH_ESPNOW_CHANNEL locked in setup() and there's nothing to scan.
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);
    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

    // Demo sweep - animates the gauges when no gateway is present. Ported
    // verbatim from xiao-round-gauge/src/main.cpp:639-649.
    EspDashTelemetry active_pkt = {0};
    if (is_demo) {
        float phase = now * 0.002f;
        active_pkt.rpm = (uint16_t)(3000 + sin(phase) * 2800 + sin(phase * 3.0f) * 500);
        active_pkt.speed_kmh_x10 = (uint16_t)((90 + sin(phase * 0.8f) * 40) * 10);
        active_pkt.water_temp_x10 = (int16_t)((92 + sin(phase * 0.2f) * 6) * 10);
        active_pkt.steering_deg = (int16_t)(sin(phase * 1.2f) * 180);
        active_pkt.throttle_pct = (uint8_t)(50 + sin(phase * 1.5f) * 45);
        active_pkt.brake_pct = (uint8_t)(max(0.0f, -sin(phase * 1.5f) * 80.0f));
        active_pkt.fuel_consumption_x10 = (uint8_t)(85 + sin(phase * 0.1f) * 20);
        active_pkt.battery_mv = (uint16_t)((13.0f + sin(phase * 0.8f) * 1.8f) * 1000); // Dynamic 11.2V - 14.8V sweep
        active_pkt.gear = (uint8_t)(5 + ((int)(now * 0.0004f) % 6));
        active_pkt.ambient_temp = (int8_t)(20 + sin(phase * 0.05f) * 8);
    } else {
        active_pkt = current_pkt;
    }

#if !XIAO_DUAL_SEAM_TEST
    update_placeholder_ui(active_pkt, link);
#endif
    lv_timer_handler();

    // ---- Periodic link + frame-rate health logger --------------------------
    static uint32_t last_link_log = 0, last_pkt_count = 0, last_frame_count = 0;
    if (now - last_link_log >= 2000) {
        uint32_t n = pkt_count;
        uint32_t f = g_frame_count;
        float hz = (n - last_pkt_count) * 1000.0f / (now - last_link_log);
        float fps = (f - last_frame_count) * 1000.0f / (now - last_link_log);
        last_link_log = now;
        last_pkt_count = n;
        last_frame_count = f;
        const char *st = (link == LINK_LIVE) ? "LIVE"
                       : (link == LINK_LOST) ? "LOST" : "SEARCHING";
        Serial.printf("[LINK] %s ch:%u rate:%.1fHz pkts:%lu gaps:%lu payload:%u fps:%.1f "
                      "rpm:%u spd:%.1f gear:%u thr:%u\n",
                      st, actual_channel(), hz, (unsigned long)n,
                      (unsigned long)pkt_gaps, current_payload_len, fps,
                      current_pkt.rpm, current_pkt.speed_kmh_x10 / 10.0f,
                      current_pkt.gear, current_pkt.throttle_pct);
    }

    delay(5);
}
