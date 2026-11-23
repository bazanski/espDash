#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <lvgl.h>

#include <EspDashProto.h>
#include <EspDashSignals.h>

#include "bsp/board_config.h"
#include "bsp/display_bsp.h"
#include "bsp/touch_bsp.h"
#include "bsp/imu_bsp.h"
#include "imu_screen.h"
#include "pfd_screen.h"

// =========================================================================
// TELEMETRY STATE & SUPERVISION
// =========================================================================
static EspDashTelemetry current_pkt = {0};
static uint16_t  current_payload_len = 0;
static volatile uint32_t last_pkt_rx_time = 0;   // written by the Wi-Fi task
static volatile uint32_t pkt_count = 0;
static portMUX_TYPE pkt_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t  pkt_gaps = 0;
static uint16_t  last_seq = 0xFFFF;
static volatile bool ever_linked = false;

// Channel supervision.
// The gateway sits on ONE fixed channel (ESPDASH_ESPNOW_CHANNEL) and never
// moves, so this node parks on it and stays there. A dropped packet or a
// short outage must NOT make the node leave: it only goes looking for the
// gateway after SCAN_AFTER_SILENCE_MS of total silence, makes one slow sweep
// (SCAN_DWELL_MS per channel, long enough to catch several 20 Hz packets),
// and then returns to its home channel. Whichever channel a packet is heard
// on becomes the new home.
static uint8_t   current_channel = ESPDASH_ESPNOW_CHANNEL;   // radio's channel
static uint8_t   home_channel = ESPDASH_ESPNOW_CHANNEL;
static volatile uint8_t rx_channel = 0;    // channel of the last packet heard
static bool      scanning = false;
static uint8_t   scan_start = 0;
static uint32_t  last_channel_hop = 0;
static uint32_t  quiet_since = 0;          // last packet / end of last sweep

#ifndef ESPDASH_PERF_LOG
#define ESPDASH_PERF_LOG 0   // 1 = print redraw rate / frame time every 2 s
#endif
#define LINK_TIMEOUT_MS        1500    // "LIVE" -> "LINK LOST" (UI only)
#define SCAN_AFTER_SILENCE_MS 10000    // total silence before a sweep starts
#define SCAN_DWELL_MS           400    // per channel while sweeping

// Unit conversion preferences
static bool speed_in_mph = false;
static bool temp_in_f = false;

// =========================================================================
// LVGL UI OBJECTS
// =========================================================================
static lv_obj_t *lbl_title = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *btn_rotate = NULL;
static lv_obj_t *lbl_rotate_btn = NULL;

static lv_obj_t *card_left = NULL;
static lv_obj_t *lbl_speed = NULL;
static lv_obj_t *lbl_speed_unit = NULL;
static lv_obj_t *lbl_gear = NULL;
static lv_obj_t *bar_rpm = NULL;
static lv_obj_t *lbl_rpm = NULL;

static lv_obj_t *card_coolant = NULL;
static lv_obj_t *lbl_coolant_val = NULL;
static lv_obj_t *card_batt = NULL;
static lv_obj_t *lbl_batt_val = NULL;
static lv_obj_t *card_fuel = NULL;
static lv_obj_t *lbl_fuel_val = NULL;
static lv_obj_t *bar_fuel = NULL;

static lv_obj_t *lbl_touch = NULL;

// Pages: the car dashboard and the IMU screen are two LVGL screens.
static lv_obj_t *scr_car = NULL;
static lv_obj_t *scr_imu_page = NULL;
static lv_obj_t *scr_pfd_page = NULL;
enum Page { PAGE_CAR, PAGE_IMU, PAGE_PFD };
static Page      page = PAGE_CAR;
static bool      imu_page_active = false;   // page == PAGE_IMU
static bool      pfd_demo = false;          // serial 'd': simulated drive on the PFD

// =========================================================================
// INTERACTIVE UI EVENT HANDLERS
// =========================================================================
static void on_rotate_click(lv_event_t *e) {
    display_toggle_rotation();
}

static void show_page(Page p);
// The one navigation button: Car -> IMU -> PFD -> Car -> ...
static void on_next_page(lv_event_t *e) {
    show_page((Page)((page + 1) % 3));
}

