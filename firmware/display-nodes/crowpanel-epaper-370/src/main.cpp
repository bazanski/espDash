#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_mac.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <EspDashProto.h>
#include <EspDashSignals.h>

#include "epd/EPD.h"
#include "epd/EPD_GUI.h"

// =========================================================================
// CROWPANEL 3.7" E-PAPER HARDWARE MAP (ESP32-S3-WROOM-1-N8R8, UC8253)
// =========================================================================
#define PIN_EPD_PWR   7
#define PIN_LED_PWR   41

#define PIN_BTN_EXIT  1   // Back button
#define PIN_BTN_HOME  2   // Menu button
#define PIN_DIAL_NEXT 4   // Rotary dial down / next
#define PIN_DIAL_OK   5   // Rotary dial push
#define PIN_DIAL_PRV  6   // Rotary dial up / prev

// Canvas dimensions in Landscape (rotation = 180 or 0)
#define CANVAS_W 416
#define CANVAS_H 240

// =========================================================================
// TELEMETRY & LINK SUPERVISION
// =========================================================================
static EspDashTelemetry current_pkt = {0};
static uint16_t current_payload_len = 0;
static volatile uint32_t last_pkt_rx_time = 0;
static volatile uint32_t pkt_count = 0;
static portMUX_TYPE pkt_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t pkt_gaps = 0;
static uint16_t last_seq = 0xFFFF;
static volatile bool ever_linked = false;

static uint8_t current_channel = ESPDASH_ESPNOW_CHANNEL;
static uint8_t home_channel = ESPDASH_ESPNOW_CHANNEL;
static volatile uint8_t rx_channel = 0;
static bool scanning = false;
static uint8_t scan_start = 0;
static uint32_t last_channel_hop = 0;
static uint32_t quiet_since = 0;

#define LINK_TIMEOUT_MS       1500
#define PARKED_AFTER_MS       10000
#define SCAN_AFTER_SILENCE_MS 10000
#define SCAN_DWELL_MS         400

enum LinkState {
    LINK_LIVE = 0,
    LINK_SEARCHING,
    LINK_LOST,
    LINK_PARKED
};

// =========================================================================
// DISPLAY & DASHBOARD STATE
// =========================================================================
static uint8_t ImageBW[12480];

enum DashPage {
    PAGE_TRIP = 0,
    PAGE_HEALTH,
    PAGE_PARKED,
    PAGE_COUNT
};

static DashPage active_page = PAGE_TRIP;
static bool flip_180 = false;          // false => rotate 180 (normal landscape), true => rotate 0
static bool demo_mode = true;          // auto-runs while searching; cancelled on first live packet
static bool force_clean_refresh = true;
static bool needs_refresh = true;
static bool parked_rendered = false;

static uint32_t refresh_count = 0;
static uint32_t partial_since_clean = 0;
static uint32_t last_refresh_ms = 0;

static uint16_t odo_base = 0xFFFF;
static bool     odo_live_seeded = false;
static bool     stats_live_seeded = false;
static uint32_t session_start_ms = 0;
static uint32_t last_rx_rate_ms = 0;
static uint32_t last_rx_rate_pkts = 0;
static float    rx_hz = 0.0f;

// Session min/max tracking and service baselines
struct SessionStats {
    int16_t  max_coolant_x10;
    uint16_t max_speed_x10;
    uint16_t max_rpm;
    uint16_t min_batt_mv;
    uint16_t max_batt_mv;
    int8_t   min_cabin_c;
    int8_t   max_cabin_c;
    uint16_t max_wheel_slip_x10;
    bool     has_cabin;
};

static SessionStats g_stats = {
    .max_coolant_x10 = -400,
    .max_speed_x10 = 0,
    .max_rpm = 0,
    .min_batt_mv = 0xFFFF,
    .max_batt_mv = 0,
    .min_cabin_c = 127,
    .max_cabin_c = -128,
    .max_wheel_slip_x10 = 0,
    .has_cabin = false
};

// Last displayed snapshot for change-threshold gating (anti-flicker)
struct RenderedSnapshot {
    DashPage page;
    LinkState link;
    bool flip_180;
    uint8_t fuel_pct;
    int16_t coolant_x10;
    uint16_t batt_mv;
    int8_t ambient;
    int8_t cabin;
    uint8_t fan_speed;
    uint8_t lights;
    uint8_t gear;
    uint8_t gear_num;
    uint8_t fuel_avg_x10;
    uint8_t flags;
    uint8_t flags2;
    uint16_t odo_50m;
};

static RenderedSnapshot g_last_drawn = {};
static EspDashTelemetry g_last_valid_pkt = {};
static uint16_t g_last_valid_len = sizeof(EspDashTelemetry);

// =========================================================================
// ESP-NOW RECEIVE & CHANNEL SUPERVISION
// =========================================================================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    uint16_t plen = 0, seq = 0;
    const EspDashTelemetry *t = espdash_parse(incomingData, len, &plen, &seq);
    if (!t) return;

    EspDashTelemetry tmp;
    memset(&tmp, 0, sizeof(tmp));
    uint16_t copy = plen < sizeof(EspDashTelemetry) ? plen : sizeof(EspDashTelemetry);
    memcpy(&tmp, t, copy);

    uint8_t ch = 0;
    wifi_second_chan_t sec;
    if (esp_wifi_get_channel(&ch, &sec) != ESP_OK) ch = 0;

    portENTER_CRITICAL(&pkt_mux);
    current_pkt = tmp;
    current_payload_len = plen;
    rx_channel = ch;
    last_pkt_rx_time = millis();
    portEXIT_CRITICAL(&pkt_mux);

    pkt_count++;
    ever_linked = true;

    if (seq != (uint16_t)(last_seq + 1) && last_seq != 0xFFFF) {
        uint16_t missed = (uint16_t)(seq - last_seq - 1);
        if (missed < 1000) pkt_gaps += missed;
    }
    last_seq = seq;
}

