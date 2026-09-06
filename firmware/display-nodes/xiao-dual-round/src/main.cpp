#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "ui/screens.h"

// =========================================================================
// ESP-NOW TELEMETRY - shared wire protocol
// =========================================================================
#include <EspDashProto.h>

static EspDashTelemetry current_pkt = {0};
static uint16_t  current_payload_len = 0;
static uint32_t  last_pkt_rx_time = 0;
static uint16_t  last_seq = 0;
static volatile uint32_t pkt_gaps = 0;
static volatile uint32_t pkt_count = 0;
static bool      ever_linked = false;

static uint8_t  espnow_channel = ESPDASH_ESPNOW_CHANNEL;
static bool     channel_locked = false;
#define LINK_TIMEOUT_MS  1500

enum LinkState { LINK_LIVE, LINK_SEARCHING, LINK_LOST };

static uint8_t actual_channel() {
    uint8_t ch = 0;
    wifi_second_chan_t sec;
    if (esp_wifi_get_channel(&ch, &sec) != ESP_OK) return espnow_channel;
    return ch;
}

// =========================================================================
// ESP-NOW RECEIVE CALLBACK
// =========================================================================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    uint16_t plen = 0, seq = 0;
    const EspDashTelemetry *t = espdash_parse(incomingData, len, &plen, &seq);
    if (!t) return;

    uint16_t copy = plen < sizeof(EspDashTelemetry) ? plen : sizeof(EspDashTelemetry);
    memset(&current_pkt, 0, sizeof(current_pkt));
    memcpy(&current_pkt, t, copy);
    current_payload_len = plen;

    if (ever_linked) {
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
// HELPERS & CIVIC ECO-COACHING COLOR LOGIC
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

static uint32_t get_civic_rpm_color(uint16_t rpm) {
    if (rpm <= 2500) return 0x00e676; // Bright Green
    else if (rpm <= 3500) return 0x00e5ff; // Cyan
    else if (rpm <= 4800) return 0xffd600; // Yellow
    else if (rpm <= 6200) return 0xff9100; // Orange
    else return 0xff1744; // Red
}

static uint32_t get_civic_efficiency_color(float l_per_100km) {
    if (l_per_100km <= 6.5f) return 0x00e676; // Bright Pure Green
    else if (l_per_100km <= 8.8f) return 0x76ff03; // Lime Green
    else if (l_per_100km <= 12.5f) return 0xffd600; // Gold / Yellow
    else if (l_per_100km <= 16.5f) return 0xff9100; // Orange
    else return 0xff1744; // Vivid Red
}

static uint32_t get_civic_throttle_color(uint8_t thr_pct) {
    if (thr_pct <= 25) return 0x00e676; // Bright Green
    else if (thr_pct <= 45) return 0xffd600; // Gold / Yellow
    else if (thr_pct <= 70) return 0xff9100; // Orange
    else return 0xff1744; // Red
}

static uint32_t get_civic_brake_color(uint8_t brake_pct) {
    if (brake_pct <= 5) return 0x00e676; // Normal/Off (Green)
    else if (brake_pct <= 35) return 0x00e5ff; // Light braking (Cyan)
    else if (brake_pct <= 65) return 0xffd600; // Moderate (Gold)
    else return 0xff1744; // Hard (Red)
}

static uint32_t get_civic_coolant_color(int16_t temp_c) {
    if (temp_c < 70) return 0x00e5ff; // Cold engine warm-up (Cyan)
    else if (temp_c <= 98) return 0x00e676; // Normal optimal operating temp (Green)
    else if (temp_c <= 104) return 0xffd600; // Getting warm (Gold)
    else return 0xff1744; // Overheating warning (Red)
}

static uint32_t get_civic_fuel_level_color(uint8_t fuel_pct, bool low_fuel_warning = false) {
    if (low_fuel_warning || fuel_pct < 15) return 0xff1744; // Low fuel reserve warning (Red)
    else if (fuel_pct <= 25) return 0xffd600; // Quarter tank (Gold)
    else return 0x00e676; // Plenty of fuel (Green)
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
// DUAL PANEL HARDWARE CONFIGURATION
// =========================================================================
// Verified hardware pin map on Waveshare ESP32-S3-Zero:
#define PIN_MOSI 8   // ESP32-S3-Zero: Pin labeled "8" (SDA line)
#define PIN_SCLK 7   // ESP32-S3-Zero: Pin labeled "7" (SCL line)
#define PIN_RST  4   // ESP32-S3-Zero: Pin labeled "4" (GP4)
#define PIN_BL   43  // ESP32-S3-Zero: Pin labeled "TX" (Backlight)
#define PIN_CS_A 2   // ESP32-S3-Zero: Pin labeled "2" (GP2) - Screen A CS
#define PIN_DC_A 3   // ESP32-S3-Zero: Pin labeled "3" (GP3) - Screen A DC
#define PIN_CS_B 1   // ESP32-S3-Zero: Pin labeled "1" (GP1) - Screen B CS
#define PIN_DC_B 5   // ESP32-S3-Zero: Pin labeled "5" (GP5) - Screen B DC

#define PANEL_H  240   // one physical panel's height
#define VDISP_W  240   // virtual LVGL display: 240 wide
#define VDISP_H  480   // virtual LVGL display: 480 tall (Panel A: 0..239, Panel B: 240..479)

#define SPI_HZ   80000000UL   // 80 MHz high-speed SPI clock

static Arduino_DataBus *busA = new Arduino_ESP32SPI(
    PIN_DC_A, PIN_CS_A, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED, FSPI, true);
static Arduino_DataBus *busB = new Arduino_ESP32SPI(
    PIN_DC_B, PIN_CS_B, PIN_SCLK, PIN_MOSI, GFX_NOT_DEFINED, FSPI, true);

// Hardware alignment calibration (in case physical LCD glass is offset inside circular bezel)
#define HARDWARE_OFFSET_X_A  1  // Screen A X offset (+: right, -: left) -> 1px right
#define HARDWARE_OFFSET_Y_A  0  // Screen A Y offset (+: down, -: up)
#define HARDWARE_OFFSET_X_B  0  // Screen B X offset (+: right, -: left)
#define HARDWARE_OFFSET_Y_B  1  // Screen B Y offset (+: down, -: up) -> 1px down

// Pass GFX_NOT_DEFINED as RST because shared RST is manually pulsed in setup()
// GC9A01 1.28" round displays are IPS panels: ips MUST be true so Display Inversion is ON (0x0000 = pure black)
static Arduino_GC9A01 *gfxA = new Arduino_GC9A01(busA, GFX_NOT_DEFINED, 0, true /* IPS */, 240, 240, HARDWARE_OFFSET_X_A, HARDWARE_OFFSET_Y_A, 0, 0);
static Arduino_GC9A01 *gfxB = new Arduino_GC9A01(busB, GFX_NOT_DEFINED, 2, true /* IPS */, 240, 240, 0, 0, HARDWARE_OFFSET_X_B, HARDWARE_OFFSET_Y_B); // 2 = 180 deg rotation

// =========================================================================
// LVGL 240x480 VIRTUAL DISPLAY SPLITTING FLUSH_CB
// =========================================================================
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

static volatile uint32_t g_frame_count = 0;
static void disp_monitor_cb(lv_disp_drv_t *disp_drv, uint32_t time_ms, uint32_t px) {
    (void)disp_drv; (void)time_ms; (void)px;
    g_frame_count++;
}

// ============================================================================
// DUAL SCREEN TELEMETRY DISPATCH (EEZ STUDIO INTEGRATION)
// ============================================================================
static void update_dual_ui(const EspDashTelemetry &pkt, LinkState link, bool is_demo) {
    // 1. Tick EEZ Studio UI engine
    ui_tick();

    // Cache previous colors to prevent full-widget invalidation on every frame
    static uint32_t prev_rpm_col = 0;
    static uint32_t prev_thr_col = 0;
    static uint32_t prev_eff_col = 0;
    static uint32_t prev_coolant_col = 0;
    static uint32_t prev_brake_col = 0;
    static uint32_t prev_fuel_col = 0;

    // ---- SCREEN A (Top Panel: Driving Dynamics) ----
    uint32_t rpm_col = get_civic_rpm_color(pkt.rpm);
    uint32_t thr_col = get_civic_throttle_color(pkt.throttle_pct);
    uint8_t eff_x10 = pkt.fuel_consumption_x10;
    uint32_t eff_col = get_civic_efficiency_color(eff_x10 / 10.0f);

    if (objects.rpm_arc) {
        lv_arc_set_value(objects.rpm_arc, pkt.rpm);
        if (rpm_col != prev_rpm_col) {
            prev_rpm_col = rpm_col;
            lv_obj_set_style_arc_color(objects.rpm_arc, lv_color_hex(rpm_col), LV_PART_INDICATOR);
            if (objects.rpm__value) lv_obj_set_style_text_color(objects.rpm__value, lv_color_hex(rpm_col), LV_PART_MAIN);
        }
    }
    if (objects.throttle_arc) {
        lv_arc_set_value(objects.throttle_arc, pkt.throttle_pct);
        if (thr_col != prev_thr_col) {
            prev_thr_col = thr_col;
            lv_obj_set_style_arc_color(objects.throttle_arc, lv_color_hex(thr_col), LV_PART_INDICATOR);
            if (objects.throttle_value) lv_obj_set_style_text_color(objects.throttle_value, lv_color_hex(thr_col), LV_PART_MAIN);
        }
    }
    if (objects.eff_arc) {
        lv_arc_set_value(objects.eff_arc, eff_x10 / 10);
        if (eff_col != prev_eff_col) {
            prev_eff_col = eff_col;
            lv_obj_set_style_arc_color(objects.eff_arc, lv_color_hex(eff_col), LV_PART_INDICATOR);
            if (objects.eff_value) lv_obj_set_style_text_color(objects.eff_value, lv_color_hex(eff_col), LV_PART_MAIN);
        }
    }

    if (objects.speed_value) {
        lv_label_set_text_fmt(objects.speed_value, "%d", pkt.speed_kmh_x10 / 10);
    }
    if (objects.eff_value) {
        lv_label_set_text_fmt(objects.eff_value, "%d.%d", eff_x10 / 10, eff_x10 % 10);
    }
    if (objects.throttle_value) {
        lv_label_set_text_fmt(objects.throttle_value, "%d", pkt.throttle_pct);
    }
    if (objects.rpm__value) {
        lv_label_set_text_fmt(objects.rpm__value, "%d", pkt.rpm);
    }

    // ---- SCREEN B (Bottom Panel: Vehicle Health & Energy) ----
    int16_t coolant_c = pkt.water_temp_x10 / 10;
    uint32_t coolant_col = get_civic_coolant_color(coolant_c);
    uint32_t brake_col = get_civic_brake_color(pkt.brake_pct);

    // Fuel level (decoded from Proto v2.4 CAN 0x1A6)
    bool fuel_valid = is_demo || (ESPDASH_HAS(current_payload_len, flags2) && (pkt.flags2 & ESPDASH_FLAG2_FUEL_VALID));
    bool low_fuel = (ESPDASH_HAS(current_payload_len, flags2) && (pkt.flags2 & ESPDASH_FLAG2_LOW_FUEL));
    uint8_t fuel_pct = 0;
    if (is_demo) {
        fuel_pct = pkt.fuel_level_pct;
        if (fuel_pct < 15) low_fuel = true;
    } else if (fuel_valid) {
        fuel_pct = (pkt.fuel_level_pct > 100) ? 100 : pkt.fuel_level_pct;
    }
    uint32_t fuel_col = get_civic_fuel_level_color(fuel_pct, low_fuel);

    // Remaining range (distance to empty in km): calculated dynamically from fuel level & consumption
    // Nominal Civic 9G fuel tank = 50.0 L (0.5 L per 1% fuel)
    float avg_cons = (pkt.fuel_consumption_x10 >= 20 && pkt.fuel_consumption_x10 <= 250)
                     ? (pkt.fuel_consumption_x10 / 10.0f)
                     : 7.2f; // Fallback to 7.2 L/100km if not available yet
    uint16_t range_km = (uint16_t)((fuel_pct * 50.0f / 100.0f) / avg_cons * 100.0f);

    if (objects.coolant_arc) {
        lv_arc_set_value(objects.coolant_arc, coolant_c);
        if (coolant_col != prev_coolant_col) {
            prev_coolant_col = coolant_col;
            lv_obj_set_style_arc_color(objects.coolant_arc, lv_color_hex(coolant_col), LV_PART_INDICATOR);
            if (objects.coolant_value) lv_obj_set_style_text_color(objects.coolant_value, lv_color_hex(coolant_col), LV_PART_MAIN);
        }
    }
    if (objects.brake_arc) {
        lv_arc_set_value(objects.brake_arc, pkt.brake_pct);
        if (brake_col != prev_brake_col) {
            prev_brake_col = brake_col;
            lv_obj_set_style_arc_color(objects.brake_arc, lv_color_hex(brake_col), LV_PART_INDICATOR);
            if (objects.brake_value) lv_obj_set_style_text_color(objects.brake_value, lv_color_hex(brake_col), LV_PART_MAIN);
        }
    }
    if (objects.fuel_arc) {
        lv_arc_set_value(objects.fuel_arc, fuel_pct);
        if (fuel_col != prev_fuel_col) {
            prev_fuel_col = fuel_col;
            lv_obj_set_style_arc_color(objects.fuel_arc, lv_color_hex(fuel_col), LV_PART_INDICATOR);
            if (objects.fuel_value) lv_obj_set_style_text_color(objects.fuel_value, lv_color_hex(fuel_col), LV_PART_MAIN);
        }
    }

    if (objects.left_distance_value) {
        if (fuel_valid) {
            lv_label_set_text_fmt(objects.left_distance_value, "%d", range_km);
        } else {
            lv_label_set_text_static(objects.left_distance_value, "--");
        }
    }
    if (objects.fuel_value) {
        if (fuel_valid) {
            lv_label_set_text_fmt(objects.fuel_value, "%d", fuel_pct);
        } else {
            lv_label_set_text_static(objects.fuel_value, "--");
        }
    }
    if (objects.brake_value) {
        lv_label_set_text_fmt(objects.brake_value, "%d", pkt.brake_pct);
    }
    if (objects.coolant_value) {
        lv_label_set_text_fmt(objects.coolant_value, "%d", coolant_c);
    }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=================================================================");
    Serial.println(" espDash DUAL ROUND GAUGE NODE (2x GC9A01, ESP32-S3-Zero)");
    Serial.println(" Screen A: CS=2, DC=3 (Driving Gauge)");
    Serial.println(" Screen B: CS=1, DC=5 (Efficiency Gauge)");
    Serial.println(" Shared:   MOSI=8, SCLK=7, RST=4, BL=43");
    Serial.println("=================================================================");

    // 1. Wi-Fi off, lock to ESP-NOW channel
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Full 240MHz native ESP32-S3 CPU clock for smooth anti-aliased rendering
    setCpuFrequencyMhz(240);
    Serial.printf("[SETUP] CPU frequency set to %u MHz.\n", getCpuFrequencyMhz());

    // 2. Park BOTH chip selects HIGH before any initialization
    pinMode(PIN_CS_A, OUTPUT);
    digitalWrite(PIN_CS_A, HIGH);
    pinMode(PIN_CS_B, OUTPUT);
    digitalWrite(PIN_CS_B, HIGH);

    // 3. Hardware reset pulse on shared RST (GPIO 4)
    Serial.println("[SETUP] Pulsing shared RST on GPIO 4...");
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, HIGH);
    delay(50);
    digitalWrite(PIN_RST, LOW);
    delay(50);
    digitalWrite(PIN_RST, HIGH);
    delay(150);

    // 4. Turn backlight on with LEDC PWM (duty: 180/255 ~70% brightness) to prevent GPIO/LDO thermal stress
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_BL, 0);
    ledcWrite(0, 180);
    Serial.println("[SETUP] Backlight ON via LEDC PWM (duty: 180/255).");

    // 5. Initialize both displays via Arduino_GFX
    Serial.println("[SETUP] Initializing Display A (CS=2, DC=3)...");
    gfxA->begin(SPI_HZ);
    Serial.println("[SETUP] Initializing Display B (CS=1, DC=5, 180 deg)...");
    gfxB->begin(SPI_HZ);
    gfxB->setRotation(2);

    // 6. Visual hardware calibration splash with optical alignment crosshairs (5 seconds)
    Serial.println("[SETUP] Running visual alignment test pattern on both screens (5 seconds)...");
    for (int s = 5; s >= 1; s--) {
        // Screen A (Blue background)
        gfxA->fillScreen(0x041F);
        gfxA->drawCircle(120, 120, 119, 0xFFFF); // White outer perimeter (r=119)
        gfxA->drawCircle(120, 120, 100, 0x07E0); // Green ring (r=100)
        gfxA->drawCircle(120, 120, 60,  0xFFE0); // Yellow ring (r=60)
        gfxA->drawFastVLine(120, 0, 240, 0xF800); // Red vertical centerline (x=120)
        gfxA->drawFastHLine(0, 120, 240, 0xF800); // Red horizontal centerline (y=120)
        // 5px tick marks on horizontal axis across center (-20px to +20px)
        for (int dx = -20; dx <= 20; dx += 5) {
            int tick_h = (dx == 0) ? 15 : ((dx % 10 == 0) ? 10 : 6);
            gfxA->drawFastVLine(120 + dx, 120 - tick_h / 2, tick_h, (dx == 0) ? 0xF800 : 0xFFFF);
        }
        gfxA->setTextColor(0xFFFF);
        gfxA->setTextSize(2);
        gfxA->setCursor(48, 40);
        gfxA->println("SCREEN A");
        gfxA->setTextSize(1);
        gfxA->setCursor(35, 65);
        gfxA->printf("CENTER (120,120) [%ds]\n", s);
        gfxA->setCursor(45, 175);
        gfxA->println("Ticks = 5px step");

        // Screen B (Purple background)
        gfxB->fillScreen(0x780F);
        gfxB->drawCircle(120, 120, 119, 0xFFFF); // White outer perimeter (r=119)
        gfxB->drawCircle(120, 120, 100, 0x07E0); // Green ring (r=100)
        gfxB->drawCircle(120, 120, 60,  0xFFE0); // Yellow ring (r=60)
        gfxB->drawFastVLine(120, 0, 240, 0xF800); // Red vertical centerline (x=120)
        gfxB->drawFastHLine(0, 120, 240, 0xF800); // Red horizontal centerline (y=120)
        // 5px tick marks on horizontal axis across center (-20px to +20px)
        for (int dx = -20; dx <= 20; dx += 5) {
            int tick_h = (dx == 0) ? 15 : ((dx % 10 == 0) ? 10 : 6);
            gfxB->drawFastVLine(120 + dx, 120 - tick_h / 2, tick_h, (dx == 0) ? 0xF800 : 0xFFFF);
        }
        gfxB->setTextColor(0xFFFF);
        gfxB->setTextSize(2);
        gfxB->setCursor(48, 40);
        gfxB->println("SCREEN B");
        gfxB->setTextSize(1);
        gfxB->setCursor(35, 65);
        gfxB->printf("CENTER (120,120) [%ds]\n", s);
        gfxB->setCursor(45, 175);
        gfxB->println("Ticks = 5px step");

        delay(1000);
    }

    gfxA->fillScreen(0x0000);
    gfxB->fillScreen(0x0000);

    // 7. Initialize LVGL virtual 240x480 display
    Serial.println("[SETUP] Initializing LVGL virtual display (240x480)...");
    lv_init();
    static lv_color_t lv_draw_buf[VDISP_W * 60];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, lv_draw_buf, NULL, VDISP_W * 60);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = VDISP_W;
    disp_drv.ver_res = VDISP_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.monitor_cb = disp_monitor_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // 7b. Initialize EEZ Studio UI (creates objects.main with Screen B widgets)
    ui_init();

    // Pure OLED pitch black background
    lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(objects.main, LV_OPA_COVER, LV_PART_MAIN);

    // Style Screen A concentric arcs (Top Screen: centered at 120, 120)
    if (objects.rpm_arc) lv_obj_set_pos(objects.rpm_arc, 10, 10);
    if (objects.throttle_arc) lv_obj_set_pos(objects.throttle_arc, 30, 30);
    if (objects.eff_arc) lv_obj_set_pos(objects.eff_arc, 50, 50);
    setup_arc_style(objects.rpm_arc, 0, 8000, 0x141a24, 0x00e676, 8);
    setup_arc_style(objects.throttle_arc, 0, 100, 0x141a24, 0x00e5ff, 6);
    setup_arc_style(objects.eff_arc, 0, 20, 0x141a24, 0xffd600, 6);

    // Style Screen B concentric arcs (Bottom Screen: centered at 120, 360)
    if (objects.coolant_arc) lv_obj_set_pos(objects.coolant_arc, 10, 250);
    if (objects.brake_arc) lv_obj_set_pos(objects.brake_arc, 30, 270);
    if (objects.fuel_arc) lv_obj_set_pos(objects.fuel_arc, 50, 290);
    lv_arc_set_range(objects.coolant_arc, 40, 120);
    lv_arc_set_range(objects.brake_arc, 0, 100);
    lv_arc_set_range(objects.fuel_arc, 0, 100);
    setup_arc_style(objects.coolant_arc, 40, 120, 0x141a24, 0x00e676, 8);
    setup_arc_style(objects.brake_arc, 0, 100, 0x141a24, 0x00e5ff, 6);
    setup_arc_style(objects.fuel_arc, 0, 100, 0x141a24, 0x00e676, 6);

    // Explicitly enforce centered 7-seg speed_value across full 240px width (Screen A)
    if (objects.speed_value) {
        lv_obj_set_pos(objects.speed_value, 0, 78);
        lv_obj_set_size(objects.speed_value, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.speed_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.speed_value, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    if (objects.speed_label) {
        lv_obj_set_pos(objects.speed_label, 0, 122);
        lv_obj_set_size(objects.speed_label, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.speed_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.speed_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }

    // Explicitly enforce centered 7-seg left_distance_value across full 240px width (Screen B)
    if (objects.left_distance_value) {
        lv_obj_set_pos(objects.left_distance_value, 0, 318);
        lv_obj_set_size(objects.left_distance_value, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.left_distance_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.left_distance_value, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    if (objects.left_distance_label) {
        lv_obj_set_pos(objects.left_distance_label, 0, 362);
        lv_obj_set_size(objects.left_distance_label, 240, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.left_distance_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.left_distance_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }

    // Explicitly enforce centered lower telemetry rows (Screen A)
    if (objects.eff_value) {
        lv_obj_set_pos(objects.eff_value, 10, 159);
        lv_obj_set_size(objects.eff_value, 107, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.eff_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.eff_label) {
        lv_obj_set_pos(objects.eff_label, 121, 159);
        lv_obj_set_style_text_align(objects.eff_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.eff_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }
    if (objects.throttle_value) {
        lv_obj_set_pos(objects.throttle_value, 10, 179);
        lv_obj_set_size(objects.throttle_value, 112, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.throttle_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.throttle_label) {
        lv_obj_set_pos(objects.throttle_label, 126, 179);
        lv_obj_set_style_text_align(objects.throttle_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.throttle_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }
    if (objects.rpm__value) {
        lv_obj_set_pos(objects.rpm__value, 10, 197);
        lv_obj_set_size(objects.rpm__value, 112, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.rpm__value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.rpm__label) {
        lv_obj_set_pos(objects.rpm__label, 126, 197);
        lv_obj_set_style_text_align(objects.rpm__label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.rpm__label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }

    // Explicitly enforce centered lower telemetry rows (Screen B)
    if (objects.fuel_value) {
        lv_obj_set_pos(objects.fuel_value, 10, 401);
        lv_obj_set_size(objects.fuel_value, 112, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.fuel_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.fuel_label) {
        lv_obj_set_pos(objects.fuel_label, 126, 401);
        lv_obj_set_style_text_align(objects.fuel_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.fuel_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }
    if (objects.brake_value) {
        lv_obj_set_pos(objects.brake_value, 10, 420);
        lv_obj_set_size(objects.brake_value, 112, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.brake_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.brake_label) {
        lv_obj_set_pos(objects.brake_label, 126, 420);
        lv_obj_set_style_text_align(objects.brake_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.brake_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }
    if (objects.coolant_value) {
        lv_obj_set_pos(objects.coolant_value, 10, 439);
        lv_obj_set_size(objects.coolant_value, 110, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(objects.coolant_value, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    }
    if (objects.coolant_label) {
        lv_obj_set_pos(objects.coolant_label, 124, 439);
        lv_obj_set_style_text_align(objects.coolant_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_set_style_text_color(objects.coolant_label, lv_color_hex(0x6b7d96), LV_PART_MAIN);
    }

    // Immediately load objects.main
    lv_scr_load(objects.main);

    // 8. ESP-NOW
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.println("[SETUP] ESP-NOW receiver registered.");
    } else {
        Serial.println("[ERROR] Failed to initialize ESP-NOW!");
    }

    Serial.println("[SETUP] Setup complete! Dual screens running.");
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    uint32_t now = millis();

    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);
    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

    // Rate-limit UI updates to 50Hz (every 20ms) for smooth animation sweep
    static EspDashTelemetry active_pkt = {0};
    static uint32_t last_ui_update = 0;
    if (now - last_ui_update >= 20) {
        last_ui_update = now;

        if (is_demo) {
            float phase = now * 0.002f;
            active_pkt.rpm = (uint16_t)(3000 + sin(phase) * 2800 + sin(phase * 3.0f) * 500);
            active_pkt.speed_kmh_x10 = (uint16_t)((90 + sin(phase * 0.8f) * 40) * 10);
            active_pkt.water_temp_x10 = (int16_t)((92 + sin(phase * 0.2f) * 6) * 10);
            active_pkt.steering_deg = (int16_t)(sin(phase * 1.2f) * 180);
            active_pkt.throttle_pct = (uint8_t)(50 + sin(phase * 1.5f) * 45);
            active_pkt.brake_pct = (uint8_t)(max(0.0f, -sin(phase * 1.5f) * 80.0f));
            active_pkt.fuel_consumption_x10 = (uint8_t)(85 + sin(phase * 0.1f) * 20);
            active_pkt.battery_mv = (uint16_t)((13.0f + sin(phase * 0.8f) * 1.8f) * 1000);
            active_pkt.gear = (uint8_t)(3 + ((int)(now * 0.0004f) % 4));
            active_pkt.ambient_temp = (int8_t)(22 + sin(phase * 0.05f) * 6);
            active_pkt.fuel_level_pct = (uint8_t)(65 + sin(phase * 0.08f) * 25);
            active_pkt.flags2 = ESPDASH_FLAG2_FUEL_VALID;
            if (active_pkt.fuel_level_pct < 15) {
                active_pkt.flags2 |= ESPDASH_FLAG2_LOW_FUEL;
            }
        } else {
            active_pkt = current_pkt;
        }

        update_dual_ui(active_pkt, link, is_demo);
    }

    lv_timer_handler();

    static uint32_t last_link_log = 0, last_pkt_count = 0, last_frame_count = 0;
    if (now - last_link_log >= 2000) {
        uint32_t n = pkt_count;
        uint32_t f = g_frame_count;
        float hz = (n - last_pkt_count) * 1000.0f / (now - last_link_log);
        float fps = (f - last_frame_count) * 1000.0f / (now - last_link_log);
        float temp_c = temperatureRead();
        last_link_log = now;
        last_pkt_count = n;
        last_frame_count = f;
        const char *st = (link == LINK_LIVE) ? "LIVE"
                       : (link == LINK_LOST) ? "LOST" : "SEARCHING (DEMO)";
        float avg_l100 = (active_pkt.fuel_consumption_x10 >= 20 && active_pkt.fuel_consumption_x10 <= 250)
                         ? (active_pkt.fuel_consumption_x10 / 10.0f) : 7.2f;
        uint16_t est_rng = (uint16_t)((active_pkt.fuel_level_pct * 50.0f / 100.0f) / avg_l100 * 100.0f);
        Serial.printf("[LINK] %s ch:%u rate:%.1fHz pkts:%lu fps:%.1f temp:%.1fC | A: rpm:%u spd:%.1f thr:%u%% eff:%.1fL | B: cool:%dC brk:%u%% fuel:%u%% rng:%ukm\n",
                      st, actual_channel(), hz, (unsigned long)n, fps, temp_c,
                      active_pkt.rpm, active_pkt.speed_kmh_x10 / 10.0f, active_pkt.throttle_pct,
                      active_pkt.fuel_consumption_x10 / 10.0f,
                      active_pkt.water_temp_x10 / 10, active_pkt.brake_pct,
                      active_pkt.fuel_level_pct, est_rng);
    }

    delay(2);
}