static void on_show_pfd(lv_event_t *e) {
    page = PAGE_PFD;
    imu_page_active = false;
    lv_scr_load(scr_pfd_page);
    Serial.println("[UI] PFD page");
}

static void on_show_imu(lv_event_t *e) {
    page = PAGE_IMU;
    imu_page_active = true;
    lv_scr_load(scr_imu_page);
    Serial.println("[UI] IMU page");
}

static void on_show_car(lv_event_t *e) {
    page = PAGE_CAR;
    imu_page_active = false;
    lv_scr_load(scr_car);
    Serial.println("[UI] Car data page");
}

static void show_page(Page p) {
    if (p == PAGE_IMU)      on_show_imu(NULL);
    else if (p == PAGE_PFD) on_show_pfd(NULL);
    else                    on_show_car(NULL);
}

static void on_speed_click(lv_event_t *e) {
    speed_in_mph = !speed_in_mph;
    Serial.printf("[UI] Speed unit toggled to %s\n", speed_in_mph ? "MPH" : "KM/H");
}

static void on_coolant_click(lv_event_t *e) {
    temp_in_f = !temp_in_f;
    Serial.printf("[UI] Coolant unit toggled to %s\n", temp_in_f ? "°F" : "°C");
}

static void on_screen_touch(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    char buf[64];
    snprintf(buf, sizeof(buf), "Touch: X=%d Y=%d [ACTIVE TAP]", p.x, p.y);
    lv_label_set_text(lbl_touch, buf);
    lv_obj_set_style_text_color(lbl_touch, lv_color_hex(0x00E676), 0);
}

// =========================================================================
// ESP-NOW RECEIVER & LINK SUPERVISION
// =========================================================================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    uint16_t plen = 0, seq = 0;
    const EspDashTelemetry *t = espdash_parse(incomingData, len, &plen, &seq);
    if (!t) return;

    // Build the packet off to the side, then publish it in one short locked
    // copy so the UI never renders a half-zeroed struct.
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
        if (missed && missed < 1000) pkt_gaps += missed;
    }
    last_seq = seq;
}

// Called from loop(): keeps the radio parked on the gateway's channel and
// runs the (rare, slow) discovery sweep. Never touches the radio while the
// link is healthy.
static void supervise_channel(uint32_t now, uint32_t last_rx, uint8_t heard_on, bool heard_recently) {
    if (heard_recently) {
        // Packets are arriving: this channel is home, stop any sweep.
        if (heard_on && heard_on != home_channel) {
            home_channel = heard_on;
            Serial.printf("[ESP-NOW] Gateway found on channel %u - locked\n", home_channel);
        }
        if (scanning) scanning = false;
        if (current_channel != home_channel) {
            current_channel = home_channel;
            esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
        }
        quiet_since = last_rx;
        return;
    }

    if (!scanning) {
        // Link down but the gateway may just be rebooting/asleep: stay home.
        if ((int32_t)(now - quiet_since) >= SCAN_AFTER_SILENCE_MS) {
            scanning = true;
            scan_start = home_channel;
            last_channel_hop = now;
            Serial.printf("[ESP-NOW] Silent %us on CH%u, sweeping channels...\n",
                          SCAN_AFTER_SILENCE_MS / 1000, home_channel);
        }
        return;
    }

    if ((int32_t)(now - last_channel_hop) >= SCAN_DWELL_MS) {
        last_channel_hop = now;
        current_channel = (current_channel % 13) + 1;
        if (current_channel == scan_start) {
            // Full lap with nothing heard: go home and wait again.
            scanning = false;
            quiet_since = now;
            Serial.printf("[ESP-NOW] Sweep done, nothing heard. Back to CH%u\n", home_channel);
        }
        esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
    }
}