static void init_espnow() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.printf("[ESP-NOW] Initialized on Channel %d\n", ESPDASH_ESPNOW_CHANNEL);
    } else {
        Serial.println("[ESP-NOW] Init FAILED!");
    }
}

static LinkState update_link_state(uint32_t now, uint32_t rx_time) {
    bool live = ever_linked && ((int32_t)(now - rx_time) <= LINK_TIMEOUT_MS);

    if (live) {
        if (scanning) {
            scanning = false;
            home_channel = rx_channel ? rx_channel : current_channel;
            current_channel = home_channel;
            esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
            Serial.printf("[ESP-NOW] Locked to gateway on ch %u\n", home_channel);
        } else if (rx_channel && rx_channel != home_channel) {
            home_channel = rx_channel;
        }
        quiet_since = now;
        demo_mode = false;
        parked_rendered = false;
        return LINK_LIVE;
    }

    if (!scanning) {
        if ((now - quiet_since) >= SCAN_AFTER_SILENCE_MS) {
            scanning = true;
            scan_start = home_channel;
            last_channel_hop = now - SCAN_DWELL_MS;
        }
    } else if ((now - last_channel_hop) >= SCAN_DWELL_MS) {
        last_channel_hop = now;
        uint8_t next = (current_channel % 13) + 1;
        if (next == scan_start) {
            scanning = false;
            current_channel = home_channel;
            quiet_since = now;
        } else {
            current_channel = next;
        }
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
    }

    if (!ever_linked) return LINK_SEARCHING;
    if ((int32_t)(now - rx_time) >= PARKED_AFTER_MS) return LINK_PARKED;
    return LINK_LOST;
}

// =========================================================================
// TELEMETRY HELPERS & BENCH DEMO GENERATOR
// =========================================================================
static EspDashTelemetry build_demo_packet(uint32_t now) {
    EspDashTelemetry p = {0};
    float t = now * 0.001f;
    // Slow-moving realistic vehicle cycle so e-ink updates cleanly every 5s
    int step = (int)(now / 5000);
    float cyc = sinf(t * 0.08f);

    p.rpm = (uint16_t)(1950 + (step % 5) * 320);
    p.speed_kmh_x10 = (uint16_t)(640 + (step % 7) * 45);
    p.water_temp_x10 = (int16_t)(845 + (int)(cyc * 65.0f)); // 78.0 .. 91.0 C
    p.battery_mv = (uint16_t)(14000 + (step % 4) * 100);     // 14.0 .. 14.3 V
    p.gear = 3;                                              // D
    p.gear_num = (uint8_t)(3 + (step % 3));                  // 3..5
    p.fuel_consumption_x10 = (uint8_t)(82 + (step % 9));     // 8.2 .. 9.0 L/100km
    p.steering_deg = (int16_t)(cyc * 18.0f);
    p.ambient_temp = (int8_t)(25 + (step % 3));
    p.cabin_temp = (int8_t)(21 + (step % 2));
    p.throttle_pct = 24;
    p.brake_pct = 0;
    p.timestamp_ms = now;
    p.wheel_fl_x10 = p.speed_kmh_x10;
    p.wheel_fr_x10 = p.speed_kmh_x10;
    p.wheel_rl_x10 = p.speed_kmh_x10;
    p.wheel_rr_x10 = p.speed_kmh_x10;

    // Fuel descends smoothly from 84% to 12% over demo steps, triggering LOW_FUEL <= 13%
    int fuel = 84 - ((step * 3) % 76);
    p.fuel_level_pct = (uint8_t)fuel;
    p.odo_50m = (uint16_t)(1000 + step * 18);                // +0.9 km per 5s step
    p.fan_speed = (uint8_t)(2 + (step % 4));                 // 2..5
    p.lights = (uint8_t)(step % 4);                          // DRL/POS/LOW/HIGH
    p.flags = ESPDASH_FLAG_ENGINE_RUNNING |
              (p.water_temp_x10 > 1050 ? ESPDASH_FLAG_OVERHEAT : 0);
    p.flags2 = ESPDASH_FLAG2_FUEL_VALID | ESPDASH_FLAG2_ODO_VALID |
               ((step % 6 < 4) ? ESPDASH_FLAG2_ECON : 0) |
               (fuel <= 13 ? ESPDASH_FLAG2_LOW_FUEL : 0);
    return p;
}

static void update_session_stats(const EspDashTelemetry &pkt, uint16_t plen) {
    if (pkt.water_temp_x10 > g_stats.max_coolant_x10) g_stats.max_coolant_x10 = pkt.water_temp_x10;
    if (pkt.speed_kmh_x10 > g_stats.max_speed_x10) g_stats.max_speed_x10 = pkt.speed_kmh_x10;
    if (pkt.rpm > g_stats.max_rpm) g_stats.max_rpm = pkt.rpm;
    if (pkt.battery_mv > 0) {
        if (pkt.battery_mv < g_stats.min_batt_mv) g_stats.min_batt_mv = pkt.battery_mv;
        if (pkt.battery_mv > g_stats.max_batt_mv) g_stats.max_batt_mv = pkt.battery_mv;
    }
    if (ESPDASH_HAS(plen, cabin_temp)) {
        g_stats.has_cabin = true;
        if (pkt.cabin_temp < g_stats.min_cabin_c) g_stats.min_cabin_c = pkt.cabin_temp;
        if (pkt.cabin_temp > g_stats.max_cabin_c) g_stats.max_cabin_c = pkt.cabin_temp;
    }
    if (ESPDASH_HAS(plen, wheel_rr_x10)) {
        uint16_t wmin = pkt.wheel_fl_x10;
        uint16_t wmax = pkt.wheel_fl_x10;
        uint16_t ws[3] = { pkt.wheel_fr_x10, pkt.wheel_rl_x10, pkt.wheel_rr_x10 };
        for (int i = 0; i < 3; i++) {
            if (ws[i] < wmin) wmin = ws[i];
            if (ws[i] > wmax) wmax = ws[i];
        }
        uint16_t slip = wmax - wmin;
        if (slip > g_stats.max_wheel_slip_x10) g_stats.max_wheel_slip_x10 = slip;
    }
}

static float compute_trip_km(const EspDashTelemetry &pkt, uint16_t plen) {
    if (!ESPDASH_HAS(plen, odo_50m) || !(pkt.flags2 & ESPDASH_FLAG2_ODO_VALID)) return 0.0f;
    if (odo_base == 0xFFFF) return 0.0f;
    uint16_t delta = (uint16_t)(pkt.odo_50m - odo_base);
    return delta * 0.05f;
}

static int compute_dte_km(const EspDashTelemetry &pkt, uint16_t plen) {
    if (!ESPDASH_HAS(plen, flags2) || !(pkt.flags2 & ESPDASH_FLAG2_FUEL_VALID)) return -1;
    if (pkt.fuel_consumption_x10 == 0) return -1;
    float litres = pkt.fuel_level_pct * 0.5f;
    float l_per_100 = pkt.fuel_consumption_x10 * 0.1f;
    return (int)lroundf((litres / l_per_100) * 100.0f);
}

// =========================================================================
// DRAWING PRIMITIVES & UI RENDERING (416x240 MONOCHROME E-PAPER)
// =========================================================================
static void draw_inverted_pill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                               const char *txt, uint16_t font_sz, bool inverted) {
    if (inverted) {
        EPD_FillRect(x1, y1, x2, y2, BLACK);
        EPD_ShowString(x1 + 4, y1 + 2, txt, font_sz, WHITE);
    } else {
        EPD_DrawRectangle(x1, y1, x2, y2, BLACK, 0);
        EPD_ShowString(x1 + 4, y1 + 2, txt, font_sz, BLACK);
    }
}

static void draw_header(const EspDashTelemetry &pkt, uint16_t plen, LinkState link) {
    EPD_FillRect(0, 0, CANVAS_W - 1, 23, BLACK);
    EPD_ShowString(6, 4, "espDash", 16, WHITE);

    const char *pg_lbl = (active_page == PAGE_TRIP)   ? "[1/3 TRIP]" :
                         (active_page == PAGE_HEALTH) ? "[2/3 HEALTH]" :
                                                        "[3/3 PARKED]";
    EPD_ShowString(72, 4, pg_lbl, 16, WHITE);

    char link_buf[16];
    if (demo_mode && link != LINK_LIVE) {
        snprintf(link_buf, sizeof(link_buf), "[DEMO CH%u]", current_channel);
    } else if (link == LINK_LIVE) {
        snprintf(link_buf, sizeof(link_buf), "[LIVE CH%u]", current_channel);
    } else if (link == LINK_PARKED) {
        snprintf(link_buf, sizeof(link_buf), "[PARKED]");
    } else if (link == LINK_LOST) {
        snprintf(link_buf, sizeof(link_buf), "[LOST CH%u]", current_channel);
    } else {
        snprintf(link_buf, sizeof(link_buf), "[SCAN CH%u]", current_channel);
    }
    EPD_ShowString(216, 4, link_buf, 16, WHITE);

    // Inverted white pill on right for Gear Selector + Engaged Gear
    const char *sel = (pkt.gear < 5) ? espdash_gear_labels[pkt.gear] : "?";
    char gear_buf[8];
    if (ESPDASH_HAS(plen, gear_num) && pkt.gear_num > 0) {
        snprintf(gear_buf, sizeof(gear_buf), "%s%u", sel, pkt.gear_num);
    } else {
        snprintf(gear_buf, sizeof(gear_buf), " %s", sel);
    }
    EPD_FillRect(356, 2, 411, 21, WHITE);
    EPD_ShowString(360, 4, "G:", 16, BLACK);
    EPD_ShowString(376, 4, gear_buf, 16, BLACK);
}

static void draw_footer(uint32_t now) {
    EPD_DrawLine(0, 222, CANVAS_W - 1, 222, BLACK);
    char buf[72];
    snprintf(buf, sizeof(buf), "DIAL:Page  OK:Clean  MENU:Demo  BACK:Flip180  #%lu",
             (unsigned long)(refresh_count + 1));
    EPD_ShowString(6, 226, buf, 12, BLACK);
}