static void init_espnow() {
    Serial.println("[ESP-NOW] Initializing Wi-Fi STA & ESP-NOW receiver...");

    // 1. Reset Wi-Fi radio cleanly
    WiFi.disconnect(true, true);
    delay(20);

    // 2. Start Wi-Fi in pure Station mode
    WiFi.mode(WIFI_STA);
    delay(20);

    // 3. Set channel to ESPDASH_ESPNOW_CHANNEL (Channel 1)
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // 4. Disable modem sleep - CRITICAL for 20Hz reception without AP association
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 5. Maximize RF output power / receiver sensitivity
    esp_wifi_set_max_tx_power(84); // 20 dBm (max)

    // 6. Initialize ESP-NOW
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.printf("[ESP-NOW] Receiver registered on channel %d\n", ESPDASH_ESPNOW_CHANNEL);
    } else {
        Serial.println("[ERROR] esp_now_init() failed!");
    }
}

// =========================================================================
// DASHBOARD BUILDER (480 x 320 LANDSCAPE WIDESCREEN)
// =========================================================================
static void build_dashboard() {
    lv_obj_t *scr = lv_scr_act();
    scr_car = scr;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0E14), 0);
    lv_obj_add_event_cb(scr, on_screen_touch, LV_EVENT_PRESSED, NULL);

    // ---------------------------------------------------------------------
    // TOP HEADER (Y: 0..38)
    // ---------------------------------------------------------------------
    lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "ESPDASH 3.5\"");
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(lbl_title, 14, 8);

    lbl_status = lv_label_create(scr);
    lv_label_set_text(lbl_status, "[ WAITING CH1 ]");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 10, 10);

    // Interactive Rotate 180° button
    btn_rotate = lv_btn_create(scr);
    lv_obj_set_size(btn_rotate, 122, 32);
    lv_obj_set_pos(btn_rotate, 346, 282);
    lv_obj_set_style_bg_color(btn_rotate, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_border_color(btn_rotate, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(btn_rotate, 1, 0);
    lv_obj_set_style_radius(btn_rotate, 16, 0);
    lv_obj_add_event_cb(btn_rotate, on_rotate_click, LV_EVENT_CLICKED, NULL);

    lbl_rotate_btn = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate_btn, "ROTATE 180");
    lv_obj_set_style_text_color(lbl_rotate_btn, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_rotate_btn, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_rotate_btn);

    // ---------------------------------------------------------------------
    // LEFT CARD: SPEED, GEAR & TACHOMETER (X: 12, Y: 40, Width: 226, Height: 236)
    // ---------------------------------------------------------------------
    card_left = lv_obj_create(scr);
    lv_obj_set_size(card_left, 226, 236);
    lv_obj_set_pos(card_left, 12, 40);
    lv_obj_set_style_bg_color(card_left, lv_color_hex(0x131A22), 0);
    lv_obj_set_style_border_color(card_left, lv_color_hex(0x222E3C), 0);
    lv_obj_set_style_border_width(card_left, 1, 0);
    lv_obj_set_style_radius(card_left, 10, 0);
    lv_obj_set_style_pad_all(card_left, 6, 0);
    lv_obj_clear_flag(card_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card_left, on_speed_click, LV_EVENT_CLICKED, NULL);

    // Speed digits
    lbl_speed = lv_label_create(card_left);
    lv_label_set_text(lbl_speed, "0");
    lv_obj_set_style_text_color(lbl_speed, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_speed, &lv_font_montserrat_48, 0);
    lv_obj_align(lbl_speed, LV_ALIGN_TOP_MID, 0, 4);

    // Speed unit
    lbl_speed_unit = lv_label_create(card_left);
    lv_label_set_text(lbl_speed_unit, "KM / H  (Tap to toggle)");
    lv_obj_set_style_text_color(lbl_speed_unit, lv_color_hex(0x78909C), 0);
    lv_obj_set_style_text_font(lbl_speed_unit, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_speed_unit, LV_ALIGN_TOP_MID, 0, 64);

    // Gear indicator
    lbl_gear = lv_label_create(card_left);
    lv_label_set_text(lbl_gear, "[ D ]");
    lv_obj_set_style_text_color(lbl_gear, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_gear, LV_ALIGN_TOP_MID, 0, 88);

    // RPM Bar
    bar_rpm = lv_bar_create(card_left);
    lv_obj_set_size(bar_rpm, 204, 16);
    lv_obj_align(bar_rpm, LV_ALIGN_TOP_MID, 0, 142);
    lv_bar_set_range(bar_rpm, 0, 7500);
    lv_bar_set_value(bar_rpm, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0x222E3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_rpm, 6, 0);
    lv_obj_set_style_radius(bar_rpm, 6, LV_PART_INDICATOR);

    // RPM Text
    lbl_rpm = lv_label_create(card_left);
    lv_label_set_text(lbl_rpm, "0 RPM");
    lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xECEFF1), 0);
    lv_obj_set_style_text_font(lbl_rpm, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_rpm, LV_ALIGN_TOP_MID, 0, 172);

    // ---------------------------------------------------------------------
    // RIGHT PANEL: METRIC CARDS (X: 246, Width: 222, Y: 40..276)
    // ---------------------------------------------------------------------

    // Card 1: Coolant Temp (Y: 40, Height: 74)
    card_coolant = lv_obj_create(scr);
    lv_obj_set_size(card_coolant, 222, 74);
    lv_obj_set_pos(card_coolant, 246, 40);
    lv_obj_set_style_bg_color(card_coolant, lv_color_hex(0x131A22), 0);
    lv_obj_set_style_border_color(card_coolant, lv_color_hex(0x222E3C), 0);
    lv_obj_set_style_border_width(card_coolant, 1, 0);
    lv_obj_set_style_radius(card_coolant, 10, 0);
    lv_obj_set_style_pad_all(card_coolant, 8, 0);
    lv_obj_clear_flag(card_coolant, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card_coolant, on_coolant_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_c_hdr = lv_label_create(card_coolant);
    lv_label_set_text(lbl_c_hdr, "COOLANT TEMP");
    lv_obj_set_style_text_color(lbl_c_hdr, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_style_text_font(lbl_c_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_c_hdr, 4, 2);

    lbl_coolant_val = lv_label_create(card_coolant);
    lv_label_set_text(lbl_coolant_val, "89 °C");
    lv_obj_set_style_text_color(lbl_coolant_val, lv_color_hex(0x00E676), 0);
    lv_obj_set_style_text_font(lbl_coolant_val, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(lbl_coolant_val, 4, 24);

    // Card 2: Battery Voltage (Y: 121, Height: 74)
    card_batt = lv_obj_create(scr);
    lv_obj_set_size(card_batt, 222, 74);
    lv_obj_set_pos(card_batt, 246, 121);
    lv_obj_set_style_bg_color(card_batt, lv_color_hex(0x131A22), 0);
    lv_obj_set_style_border_color(card_batt, lv_color_hex(0x222E3C), 0);
    lv_obj_set_style_border_width(card_batt, 1, 0);
    lv_obj_set_style_radius(card_batt, 10, 0);
    lv_obj_set_style_pad_all(card_batt, 8, 0);
    lv_obj_clear_flag(card_batt, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_b_hdr = lv_label_create(card_batt);
    lv_label_set_text(lbl_b_hdr, "BATTERY VOLTAGE");
    lv_obj_set_style_text_color(lbl_b_hdr, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_style_text_font(lbl_b_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_b_hdr, 4, 2);

    lbl_batt_val = lv_label_create(card_batt);
    lv_label_set_text(lbl_batt_val, "14.2 V");
    lv_obj_set_style_text_color(lbl_batt_val, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_batt_val, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(lbl_batt_val, 4, 24);

    // Card 3: Fuel Level (Y: 202, Height: 74)
    card_fuel = lv_obj_create(scr);
    lv_obj_set_size(card_fuel, 222, 74);
    lv_obj_set_pos(card_fuel, 246, 202);
    lv_obj_set_style_bg_color(card_fuel, lv_color_hex(0x131A22), 0);
    lv_obj_set_style_border_color(card_fuel, lv_color_hex(0x222E3C), 0);
    lv_obj_set_style_border_width(card_fuel, 1, 0);
    lv_obj_set_style_radius(card_fuel, 10, 0);
    lv_obj_set_style_pad_all(card_fuel, 8, 0);
    lv_obj_clear_flag(card_fuel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_f_hdr = lv_label_create(card_fuel);
    lv_label_set_text(lbl_f_hdr, "FUEL LEVEL");
    lv_obj_set_style_text_color(lbl_f_hdr, lv_color_hex(0x90A4AE), 0);
    lv_obj_set_style_text_font(lbl_f_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_f_hdr, 4, 2);

    lbl_fuel_val = lv_label_create(card_fuel);
    lv_label_set_text(lbl_fuel_val, "78 %");
    lv_obj_set_style_text_color(lbl_fuel_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_fuel_val, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(lbl_fuel_val, 4, 24);

    bar_fuel = lv_bar_create(card_fuel);
    lv_obj_set_size(bar_fuel, 100, 10);
    lv_obj_align(bar_fuel, LV_ALIGN_RIGHT_MID, -6, 6);
    lv_bar_set_range(bar_fuel, 0, 100);
    lv_bar_set_value(bar_fuel, 78, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_fuel, lv_color_hex(0x222E3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_fuel, lv_color_hex(0x00E676), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_fuel, 4, 0);

    // ---------------------------------------------------------------------
    // BOTTOM STATUS / TOUCH FEEDBACK BAR (Y: 284..316)
    // ---------------------------------------------------------------------
    lbl_touch = lv_label_create(scr);
    lv_label_set_text(lbl_touch, "Touch: Ready (Tap screen to interact)");
    lv_obj_set_style_text_color(lbl_touch, lv_color_hex(0x78909C), 0);
    lv_obj_set_style_text_font(lbl_touch, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_touch, 14, 292);

    // Switch to the IMU page
    lv_obj_t *btn_imu = lv_btn_create(scr);
    lv_obj_set_size(btn_imu, 96, 26);
    lv_obj_set_pos(btn_imu, 380, 5);
    lv_obj_set_style_bg_color(btn_imu, lv_color_hex(0x1B2430), 0);
    lv_obj_set_style_border_color(btn_imu, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_border_width(btn_imu, 1, 0);
    lv_obj_set_style_radius(btn_imu, 13, 0);
    lv_obj_set_style_shadow_width(btn_imu, 0, 0);
    lv_obj_add_event_cb(btn_imu, on_next_page, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_imu_btn = lv_label_create(btn_imu);
    lv_label_set_text(lbl_imu_btn, "NEXT: IMU >");
    lv_obj_set_style_text_color(lbl_imu_btn, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_text_font(lbl_imu_btn, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl_imu_btn);
}

// =========================================================================
// AUTONOMOUS DEMO SWEEP (FOR DESK TESTING / SEARCHING FALLBACK)
// =========================================================================
static void run_demo_sweep(uint32_t now) {
    static float demo_phase = 0.0f;
    demo_phase += 0.04f;

    // Smooth sinusoidal simulation
    uint16_t sim_rpm = (uint16_t)(3500 + 2600 * sin(demo_phase));
    uint8_t sim_speed_kmh = (uint8_t)(60 + 55 * sin(demo_phase * 0.45f));
    uint8_t sim_gear = (sim_speed_kmh < 18) ? 1 : (sim_speed_kmh < 40) ? 2 : (sim_speed_kmh < 70) ? 3 : (sim_speed_kmh < 95) ? 4 : 5;

    char buf[48];

    // Speed display (KM/H or MPH)
    if (speed_in_mph) {
        uint8_t sim_mph = (uint8_t)(sim_speed_kmh * 0.621371f);
        snprintf(buf, sizeof(buf), "%d", sim_mph);
        lv_label_set_text(lbl_speed, buf);
        lv_label_set_text(lbl_speed_unit, "MPH  (Tap to toggle)");
    } else {
        snprintf(buf, sizeof(buf), "%d", sim_speed_kmh);
        lv_label_set_text(lbl_speed, buf);
        lv_label_set_text(lbl_speed_unit, "KM / H  (Tap to toggle)");
    }

    // RPM Bar & Value
    snprintf(buf, sizeof(buf), "%d RPM", sim_rpm);
    lv_label_set_text(lbl_rpm, buf);
    lv_bar_set_value(bar_rpm, sim_rpm, LV_ANIM_OFF);

    // RPM Color thresholds
    if (sim_rpm > 6200) {
        lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0xFF1744), LV_PART_INDICATOR); // Redline
        lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xFF1744), 0);
    } else if (sim_rpm > 4500) {
        lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0xFFD600), LV_PART_INDICATOR); // VTEC zone
        lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xFFD600), 0);
    } else {
        lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0x00E676), LV_PART_INDICATOR); // Cruising
        lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xECEFF1), 0);
    }

    // Gear Indicator
    snprintf(buf, sizeof(buf), "[ D%d ]", sim_gear);
    lv_label_set_text(lbl_gear, buf);

    // Metric Cards
    if (temp_in_f) {
        lv_label_set_text(lbl_coolant_val, "192 °F");
    } else {
        lv_label_set_text(lbl_coolant_val, "89 °C");
    }

    lv_label_set_text(lbl_batt_val, "14.2 V");

    uint8_t sim_fuel = (uint8_t)(72 + 6 * sin(demo_phase * 0.1f));
    snprintf(buf, sizeof(buf), "%d %%", sim_fuel);
    lv_label_set_text(lbl_fuel_val, buf);
    lv_bar_set_value(bar_fuel, sim_fuel, LV_ANIM_OFF);
}