static void draw_page_trip(const EspDashTelemetry &pkt, uint16_t plen, LinkState link, uint32_t now) {
    char buf[40];

    // ---- 1. LEFT HERO CARD: FUEL & RANGE (x=4..204, y=27..149) ----
    EPD_DrawRectangle(4, 27, 204, 149, BLACK, 0);
    EPD_ShowString(10, 30, "FUEL TANK", 12, BLACK);

    bool fuel_valid = ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_FUEL_VALID);
    bool low_fuel = ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_LOW_FUEL);

    if (low_fuel) {
        draw_inverted_pill(122, 28, 201, 43, "LOW FUEL", 12, true);
    } else if (!fuel_valid) {
        EPD_ShowString(138, 30, "NO SENSOR", 12, BLACK);
    } else {
        EPD_ShowString(146, 30, "50L TANK", 12, BLACK);
    }
    EPD_DrawLine(4, 45, 204, 45, BLACK);

    if (fuel_valid) {
        snprintf(buf, sizeof(buf), "%3u", pkt.fuel_level_pct);
        EPD_ShowString(8, 48, buf, 48, BLACK);
        EPD_ShowString(84, 62, "%", 24, BLACK);

        float litres = pkt.fuel_level_pct * 0.5f;
        snprintf(buf, sizeof(buf), "%.1fL", litres);
        EPD_ShowString(118, 52, buf, 24, BLACK);
        EPD_ShowString(118, 79, low_fuel ? "RESERVE!" : "REMAIN", 16, BLACK);
    } else {
        EPD_ShowString(12, 48, "--", 48, BLACK);
        EPD_ShowString(64, 62, "%", 24, BLACK);
        EPD_ShowString(118, 52, "--.-L", 24, BLACK);
    }

    // 10-segment graphical fuel bar (x=10..198, y=100..115)
    EPD_DrawRectangle(10, 100, 198, 115, BLACK, 0);
    if (fuel_valid) {
        int segs = (pkt.fuel_level_pct + 5) / 10;
        if (segs > 10) segs = 10;
        for (int i = 0; i < 10; i++) {
            uint16_t sx1 = 13 + i * 18;
            uint16_t sx2 = sx1 + 15;
            if (i < segs) {
                EPD_FillRect(sx1, 103, sx2, 112, BLACK);
            } else {
                EPD_DrawRectangle(sx1, 103, sx2, 112, BLACK, 0);
            }
        }
    }
    EPD_ShowString(10, 118, "E    1/4    1/2    3/4    F", 12, BLACK);

    int dte = compute_dte_km(pkt, plen);
    if (dte >= 0) {
        snprintf(buf, sizeof(buf), "DTE:%3dkm  AVG:%.1fL", dte, pkt.fuel_consumption_x10 * 0.1f);
    } else {
        snprintf(buf, sizeof(buf), "DTE: --km  AVG:%.1fL", pkt.fuel_consumption_x10 * 0.1f);
    }
    EPD_ShowString(10, 134, buf, 12, BLACK);

    // ---- 2. TOP-RIGHT CARD: TRIP COMPUTER (x=210..411, y=27..149) ----
    EPD_DrawRectangle(210, 27, 411, 149, BLACK, 0);
    EPD_ShowString(216, 30, "TRIP COMP", 12, BLACK);

    bool econ_on = ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_ECON);
    bool sport_on = ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_SPORT);
    if (econ_on) draw_inverted_pill(318, 28, 358, 43, "ECON", 12, true);
    else         draw_inverted_pill(318, 28, 358, 43, "ECON", 12, false);
    if (sport_on) draw_inverted_pill(362, 28, 408, 43, "SPRT", 12, true);
    else          draw_inverted_pill(362, 28, 408, 43, "SPRT", 12, false);
    EPD_DrawLine(210, 45, 411, 45, BLACK);

    float trip_km = compute_trip_km(pkt, plen);
    uint32_t elapsed_s = (now - session_start_ms) / 1000;
    float avg_spd = (elapsed_s > 5) ? (trip_km * 3600.0f / (float)elapsed_s) : 0.0f;
    float fuel_used = trip_km * (pkt.fuel_consumption_x10 * 0.1f) / 100.0f;

    snprintf(buf, sizeof(buf), "%.1fkm", trip_km);
    EPD_ShowString(216, 49, buf, 24, BLACK);
    EPD_ShowString(326, 49, "AVG SPD", 12, BLACK);
    snprintf(buf, sizeof(buf), "%.0f km/h", avg_spd);
    EPD_ShowString(326, 62, buf, 12, BLACK);

    EPD_DrawLine(216, 79, 405, 79, BLACK);

    EPD_ShowString(216, 83, "TRIP AVG", 12, BLACK);
    snprintf(buf, sizeof(buf), "%.1f L/100", pkt.fuel_consumption_x10 * 0.1f);
    EPD_ShowString(216, 97, buf, 16, BLACK);

    EPD_ShowString(326, 83, "FUEL USED", 12, BLACK);
    snprintf(buf, sizeof(buf), "%.2f L", fuel_used);
    EPD_ShowString(326, 97, buf, 16, BLACK);

    EPD_DrawLine(216, 117, 405, 117, BLACK);

    uint32_t hh = elapsed_s / 3600;
    uint32_t mm = (elapsed_s % 3600) / 60;
    uint32_t ss = elapsed_s % 60;
    snprintf(buf, sizeof(buf), "TIME:%02lu:%02lu:%02lu",
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    EPD_ShowString(216, 126, buf, 16, BLACK);

    snprintf(buf, sizeof(buf), "%u km/h", pkt.speed_kmh_x10 / 10);
    EPD_ShowString(342, 129, buf, 12, BLACK);

    // ---- 3. BOTTOM 4-TILE STRIP (y=154..218) ----
    // Tile 1: COOLANT (x=4..102)
    EPD_DrawRectangle(4, 154, 102, 218, BLACK, 0);
    EPD_ShowString(10, 158, "COOLANT", 12, BLACK);
    float cool_c = pkt.water_temp_x10 * 0.1f;
    snprintf(buf, sizeof(buf), "%.1fC", cool_c);
    EPD_ShowString(10, 174, buf, 24, BLACK);
    if (cool_c >= 105.0f || (pkt.flags & ESPDASH_FLAG_OVERHEAT)) {
        draw_inverted_pill(8, 200, 98, 215, "OVERHEAT!", 12, true);
    } else if (cool_c < 60.0f) {
        EPD_ShowString(10, 202, "COLD / WARMUP", 12, BLACK);
    } else {
        EPD_ShowString(10, 202, "OPTIMAL TEMP", 12, BLACK);
    }

    // Tile 2: 12V BATTERY (x=106..204)
    EPD_DrawRectangle(106, 154, 204, 218, BLACK, 0);
    EPD_ShowString(112, 158, "12V BATTERY", 12, BLACK);
    if (pkt.battery_mv > 0) {
        float bv = pkt.battery_mv * 0.001f;
        snprintf(buf, sizeof(buf), "%.1fV", bv);
        EPD_ShowString(112, 174, buf, 24, BLACK);
        if (bv < 12.0f) {
            draw_inverted_pill(110, 200, 200, 215, "LOW BATT!", 12, true);
        } else if (bv >= 13.4f) {
            EPD_ShowString(112, 202, "ALT CHARGING", 12, BLACK);
        } else {
            EPD_ShowString(112, 202, "RAIL NORMAL", 12, BLACK);
        }
    } else {
        EPD_ShowString(112, 174, "--.-V", 24, BLACK);
        EPD_ShowString(112, 202, "NO INA228", 12, BLACK);
    }

    // Tile 3: CABIN & OUTSIDE TEMPS (x=208..306)
    EPD_DrawRectangle(208, 154, 306, 218, BLACK, 0);
    EPD_ShowString(214, 158, "TEMPS IN/OUT", 12, BLACK);
    if (ESPDASH_HAS(plen, cabin_temp)) {
        snprintf(buf, sizeof(buf), "IN :%3d C", pkt.cabin_temp);
    } else {
        snprintf(buf, sizeof(buf), "IN : -- C");
    }
    EPD_ShowString(214, 176, buf, 16, BLACK);
    snprintf(buf, sizeof(buf), "OUT:%3d C", pkt.ambient_temp);
    EPD_ShowString(214, 196, buf, 16, BLACK);

    // Tile 4: CLIMATE FAN & LIGHTS (x=310..411)
    EPD_DrawRectangle(310, 154, 411, 218, BLACK, 0);
    EPD_ShowString(316, 158, "HVAC & LIGHTS", 12, BLACK);
    if (ESPDASH_HAS(plen, fan_speed)) {
        snprintf(buf, sizeof(buf), "FAN:%u/7", pkt.fan_speed);
    } else {
        snprintf(buf, sizeof(buf), "FAN: --");
    }
    EPD_ShowString(316, 176, buf, 16, BLACK);

    const char *lts = "--";
    if (ESPDASH_HAS(plen, lights) && pkt.lights < 4) {
        lts = espdash_lights_labels[pkt.lights];
    }
    snprintf(buf, sizeof(buf), "LTS:%s", lts);
    EPD_ShowString(316, 196, buf, 16, BLACK);
}

static void draw_page_health(const EspDashTelemetry &pkt, uint16_t plen, uint32_t now) {
    char buf[44];

    // ---- LEFT CARD: SESSION PEAKS (x=4..204, y=27..218) ----
    EPD_DrawRectangle(4, 27, 204, 218, BLACK, 0);
    EPD_ShowString(10, 30, "SESSION PEAKS", 12, BLACK);
    EPD_DrawLine(4, 45, 204, 45, BLACK);

    snprintf(buf, sizeof(buf), "MAX COOL: %.1f C", g_stats.max_coolant_x10 * 0.1f);
    EPD_ShowString(10, 52, buf, 16, BLACK);

    snprintf(buf, sizeof(buf), "MAX SPD : %.0f km/h", g_stats.max_speed_x10 * 0.1f);
    EPD_ShowString(10, 74, buf, 16, BLACK);

    snprintf(buf, sizeof(buf), "MAX RPM : %u", g_stats.max_rpm);
    EPD_ShowString(10, 96, buf, 16, BLACK);

    if (g_stats.max_batt_mv > 0) {
        snprintf(buf, sizeof(buf), "BATT:%4.1f-%4.1fV",
                 g_stats.min_batt_mv * 0.001f, g_stats.max_batt_mv * 0.001f);
    } else {
        snprintf(buf, sizeof(buf), "BATT: --.- / --.-V");
    }
    EPD_ShowString(10, 118, buf, 16, BLACK);

    if (g_stats.has_cabin) {
        snprintf(buf, sizeof(buf), "CABIN:%2d..%2d C", g_stats.min_cabin_c, g_stats.max_cabin_c);
    } else {
        snprintf(buf, sizeof(buf), "CABIN: --..-- C");
    }
    EPD_ShowString(10, 140, buf, 16, BLACK);

    float trip_km = compute_trip_km(pkt, plen);
    float used_l = trip_km * (pkt.fuel_consumption_x10 * 0.1f) / 100.0f;
    snprintf(buf, sizeof(buf), "BURNED: %.2f L", used_l);
    EPD_ShowString(10, 162, buf, 16, BLACK);

    snprintf(buf, sizeof(buf), "WHL SLP: %.1f km/h", g_stats.max_wheel_slip_x10 * 0.1f);
    EPD_ShowString(10, 184, buf, 16, BLACK);

    // ---- RIGHT TOP: SERVICE & WARNINGS (x=210..411, y=27..122) ----
    EPD_DrawRectangle(210, 27, 411, 122, BLACK, 0);
    EPD_ShowString(216, 30, "SERVICE & ALERTS", 12, BLACK);
    EPD_DrawLine(210, 45, 411, 45, BLACK);

    // Maintenance counters subtract session distance from typical intervals
    int oil_rem = (int)lroundf(5000.0f - trip_km);
    int tire_rem = (int)lroundf(10000.0f - trip_km);
    snprintf(buf, sizeof(buf), "OIL CHG : %d km", oil_rem);
    EPD_ShowString(216, 51, buf, 16, BLACK);

    snprintf(buf, sizeof(buf), "TIRES   : %d km", tire_rem);
    EPD_ShowString(216, 71, buf, 16, BLACK);

    bool any_warn = (pkt.flags & (ESPDASH_FLAG_OVERHEAT | ESPDASH_FLAG_CEL |
                                  ESPDASH_FLAG_VSA_WARNING | ESPDASH_FLAG_ABS_ACTIVE)) ||
                    (ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_LOW_FUEL));
    if (any_warn) {
        draw_inverted_pill(216, 94, 405, 114, "ALERT ACTIVE!", 16, true);
    } else {
        EPD_ShowString(216, 95, "ALERTS  : ALL OK", 16, BLACK);
    }

    // ---- RIGHT BOTTOM: NODE & RADIO LINK (x=210..411, y=127..218) ----
    EPD_DrawRectangle(210, 127, 411, 218, BLACK, 0);
    EPD_ShowString(216, 130, "RADIO & NODE INFO", 12, BLACK);
    EPD_DrawLine(210, 145, 411, 145, BLACK);

    snprintf(buf, sizeof(buf), "PROTO: v2.%u (%uB)", ESPDASH_PROTO_MINOR, plen);
    EPD_ShowString(216, 150, buf, 12, BLACK);

    snprintf(buf, sizeof(buf), "LINK : CH%u  %.1f Hz", current_channel, rx_hz);
    EPD_ShowString(216, 165, buf, 12, BLACK);

    snprintf(buf, sizeof(buf), "PKTS : %lu  DROP:%lu",
             (unsigned long)pkt_count, (unsigned long)pkt_gaps);
    EPD_ShowString(216, 180, buf, 12, BLACK);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, sizeof(buf), "MAC  : %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    EPD_ShowString(216, 196, buf, 12, BLACK);
}