// =========================================================================
// ARDUINO SETUP & LOOP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("==================================================");
    Serial.println("  espDash 3.5\" Touch Display Node (ES3C35P)      ");
    Serial.println("==================================================");

    // Configure BOOT button pin for orientation toggle
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

    // Initialize display hardware & LVGL
    display_init();

    // Initialize touch controller & register to LVGL
    touch_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_lvgl_read_cb;
    lv_indev_drv_register(&indev_drv);

    // IMU sampling task (shares the touch I2C bus) + its screen
    imu_init();

    // Build the widescreen UI
    build_dashboard();
    scr_imu_page = imu_screen_build(on_next_page, on_rotate_click);
    scr_pfd_page = pfd_screen_build(on_next_page);

    // Initialize ESP-NOW
    init_espnow();
    quiet_since = millis();   // start the "silent too long" clock at boot, not at 0
}

void loop() {
    static uint32_t last_ui_update = 0;
    static uint32_t last_btn_check = 0;
    static bool last_btn_state = HIGH;
    uint32_t now = millis();

    // Handle BOOT button (GPIO 0) press to toggle rotation
    if (now - last_btn_check >= 50) {
        last_btn_check = now;
        bool btn = digitalRead(BOOT_BTN_PIN);
        if (btn == LOW && last_btn_state == HIGH) {
            display_toggle_rotation();
        }
        last_btn_state = btn;
    }

    // Serial debug: 'i' = IMU page, 'c' = car page (same as the on-screen buttons)
    while (Serial.available()) {
        int ch = Serial.read();
        if (ch == 'i') on_show_imu(NULL);
        else if (ch == 'c') on_show_car(NULL);
        else if (ch == 'p') on_show_pfd(NULL);
        else if (ch == 'n') on_next_page(NULL);   // same as the NEXT button
        else if (ch == 'd') { pfd_demo = !pfd_demo; Serial.printf("[UI] PFD demo %s\n", pfd_demo ? "ON" : "OFF"); }
    }

    // LVGL tick & task handler (timed: the PFD draws everything in software)
    uint32_t t_lv0 = micros();
    lv_timer_handler();
    {
        static uint32_t acc_us = 0, max_us = 0, frames = 0, t_rep = 0;
        uint32_t el = micros() - t_lv0;
        if (el > 3000) { acc_us += el; frames++; if (el > max_us) max_us = el; }   // a real redraw, not an idle tick
        if (millis() - t_rep >= 2000) {
#if ESPDASH_PERF_LOG
            if (frames)
                Serial.printf("[PERF p%d] %.1f redraws/s, avg %lu ms, worst %lu ms\n",
                              (int)page, frames * 1000.0f / (millis() - t_rep), (unsigned long)(acc_us / frames / 1000), (unsigned long)(max_us / 1000));
#endif
            acc_us = max_us = frames = 0; t_rep = millis();
        }
    }

    // Link supervision. Re-read the clock AFTER lv_timer_handler(): a frame
    // flush takes tens of ms, during which the Wi-Fi task can stamp a packet
    // NEWER than a clock sampled at the top of loop(). With unsigned math that
    // "negative age" wrapped to ~4 billion, the link was declared dead every
    // few frames and the node hopped channels away from a healthy gateway.
    now = millis();
    uint32_t last_rx;
    uint8_t  heard_on;
    EspDashTelemetry cp;
    portENTER_CRITICAL(&pkt_mux);
    last_rx  = last_pkt_rx_time;
    heard_on = rx_channel;
    cp       = current_pkt;
    portEXIT_CRITICAL(&pkt_mux);

    bool is_live = ever_linked && ((int32_t)(now - last_rx) <= (int32_t)LINK_TIMEOUT_MS);
    supervise_channel(now, last_rx, heard_on, is_live);

    // Update UI at 20Hz (every 50ms)
    if (now - last_ui_update >= 50) {
        last_ui_update = now;

        if (page == PAGE_PFD) {
            ImuData imu;
            imu_get(&imu);
            PfdData pd;
            if (pfd_demo) {
                pfd_demo_fill(pd, now);
            } else {
                memset(&pd, 0, sizeof(pd));
                pd.link = is_live;
                pd.imu_ok = imu.present;
                pd.speed_kmh = cp.speed_kmh_x10 / 10.0f;
                pd.rpm = cp.rpm;
                pd.gear_sel = cp.gear;
                pd.gear_num = cp.gear_num;
                pd.coolant_c = cp.water_temp_x10 / 10.0f;
                pd.batt_v = cp.battery_mv / 1000.0f;
                pd.fuel_pct = cp.fuel_level_pct;
                pd.fuel_valid = (cp.flags2 & ESPDASH_FLAG2_FUEL_VALID) != 0;
                pd.low_fuel = (cp.flags2 & ESPDASH_FLAG2_LOW_FUEL) != 0;
                pd.econ = (cp.flags2 & ESPDASH_FLAG2_ECON) != 0;
                pd.sport = (cp.flags2 & ESPDASH_FLAG2_SPORT) != 0;
                pd.roll = imu.roll;
                pd.pitch_up = -imu.pitch;      // IMU pitch is positive nose-down
                pd.heading = imu.heading;
                pd.heading_mag = imu.has_mag && imu.mag_calibrated;
                pd.lin_x = imu.lin_x;
                pd.lin_y = imu.lin_y;
                static char link[24];
                if (is_live)          { snprintf(link, sizeof(link), "GW LIVE"); pd.link_color = 0x30FF40; }
                else if (ever_linked) { snprintf(link, sizeof(link), "GW LOST"); pd.link_color = 0xFFB000; }
                else                  { snprintf(link, sizeof(link), "NO GW");   pd.link_color = 0xFF2020; }
                pd.link_text = link;
            }
            pfd_screen_update(pd);
        } else if (imu_page_active) {
            ImuData imu;
            imu_get(&imu);
            char link[48];
            uint32_t col;
            if (is_live)          { snprintf(link, sizeof(link), "GW LIVE CH%d", current_channel); col = 0x00E676; }
            else if (ever_linked) { snprintf(link, sizeof(link), "GW LOST CH%d", current_channel); col = 0xFF9100; }
            else                  { snprintf(link, sizeof(link), "GW %s CH%d", scanning ? "SCAN" : "WAIT", current_channel); col = 0x78909C; }
            imu_screen_update(imu, link, col);

            static uint32_t last_imu_log = 0;
            if (now - last_imu_log >= 1000 && imu.present) {
                last_imu_log = now;
                Serial.printf("[IMU] %s r=%+.1f p=%+.1f hdg=%.0f a=(%+.2f %+.2f %+.2f)g g=(%+.1f %+.1f %+.1f)dps m=(%+.1f %+.1f %+.1f)uT T=%.1fC %.0fHz\n",
                              imu.chip, imu.roll, imu.pitch, imu.heading, imu.ax, imu.ay, imu.az,
                              imu.gx, imu.gy, imu.gz, imu.mx, imu.my, imu.mz, imu.temp_c, imu.rate_hz);
            }
        } else if (is_live) {
            // Live Gateway Telemetry Mode
            char buf[64];
            snprintf(buf, sizeof(buf), "[ LIVE 20Hz #%lu CH%d ]", (unsigned long)pkt_count, current_channel);
            lv_label_set_text(lbl_status, buf);
            lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00E676), 0);

            // Speed
            uint16_t spd_kmh = cp.speed_kmh_x10 / 10;
            if (speed_in_mph) {
                uint16_t spd_mph = (uint16_t)(spd_kmh * 0.621371f);
                snprintf(buf, sizeof(buf), "%d", spd_mph);
                lv_label_set_text(lbl_speed, buf);
                lv_label_set_text(lbl_speed_unit, "MPH  (Tap to toggle)");
            } else {
                snprintf(buf, sizeof(buf), "%d", spd_kmh);
                lv_label_set_text(lbl_speed, buf);
                lv_label_set_text(lbl_speed_unit, "KM / H  (Tap to toggle)");
            }

            // RPM
            snprintf(buf, sizeof(buf), "%d RPM", cp.rpm);
            lv_label_set_text(lbl_rpm, buf);
            lv_bar_set_value(bar_rpm, cp.rpm, LV_ANIM_OFF);

            if (cp.rpm > 6200) {
                lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0xFF1744), LV_PART_INDICATOR);
                lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xFF1744), 0);
            } else if (cp.rpm > 4500) {
                lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0xFFD600), LV_PART_INDICATOR);
                lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xFFD600), 0);
            } else {
                lv_obj_set_style_bg_color(bar_rpm, lv_color_hex(0x00E676), LV_PART_INDICATOR);
                lv_obj_set_style_text_color(lbl_rpm, lv_color_hex(0xECEFF1), 0);
            }

            // Gear
            if (cp.gear_num > 0 && cp.gear_num <= 6) {
                snprintf(buf, sizeof(buf), "[ %d ]", cp.gear_num);
            } else {
                snprintf(buf, sizeof(buf), "[ D ]");
            }
            lv_label_set_text(lbl_gear, buf);

            // Coolant Card
            int16_t temp_c = cp.water_temp_x10 / 10;
            if (temp_in_f) {
                int16_t temp_f = (int16_t)(temp_c * 1.8f + 32);
                snprintf(buf, sizeof(buf), "%d °F", temp_f);
            } else {
                snprintf(buf, sizeof(buf), "%d °C", temp_c);
            }
            lv_label_set_text(lbl_coolant_val, buf);

            if (temp_c > 105) {
                lv_obj_set_style_text_color(lbl_coolant_val, lv_color_hex(0xFF1744), 0); // Overheating
            } else if (temp_c < 70) {
                lv_obj_set_style_text_color(lbl_coolant_val, lv_color_hex(0x00E5FF), 0); // Cold
            } else {
                lv_obj_set_style_text_color(lbl_coolant_val, lv_color_hex(0x00E676), 0); // Normal operating
            }

            // Battery Card
            float batt = cp.battery_mv / 1000.0f;
            snprintf(buf, sizeof(buf), "%.1f V", batt);
            lv_label_set_text(lbl_batt_val, buf);
            if (batt < 12.2f && batt > 5.0f) {
                lv_obj_set_style_text_color(lbl_batt_val, lv_color_hex(0xFFD600), 0); // Low
            } else {
                lv_obj_set_style_text_color(lbl_batt_val, lv_color_hex(0x00E5FF), 0); // Healthy
            }

            // Fuel Card
            snprintf(buf, sizeof(buf), "%d %%", cp.fuel_level_pct);
            lv_label_set_text(lbl_fuel_val, buf);
            lv_bar_set_value(bar_fuel, cp.fuel_level_pct, LV_ANIM_OFF);
        } else if (ever_linked) {
            // Previously linked, but gateway packets stopped
            char buf[64];
            snprintf(buf, sizeof(buf), "[ LINK LOST CH%d ]", current_channel);
            lv_label_set_text(lbl_status, buf);
            lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFF9100), 0);
            run_demo_sweep(now);
        } else {
            // Not heard yet (parked on home channel, sweeping only if silent long)
            char buf[64];
            snprintf(buf, sizeof(buf), "[ %s CH%d ]", scanning ? "SCANNING" : "WAITING", current_channel);
            lv_label_set_text(lbl_status, buf);
            lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x00E5FF), 0);
            run_demo_sweep(now);
        }
    }

    delay(5);
}