static void draw_page_parked(const EspDashTelemetry &pkt, uint16_t plen) {
    char buf[52];

    // Top Zero-Power Snapshot Hero Banner (x=4..411, y=27..90)
    EPD_DrawRectangle(4, 27, 411, 90, BLACK, 0);
    EPD_FillRect(4, 27, 411, 44, BLACK);
    EPD_ShowString(10, 30, "PARKED SNAPSHOT (0mA E-INK RETENTION)", 12, WHITE);

    int dte = compute_dte_km(pkt, plen);
    bool fuel_valid = ESPDASH_HAS(plen, flags2) && (pkt.flags2 & ESPDASH_FLAG2_FUEL_VALID);
    if (fuel_valid) {
        snprintf(buf, sizeof(buf), "FUEL:%u%% (%.1fL)  DTE:%dkm",
                 pkt.fuel_level_pct, pkt.fuel_level_pct * 0.5f, dte >= 0 ? dte : 0);
    } else {
        snprintf(buf, sizeof(buf), "FUEL: --%%  DTE: --km");
    }
    EPD_ShowString(10, 48, buf, 24, BLACK);

    float trip_km = compute_trip_km(pkt, plen);
    if (pkt.battery_mv > 0) {
        snprintf(buf, sizeof(buf), "TRIP:%.1fkm  AVG:%.1f  H2O:%.0fC  BAT:%.1fV",
                 trip_km, pkt.fuel_consumption_x10 * 0.1f,
                 pkt.water_temp_x10 * 0.1f, pkt.battery_mv * 0.001f);
    } else {
        snprintf(buf, sizeof(buf), "TRIP:%.1fkm  AVG:%.1f  H2O:%.0fC  BAT:--.-V",
                 trip_km, pkt.fuel_consumption_x10 * 0.1f, pkt.water_temp_x10 * 0.1f);
    }
    EPD_ShowString(10, 74, buf, 12, BLACK);

    // Bottom Signal Catalog Matrix powered directly by EspDashSignals.h
    EPD_DrawRectangle(4, 95, 411, 218, BLACK, 0);
    EPD_ShowString(10, 98, "CATALOG MATRIX (EspDashSignals.h)", 12, BLACK);
    EPD_DrawLine(4, 113, 411, 113, BLACK);
    EPD_DrawLine(208, 113, 208, 218, BLACK);

    static const EspDashSignalId kLeftCol[5] = {
        ESPDASH_SIG_FUEL_LEVEL,
        ESPDASH_SIG_FUEL_LITRES,
        ESPDASH_SIG_FUEL_AVG,
        ESPDASH_SIG_ODO_KM,
        ESPDASH_SIG_COOLANT,
    };
    static const EspDashSignalId kRightCol[5] = {
        ESPDASH_SIG_CABIN_TEMP,
        ESPDASH_SIG_AMBIENT,
        ESPDASH_SIG_FAN_SPEED,
        ESPDASH_SIG_LIGHTS,
        ESPDASH_SIG_ECON,
    };

    for (int i = 0; i < 5; i++) {
        uint16_t ry = 119 + i * 19;
        char val[16];

        const EspDashSignalInfo *li = espdash_signal_info(kLeftCol[i]);
        espdash_signal_format(&pkt, plen, kLeftCol[i], odo_base, val, sizeof(val));
        snprintf(buf, sizeof(buf), "%-6s: %s %s", li->label, val, li->unit);
        EPD_ShowString(12, ry, buf, 16, BLACK);

        const EspDashSignalInfo *ri = espdash_signal_info(kRightCol[i]);
        espdash_signal_format(&pkt, plen, kRightCol[i], odo_base, val, sizeof(val));
        snprintf(buf, sizeof(buf), "%-6s: %s %s", ri->label, val, ri->unit);
        EPD_ShowString(216, ry, buf, 16, BLACK);
    }
}

static void render_epaper_screen(const EspDashTelemetry &pkt, uint16_t plen,
                                 LinkState link, uint32_t now) {
    uint16_t rot = flip_180 ? 0 : 180;
    Paint_NewImage(ImageBW, EPD_W, EPD_H, rot, WHITE);
    Paint_Clear(WHITE);

    draw_header(pkt, plen, link);
    if (active_page == PAGE_TRIP) {
        draw_page_trip(pkt, plen, link, now);
    } else if (active_page == PAGE_HEALTH) {
        draw_page_health(pkt, plen, now);
    } else {
        draw_page_parked(pkt, plen);
    }
    draw_footer(now);

    digitalWrite(PIN_LED_PWR, HIGH);
    EPD_ClearBusyTimeout();

    uint32_t t0 = millis();
    if (force_clean_refresh || partial_since_clean >= 15) {
        Serial.println("[EPD] Full clean refresh...");
        EPD_FastInit();
        EPD_Display_Clear();
        EPD_Update();
        EPD_PartInit();
        EPD_Display(ImageBW);
        EPD_Update();
        EPD_DeepSleep();
        force_clean_refresh = false;
        partial_since_clean = 0;
    } else {
        Serial.println("[EPD] Partial refresh...");
        EPD_PartInit();
        EPD_Display(ImageBW);
        EPD_Update();
        EPD_DeepSleep();
        partial_since_clean++;
    }
    digitalWrite(PIN_LED_PWR, LOW);

    refresh_count++;
    last_refresh_ms = millis();
    Serial.printf("[EPD] Refresh #%lu complete in %lu ms (busy_timeout=%s)\n",
                  (unsigned long)refresh_count,
                  (unsigned long)(last_refresh_ms - t0),
                  EPD_BusyTimedOut() ? "YES" : "NO");

    g_last_drawn.page = active_page;
    g_last_drawn.link = link;
    g_last_drawn.flip_180 = flip_180;
    g_last_drawn.fuel_pct = pkt.fuel_level_pct;
    g_last_drawn.coolant_x10 = pkt.water_temp_x10;
    g_last_drawn.batt_mv = pkt.battery_mv;
    g_last_drawn.ambient = pkt.ambient_temp;
    g_last_drawn.cabin = pkt.cabin_temp;
    g_last_drawn.fan_speed = pkt.fan_speed;
    g_last_drawn.lights = pkt.lights;
    g_last_drawn.gear = pkt.gear;
    g_last_drawn.gear_num = pkt.gear_num;
    g_last_drawn.fuel_avg_x10 = pkt.fuel_consumption_x10;
    g_last_drawn.flags = pkt.flags;
    g_last_drawn.flags2 = pkt.flags2;
    g_last_drawn.odo_50m = pkt.odo_50m;
}

static bool slow_data_changed(const EspDashTelemetry &pkt, LinkState link) {
    if (g_last_drawn.page != active_page) return true;
    if (g_last_drawn.link != link) return true;
    if (g_last_drawn.flip_180 != flip_180) return true;
    if (g_last_drawn.gear != pkt.gear || g_last_drawn.gear_num != pkt.gear_num) return true;
    if (g_last_drawn.flags != pkt.flags || g_last_drawn.flags2 != pkt.flags2) return true;
    if (g_last_drawn.lights != pkt.lights || g_last_drawn.fan_speed != pkt.fan_speed) return true;
    if (abs((int)pkt.fuel_level_pct - (int)g_last_drawn.fuel_pct) >= 1) return true;
    if (abs((int)pkt.water_temp_x10 - (int)g_last_drawn.coolant_x10) >= 10) return true;
    if (abs((int)pkt.battery_mv - (int)g_last_drawn.batt_mv) >= 100) return true;
    if (abs((int)pkt.ambient_temp - (int)g_last_drawn.ambient) >= 1) return true;
    if (abs((int)pkt.cabin_temp - (int)g_last_drawn.cabin) >= 1) return true;
    if (abs((int)pkt.fuel_consumption_x10 - (int)g_last_drawn.fuel_avg_x10) >= 1) return true;
    if ((uint16_t)(pkt.odo_50m - g_last_drawn.odo_50m) >= 2) return true; // >= 0.1 km
    return false;
}

// =========================================================================
// PHYSICAL BUTTONS & SERIAL CONSOLE
// =========================================================================
struct DebouncedBtn {
    uint8_t pin;
    bool    last_raw;
    bool    stable;
    uint32_t changed_ms;
    const char *name;
};

static DebouncedBtn g_btns[5] = {
    { PIN_BTN_EXIT,  true, true, 0, "BACK" },
    { PIN_BTN_HOME,  true, true, 0, "MENU" },
    { PIN_DIAL_NEXT, true, true, 0, "NEXT" },
    { PIN_DIAL_OK,   true, true, 0, "OK"   },
    { PIN_DIAL_PRV,  true, true, 0, "PREV" },
};

static void poll_buttons(uint32_t now) {
    for (int i = 0; i < 5; i++) {
        bool raw = digitalRead(g_btns[i].pin);
        if (raw != g_btns[i].last_raw) {
            g_btns[i].last_raw = raw;
            g_btns[i].changed_ms = now;
        }
        if ((now - g_btns[i].changed_ms) >= 35 && raw != g_btns[i].stable) {
            g_btns[i].stable = raw;
            if (!raw) { // pressed (active LOW)
                Serial.printf("[BTN] %s pressed\n", g_btns[i].name);
                if (g_btns[i].pin == PIN_DIAL_NEXT) {
                    active_page = (DashPage)((active_page + 1) % PAGE_COUNT);
                    needs_refresh = true;
                } else if (g_btns[i].pin == PIN_DIAL_PRV) {
                    active_page = (DashPage)((active_page + PAGE_COUNT - 1) % PAGE_COUNT);
                    needs_refresh = true;
                } else if (g_btns[i].pin == PIN_DIAL_OK) {
                    force_clean_refresh = true;
                    needs_refresh = true;
                } else if (g_btns[i].pin == PIN_BTN_HOME) {
                    demo_mode = !demo_mode;
                    needs_refresh = true;
                } else if (g_btns[i].pin == PIN_BTN_EXIT) {
                    flip_180 = !flip_180;
                    force_clean_refresh = true;
                    needs_refresh = true;
                }
            }
        }
    }
}

static void handle_serial(const EspDashTelemetry &pkt, uint16_t plen, LinkState link) {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == 'n' || c == 'N') {
            active_page = (DashPage)((active_page + 1) % PAGE_COUNT);
            needs_refresh = true;
        } else if (c == 'p' || c == 'P') {
            active_page = (DashPage)((active_page + PAGE_COUNT - 1) % PAGE_COUNT);
            needs_refresh = true;
        } else if (c == '1') {
            active_page = PAGE_TRIP;
            needs_refresh = true;
        } else if (c == '2') {
            active_page = PAGE_HEALTH;
            needs_refresh = true;
        } else if (c == '3') {
            active_page = PAGE_PARKED;
            needs_refresh = true;
        } else if (c == 'r' || c == 'R') {
            flip_180 = !flip_180;
            force_clean_refresh = true;
            needs_refresh = true;
        } else if (c == 'c' || c == 'C') {
            force_clean_refresh = true;
            needs_refresh = true;
        } else if (c == 'd' || c == 'D') {
            demo_mode = !demo_mode;
            needs_refresh = true;
        } else if (c == 'z' || c == 'Z') {
            if (pkt.flags2 & ESPDASH_FLAG2_ODO_VALID) odo_base = pkt.odo_50m;
            session_start_ms = millis();
            needs_refresh = true;
        } else if (c == 's' || c == 'S') {
            Serial.printf("[STATUS] page=%d link=%d demo=%d pkts=%lu gaps=%lu fuel=%u%% cool=%.1fC trip=%.2fkm\n",
                          (int)active_page + 1, (int)link, (int)demo_mode,
                          (unsigned long)pkt_count, (unsigned long)pkt_gaps,
                          pkt.fuel_level_pct, pkt.water_temp_x10 * 0.1f,
                          compute_trip_km(pkt, plen));
        }
    }
}

// =========================================================================
// SETUP & LOOP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(250);

    Serial.println("\n=================================================================");
    Serial.println(" espDash CrowPanel 3.7\" E-Paper Slow-Data Dash (ESP32-S3-N8R8)");
    Serial.println("=================================================================");

    // Enable E-Paper power rail BEFORE initializing SPI/GPIO
    pinMode(PIN_EPD_PWR, OUTPUT);
    digitalWrite(PIN_EPD_PWR, HIGH);
    pinMode(PIN_LED_PWR, OUTPUT);
    digitalWrite(PIN_LED_PWR, LOW);
    delay(20);

    for (int i = 0; i < 5; i++) {
        pinMode(g_btns[i].pin, INPUT_PULLUP);
        bool init_state = digitalRead(g_btns[i].pin);
        g_btns[i].last_raw = init_state;
        g_btns[i].stable = init_state;
    }

    EPD_GPIOInit();
    init_espnow();

    session_start_ms = millis();
    quiet_since = session_start_ms;
    last_rx_rate_ms = session_start_ms;

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("[NODE] MAC: %02X:%02X:%02X:%02X:%02X:%02X | PSRAM: %lu B\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned long)ESP.getPsramSize());

    // Initial bench render so the screen shows data immediately on boot
    EspDashTelemetry init_pkt = build_demo_packet(millis());
    odo_base = init_pkt.odo_50m;
    update_session_stats(init_pkt, sizeof(EspDashTelemetry));
    render_epaper_screen(init_pkt, sizeof(EspDashTelemetry), LINK_SEARCHING, millis());
}

void loop() {
    uint32_t now = millis();

    EspDashTelemetry rx_pkt;
    uint16_t rx_len;
    uint32_t rx_time;
    portENTER_CRITICAL(&pkt_mux);
    rx_pkt = current_pkt;
    rx_len = current_payload_len;
    rx_time = last_pkt_rx_time;
    portEXIT_CRITICAL(&pkt_mux);

    LinkState link = update_link_state(now, rx_time);

    if (now - last_rx_rate_ms >= 2000) {
        uint32_t pkts = pkt_count;
        rx_hz = (pkts - last_rx_rate_pkts) * 1000.0f / (now - last_rx_rate_ms);
        last_rx_rate_pkts = pkts;
        last_rx_rate_ms = now;
    }

    EspDashTelemetry active_pkt;
    uint16_t active_len;
    if (link == LINK_LIVE) {
        active_pkt = rx_pkt;
        active_len = rx_len ? rx_len : sizeof(EspDashTelemetry);
        g_last_valid_pkt = active_pkt;
        g_last_valid_len = active_len;
        if (!stats_live_seeded) {
            // Clear bench demo peaks when the first real gateway packet arrives
            g_stats = {
                .max_coolant_x10 = -400,
                .max_speed_x10 = 0,
                .max_rpm = 0,
                .min_batt_mv = 0xFFFF,
                .max_batt_mv = 0,
                .min_cabin_c = 127,
                .max_cabin_c = -128,
                .max_wheel_slip_x10 = 0,
                .has_cabin = false
            };
            session_start_ms = now;
            odo_base = 0xFFFF;
            odo_live_seeded = false;
            stats_live_seeded = true;
        }
        if (!odo_live_seeded && ESPDASH_HAS(active_len, odo_50m) &&
            (active_pkt.flags2 & ESPDASH_FLAG2_ODO_VALID)) {
            odo_base = active_pkt.odo_50m;
            odo_live_seeded = true;
        }
        update_session_stats(active_pkt, active_len);
    } else if (link == LINK_PARKED && !demo_mode) {
        active_pkt = g_last_valid_pkt;
        active_len = g_last_valid_len;
        if (!parked_rendered) {
            active_page = PAGE_PARKED;
            force_clean_refresh = true;
            needs_refresh = true;
            parked_rendered = true;
        }
    } else if (demo_mode) {
        active_pkt = build_demo_packet(now);
        active_len = sizeof(EspDashTelemetry);
        if (odo_base == 0xFFFF) odo_base = active_pkt.odo_50m;
        update_session_stats(active_pkt, active_len);
    } else {
        active_pkt = g_last_valid_pkt;
        active_len = g_last_valid_len;
    }

    poll_buttons(now);
    handle_serial(active_pkt, active_len, link);

    // E-Paper smart refresh: immediate on interaction/alert, otherwise >= 5s interval when slow values move
    bool critical_edge = (g_last_drawn.link != link) ||
                         ((active_pkt.flags2 & ESPDASH_FLAG2_LOW_FUEL) != (g_last_drawn.flags2 & ESPDASH_FLAG2_LOW_FUEL)) ||
                         ((active_pkt.flags & ESPDASH_FLAG_OVERHEAT) != (g_last_drawn.flags & ESPDASH_FLAG_OVERHEAT));

    if (needs_refresh || critical_edge ||
        ((now - last_refresh_ms >= 5000) && slow_data_changed(active_pkt, link)) ||
        (link == LINK_LIVE && (now - last_refresh_ms >= 15000))) {
        needs_refresh = false;
        render_epaper_screen(active_pkt, active_len, link, now);
    }

    delay(25);
}
