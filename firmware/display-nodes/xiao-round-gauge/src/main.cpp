#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <lvgl.h>
#include "ui.h"
#include "screens.h"

#if ESPDASH_NODE_WIFI
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#endif

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
// With ESPDASH_NODE_WIFI=0 (the default) this node never associates to
// Wi-Fi, so it locks to ESPDASH_ESPNOW_CHANNEL in setup() - the same fixed
// constant the gateway uses - and never needs to move. The channel-sweep
// logic below only runs when the flag is on: in that mode the node's own
// Wi-Fi may park the radio on whatever channel a real AP uses, which
// silently deafens it to the gateway's fixed-channel broadcasts whenever the
// two don't match (measured on the bench: 0 packets received, holding
// steady on the AP's channel for 29+ seconds). Sweeping is how a Wi-Fi-
// connected node finds the gateway anyway in that case.
static uint8_t  espnow_channel = ESPDASH_ESPNOW_CHANNEL;
static bool     channel_locked = false;
#if ESPDASH_NODE_WIFI
static uint32_t last_channel_hop = 0;
#define CHANNEL_HOP_MS   200    // dwell per channel while searching
#endif
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
// DISPLAY & DOUBLE BUFFER SPRITE
// =========================================================================
static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

// Color definitions (16-bit 565 RGB)
#define COLOR_BG        0x0863 // Very dark blue (#0b0f19)
#define COLOR_CARD      0x10E5 // Dark card bg
#define COLOR_CYAN      0x07FF // Bright neon cyan
#define COLOR_GREEN     0x07E0 // Bright green
#define COLOR_YELLOW    0xFFE0 // Bright yellow
#define COLOR_RED       0xF800 // Bright red
#define COLOR_BLUE      0x041F // Bright blue
#define COLOR_WHITE     0xFFFF // Pure white
#define COLOR_TEXT_MUT  0x8C71 // Muted grey
#define COLOR_DARK_GRAY 0x18E3 // Track bg gray

// =========================================================================
// MULTI-WIFI & WEB OTA SERVER  (ESPDASH_NODE_WIFI=1 only)
// =========================================================================
#if ESPDASH_NODE_WIFI
struct WifiNet { const char *ssid, *pass; };
static const WifiNet WIFI_NETS[] = {
    {"Complex_parking", "12345678"},
    {"Bazanski_ph",     "52288488"},
    {"Bazanski_IS",     "52288488"},
    {"IOT-monday",      "fsdL2Dp*KBU0y#9F&c!Zbq853axj"},
};
static const int WIFI_NET_COUNT = sizeof(WIFI_NETS) / sizeof(WIFI_NETS[0]);

static WebServer webServer(80);
static uint32_t last_wifi_check = 0;

// Simple WebOTA Status & Update HTML Page
const char* ota_index_html = 
"<!DOCTYPE html><html><head><title>xiao-round-gauge.local WebOTA</title>"
"<style>body{background:#0b0f19;color:#fff;font-family:sans-serif;text-align:center;padding:50px;}"
"h1{color:#00f0ff;} .card{background:#141b2d;padding:30px;border-radius:12px;display:inline-block;border:1px solid #222e48;}"
"input[type=file]{margin:20px 0;} input[type=submit]{background:#00f0ff;color:#000;border:none;padding:10px 20px;font-weight:bold;border-radius:6px;cursor:pointer;}</style></head>"
"<body><div class='card'><h1>🏎️ esp32-gauge-round.local</h1>"
"<p>XIAO Round Universal Gauge Wireless Firmware Update</p>"
"<form method='POST' action='/update' enctype='multipart/form-data'>"
"<input type='file' name='update'><br><input type='submit' value='Upload & Flash Firmware'>"
"</form></div></body></html>";

void setup_web_ota() {
    webServer.on("/", HTTP_GET, []() {
        webServer.send(200, "text/html", ota_index_html);
    });
    webServer.on("/update", HTTP_POST, []() {
        webServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK - Rebooting...");
        delay(500);
        ESP.restart();
    }, []() {
        HTTPUpload& upload = webServer.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %u bytes\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });
    webServer.begin();
}
#endif  // ESPDASH_NODE_WIFI

// =========================================================================
// ESP-NOW RECEIVE CALLBACK
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

// =========================================================================
// CIVIC 9G 1.8L ECO-COACHING COLOR LOGIC (AMOLED THEME INTEGRATION)
// =========================================================================
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
// DRAW GAUGES ON 240x240 CIRCULAR DISPLAY SPRITE
// =========================================================================
void render_gauge_ui(const EspDashTelemetry &pkt, LinkState link) {
    spr.fillSprite(COLOR_BG);

    const int cx = 120;
    const int cy = 120;

    // 1. Shift Light Outer LED Arch at top (12 LEDs across top from 210° to 330°)
    float shiftStart = 210.0f * DEG_TO_RAD;
    float shiftEnd = 330.0f * DEG_TO_RAD;
    const int ledCount = 12;
    float rpmPct = min(1.0f, (float)pkt.rpm / 7000.0f);

    for (int i = 0; i < ledCount; i++) {
        float angle = shiftStart + ((float)i / (ledCount - 1)) * (shiftEnd - shiftStart);
        int lx = cx + cos(angle) * 110;
        int ly = cy + sin(angle) * 110;
        float threshold = (float)(i + 1) / ledCount;

        if (rpmPct >= threshold) {
            uint16_t c = COLOR_GREEN;
            if (i >= 8) c = COLOR_RED;
            else if (i >= 4) c = COLOR_YELLOW;
            spr.fillCircle(lx, ly, 4, c);
        } else {
            spr.fillCircle(lx, ly, 4, COLOR_DARK_GRAY);
        }
    }

    // 2. RPM Outer Arc Sweep (45° to 315° clockwise across arc = 270° total sweep, 180° rotated)
    float rpmRatio = min(1.0f, (float)pkt.rpm / 8000.0f);
    uint32_t rpmEndDeg = (45 + (uint32_t)(rpmRatio * 270.0f)) % 360;

    // Background RPM track (smooth anti-aliased)
    spr.drawSmoothArc(cx, cy, 98, 90, 45, 315, COLOR_DARK_GRAY, COLOR_BG, false);

    // Active RPM Arc (smooth anti-aliased)
    if (rpmRatio > 0.01f) {
        uint16_t rpmColor = COLOR_CYAN;
        if (rpmRatio > 0.85f) rpmColor = COLOR_RED;
        else if (rpmRatio > 0.65f) rpmColor = COLOR_YELLOW;
        else if (rpmRatio > 0.35f) rpmColor = COLOR_GREEN;

        spr.drawSmoothArc(cx, cy, 98, 90, 45, rpmEndDeg, rpmColor, COLOR_BG, true);
    }

    // 3. Throttle Position Arc (60° to 120°, 60° sweep, 180° rotated)
    float thRatio = min(1.0f, pkt.throttle_pct / 100.0f);
    uint32_t thEndDeg = 60 + (uint32_t)(thRatio * 60.0f);

    spr.drawSmoothArc(cx, cy, 82, 76, 60, 120, COLOR_DARK_GRAY, COLOR_BG, false);
    if (thRatio > 0.01f) {
        spr.drawSmoothArc(cx, cy, 82, 76, 60, thEndDeg, COLOR_GREEN, COLOR_BG, true);
    }

    // 4. Brake Pressure Arc (240° to 300°, 60° sweep, 180° rotated)
    // The brake switch (a distinct binary signal from the pressure below) is
    // shown by lighting the whole gauge - arc and readout - in alert red
    // instead of drawing a new indicator, since this display has no verified
    // free space for one and a wrongly-placed element is worse than none.
    bool brakeEngaged = (pkt.flags & ESPDASH_FLAG_BRAKE_SWITCH) != 0;
    uint16_t brakeColor = brakeEngaged ? COLOR_RED : COLOR_BLUE;

    float brRatio = min(1.0f, (float)pkt.brake_pct / 100.0f);
    uint32_t brStartDeg = 300 - (uint32_t)(brRatio * 60.0f);

    spr.drawSmoothArc(cx, cy, 82, 76, 240, 300, COLOR_DARK_GRAY, COLOR_BG, false);
    if (brRatio > 0.01f || brakeEngaged) {
        // Engaged-but-near-zero-pressure still needs to show something, so
        // floor the arc to a small visible sliver rather than drawing nothing.
        uint32_t litStartDeg = brakeEngaged ? min((uint32_t)295, brStartDeg) : brStartDeg;
        spr.drawSmoothArc(cx, cy, 82, 76, litStartDeg, 300, brakeColor, COLOR_BG, true);
    }

    // Readout labels on sides (positioned right up against inside of throttle/brake slider arcs)
    spr.setTextColor(COLOR_GREEN, COLOR_BG);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(String(pkt.throttle_pct) + "%", cx - 70, cy);

    spr.setTextColor(brakeColor, COLOR_BG);
    spr.setTextDatum(MR_DATUM);
    spr.drawString(String(pkt.brake_pct) + "%", cx + 70, cy);

    // 5. Steering Angle Dial (Top Part of Screen at 270°)
    float stAngleRad = (pkt.steering_deg / 540.0f) * (PI * 0.4f);
    float stDotAngle = (270.0f * DEG_TO_RAD) + stAngleRad;
    int stX = cx + cos(stDotAngle) * 58;
    int stY = cy + sin(stDotAngle) * 58;
    spr.fillCircle(stX, stY, 3, COLOR_CYAN);

    spr.setTextColor(COLOR_TEXT_MUT, COLOR_BG);
    spr.setTextDatum(TC_DATUM);
    String stStr = (pkt.steering_deg == 0) ? "0°" : (pkt.steering_deg > 0 ? String(pkt.steering_deg) + "°R" : String(abs(pkt.steering_deg)) + "°L");
    spr.drawString(stStr, cx, cy - 50);

    // 6. Central Digital Speedometer Readout
    spr.setTextColor(COLOR_WHITE, COLOR_BG);
    spr.setTextDatum(MC_DATUM);
    uint16_t spd = pkt.speed_kmh_x10 / 10;
    spr.drawString(String(spd), cx, cy + 8, 7); // Font 7 = 7-segment big font or large GLCD

    spr.setTextColor(COLOR_TEXT_MUT, COLOR_BG);
    spr.drawString("KM/H", cx, cy + 32);

    // 7. Coolant Temp Badge (Bottom Left)
    float waterC = pkt.water_temp_x10 / 10.0f;
    bool isOverheat = waterC > 105.0f;
    spr.setTextColor(isOverheat ? COLOR_RED : COLOR_YELLOW, COLOR_BG);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(String((int)waterC) + "°C", cx - 48, cy + 62);

    // 8. Fuel Level Badge (Bottom Center)
    uint8_t fuel_x10 = pkt.fuel_consumption_x10;
    bool isHighCons = fuel_x10 >= 150;
    spr.setTextColor(isHighCons ? COLOR_RED : COLOR_GREEN, COLOR_BG);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(String(fuel_x10 / 10) + "." + String(fuel_x10 % 10) + "L", cx, cy + 62);

    // 9. Gear Indicator Badge (Bottom Right)
    const char* gearStr = "N";
    switch(pkt.gear) {
        case 0: gearStr = "P"; break;
        case 1: gearStr = "R"; break;
        case 2: gearStr = "N"; break;
        case 3: gearStr = "D"; break;
        case 4: gearStr = "S"; break;
        case 5: gearStr = "1"; break;
        case 6: gearStr = "2"; break;
        case 7: gearStr = "3"; break;
        case 8: gearStr = "4"; break;
        case 9: gearStr = "5"; break;
        case 10: gearStr = "6"; break;
        default: gearStr = "D"; break;
    }
    spr.drawCircle(cx + 48, cy + 62, 12, COLOR_CYAN);
    spr.setTextColor(COLOR_WHITE, COLOR_BG);
    spr.drawString(gearStr, cx + 48, cy + 62);

    // 9. Top Status Badge - link state, never a silent fake
    const char *badge;
    uint16_t badge_col;
    switch (link) {
        case LINK_LIVE:
            badge = "ESP-NOW 20Hz";  badge_col = COLOR_CYAN;   break;
        case LINK_LOST:
            badge = "NO LINK";       badge_col = COLOR_RED;    break;
        default:
            badge = "SEARCHING...";  badge_col = COLOR_YELLOW; break;
    }
    spr.setTextColor(badge_col, COLOR_BG);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(badge, cx, 16);

    // Second status line is shared: while searching, show which channel is
    // being probed so a mismatch is diagnosable; otherwise show battery
    // voltage there instead, since a real packet means real data.
    if (link == LINK_SEARCHING && !channel_locked) {
        spr.setTextColor(COLOR_TEXT_MUT, COLOR_BG);
        spr.drawString("ch " + String(espnow_channel), cx, 30);
    } else if (link != LINK_SEARCHING) {
        float battV = pkt.battery_mv / 1000.0f;
        bool lowBatt = battV > 0.0f && battV < 12.0f;
        spr.setTextColor(lowBatt ? COLOR_RED : COLOR_TEXT_MUT, COLOR_BG);
        spr.drawString(String(battV, 1) + "V", cx, 30);
    }

    // Push Sprite to Screen
    spr.pushSprite(0, 0);
}

// =========================================================================
// SETUP
// =========================================================================
uint32_t g_telemetry_rpm = 0;
uint32_t g_telemetry_throttle = 0;

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=================================================================");
    Serial.println(" 🏎️ espDash XIAO ROUND GAUGE (esp32-gauge-round.local)");
    Serial.println(" DISPLAY: GC9A01 240x240 Round TFT (SPI)");
    Serial.println("=================================================================");

    // Turn on display backlight (GPIO 43 on XIAO ESP32-S3 round expansion board)
    pinMode(43, OUTPUT);
    digitalWrite(43, HIGH);

    // Initialize GC9A01 TFT Display
    tft.init();
    tft.setRotation(0); // Hardware rotation 0 (rotates full screen and all elements by 180 degrees)
    tft.fillScreen(TFT_BLACK);

    // Render 3-Second Startup Splash Screen with IP Address or Standalone ESP-NOW
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString("espDash Telemetry", 120, 65, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("XIAO Round Gauge Ready", 120, 110, 2);

#if ESPDASH_NODE_WIFI
    // Wi-Fi connection attempt, hard-bounded to 15s total.
    //
    // This used to call WiFiMulti's run(), wrapped in the same 15s while-loop
    // structure still below - and still hung indefinitely anyway. run() is a
    // single blocking call, and an AP replying "association refused, comeback
    // time" makes the ESP-IDF driver honor that backoff *inside* the call, so
    // control never returns to let the outer loop's deadline check run at
    // all. Measured on the bench: 75+ seconds of total silence with no bound
    // whatsoever, after exactly that AP response.
    //
    // WiFi.begin() returns immediately (the connection happens in the
    // background) and WiFi.status() is a cheap, non-blocking poll, so a loop
    // built on those genuinely enforces a deadline no matter what the driver
    // or a hostile AP does underneath - which is the actual point of having
    // a timeout here at all.
    uint32_t start_connect = millis();
    bool wifi_ok = false;
    for (int i = 0; i < WIFI_NET_COUNT && !wifi_ok && (millis() - start_connect < 15000); i++) {
        WiFi.begin(WIFI_NETS[i].ssid, WIFI_NETS[i].pass);
        uint32_t attempt_start = millis();
        // Budget each network a slice of the total, but never overrun it.
        uint32_t per_net_budget = 15000 / WIFI_NET_COUNT;
        while (millis() - attempt_start < per_net_budget &&
               millis() - start_connect < 15000) {
            if (WiFi.status() == WL_CONNECTED) { wifi_ok = true; break; }
            delay(150);
            String dots = "Connecting";
            int cnt = ((millis() - start_connect) / 400) % 4;
            for (int j = 0; j < cnt; j++) dots += ".";
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.drawCentreString(dots + "   ", 120, 150, 2);
        }
        if (!wifi_ok) WiFi.disconnect();
    }

    if (wifi_ok) {
        String ssidStr = "SSID: " + WiFi.SSID();
        String ipStr   = "IP: " + WiFi.localIP().toString();
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawCentreString(ssidStr, 120, 140, 2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawCentreString(ipStr, 120, 162, 2);

        MDNS.begin("esp32-gauge-round");
        ArduinoOTA.setHostname("esp32-gauge-round");
        ArduinoOTA.begin();
        setup_web_ota();
    } else {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawCentreString("Standalone (ESP-NOW)", 120, 150, 2);
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }
#else
    // No Wi-Fi is ever attempted: no association, so no chance of parking the
    // radio on a channel that doesn't match the gateway's fixed
    // ESPDASH_ESPNOW_CHANNEL. Order matters here - disconnect(true, true)'s
    // second argument stops the radio outright, so WiFi.mode(WIFI_STA) must
    // come AFTER it to restart it into STA mode; reversed, ESP-NOW fails
    // every send with ESP_ERR_ESPNOW_IF (found and fixed on the gateway the
    // same way earlier this session).
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawCentreString("Standalone (ESP-NOW)", 120, 150, 2);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

    spr.setColorDepth(16);
    spr.createSprite(240, 240);

    // Initialize LVGL 8 & EEZ Studio UI (Screen 1)
    lv_init();
    static lv_color_t buf[240 * 10];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 10);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 240;
    disp_drv.flush_cb = [](lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
        uint32_t w = (area->x2 - area->x1 + 1);
        uint32_t h = (area->y2 - area->y1 + 1);
        tft.startWrite();
        tft.setAddrWindow(area->x1, area->y1, w, h);
        tft.pushColors((uint16_t *)&color_p->full, w * h, true);
        tft.endWrite();
        lv_disp_flush_ready(disp);
    };
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ui_init();

    // Ensure pure OLED dark background (#000000) matching AMOLED theme
    if (objects.main) {
        lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(objects.main, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(objects.main, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Apply sleek AMOLED arc styling (track color #161b26 with rounded ends)
    setup_arc_style(objects.rpm_level_arc, 0, 8000, 0x161b26, 0x00e676, 8);
    setup_arc_style(objects.gas_pedal_arc, 0, 100, 0x161b26, 0x00e676, 6);
    setup_arc_style(objects.brake_pressure_arc, 0, 100, 0x161b26, 0x00e5ff, 6);

    // Speed value & label styling
    if (objects.speed_value) {
        lv_obj_clear_flag(objects.speed_value, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(objects.speed_value, lv_color_hex(0xffffff), LV_PART_MAIN);
    }
    if (objects.speed_type_label) {
        lv_obj_clear_flag(objects.speed_type_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(objects.speed_type_label, lv_color_hex(0x00e5ff), LV_PART_MAIN);
    }

    // Clean transparent styling for all widgets
    lv_obj_t* widgets[] = {
        objects.speed_value, objects.speed_type_label, objects.gas_pedal_value,
        objects.brake_pressure_value, objects.battery_voltage_value,
        objects.coolant_temp_value, objects.fuel_level_value, objects.gear_value
    };
    for (int i = 0; i < 8; i++) {
        if (widgets[i]) {
            lv_obj_set_style_bg_opa(widgets[i], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_opa(widgets[i], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_outline_opa(widgets[i], LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_shadow_opa(widgets[i], LV_OPA_TRANSP, LV_PART_MAIN);
        }
    }
    if (objects.gas_pedal_value) lv_obj_set_style_text_color(objects.gas_pedal_value, lv_color_hex(0x00e676), LV_PART_MAIN);
    if (objects.brake_pressure_value) lv_obj_set_style_text_color(objects.brake_pressure_value, lv_color_hex(0x00e5ff), LV_PART_MAIN);
    if (objects.battery_voltage_value) lv_obj_set_style_text_color(objects.battery_voltage_value, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    if (objects.coolant_temp_value) lv_obj_set_style_text_color(objects.coolant_temp_value, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    if (objects.fuel_level_value) lv_obj_set_style_text_color(objects.fuel_level_value, lv_color_hex(0x8c9eb5), LV_PART_MAIN);
    if (objects.gear_value) lv_obj_set_style_text_color(objects.gear_value, lv_color_hex(0xffd600), LV_PART_MAIN);

    // ESP-NOW Setup
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
    }
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
#if ESPDASH_NODE_WIFI
    // Only process OTA & HTTP requests if Wi-Fi is actively connected
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
        webServer.handleClient();
    }
#endif

    uint32_t now = millis();

#if ESPDASH_NODE_WIFI
    // WiFi Maintenance & Auto-Reconnect
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setAutoReconnect(true);
    }
#endif

    // ---- ESP-NOW link supervision & channel discovery -------------------
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);

#if ESPDASH_NODE_WIFI
    if (!live && WiFi.status() != WL_CONNECTED) {
        if (channel_locked) {
            channel_locked = false;
            Serial.println("[ESP-NOW] Link lost, resuming channel scan");
        }
        if (now - last_channel_hop >= CHANNEL_HOP_MS) {
            last_channel_hop = now;
            espnow_channel = (espnow_channel % 13) + 1;
            esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
        }
    }
#endif

    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

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
    } else {
        active_pkt = current_pkt;
    }

    // Keep global telemetry variables synced for EEZ Studio tick functions
    g_telemetry_rpm = active_pkt.rpm;
    g_telemetry_throttle = active_pkt.throttle_pct;

    // Periodic link health logger
    static uint32_t last_link_log = 0, last_pkt_count = 0;
    if (now - last_link_log >= 2000) {
        uint32_t n = pkt_count;
        float hz = (n - last_pkt_count) * 1000.0f / (now - last_link_log);
        last_link_log = now;
        last_pkt_count = n;
        const char *st = (link == LINK_LIVE) ? "LIVE"
                       : (link == LINK_LOST) ? "LOST" : "SEARCHING";
        Serial.printf("[LINK] %s ch:%u rate:%.1fHz pkts:%lu gaps:%lu payload:%u "
                      "rpm:%u spd:%.1f gear:%u thr:%u\n",
                      st, actual_channel(), hz, (unsigned long)n,
                      (unsigned long)pkt_gaps, current_payload_len,
                      current_pkt.rpm, current_pkt.speed_kmh_x10 / 10.0f,
                      current_pkt.gear, current_pkt.throttle_pct);
    }

    // ---- Dual Screen 10-Second Auto Carousel ----
    static uint32_t last_carousel_switch = 0;
    static uint8_t active_screen = 1; // 1 = EEZ Studio UI, 2 = Production UI

    if (now - last_carousel_switch >= 10000) {
        last_carousel_switch = now;
        active_screen = (active_screen == 1) ? 2 : 1;
        if (active_screen == 1) {
            // Wipes screen 2 graphics from TFT framebuffer and forces LVGL to re-render screen 1 completely
            tft.fillScreen(COLOR_BG);
            if (lv_scr_act()) lv_obj_invalidate(lv_scr_act());
        }
        Serial.printf("[CAROUSEL] Switched to Screen %d (%s)\n",
                      active_screen, (active_screen == 1) ? "EEZ Studio UI" : "Production UI");
    }

    if (active_screen == 1) {
        ui_tick();

        static uint32_t last_rpm_col = 0;
        static uint32_t last_thr_col = 0;
        static uint32_t last_brk_col = 0;

        uint32_t rpm_col = get_civic_rpm_color(active_pkt.rpm);
        uint32_t thr_col = get_civic_throttle_color(active_pkt.throttle_pct);
        bool brake_active = (active_pkt.flags & ESPDASH_FLAG_BRAKE_SWITCH) || (active_pkt.brake_pct > 0);
        uint32_t brk_col = brake_active ? 0xff1744 : 0x00e5ff;

        // Screen 1: Update EEZ Studio LVGL Widgets with 20Hz Telemetry & Demo Sweep
        if (objects.rpm_level_arc) lv_arc_set_value(objects.rpm_level_arc, active_pkt.rpm);
        if (rpm_col != last_rpm_col) {
            last_rpm_col = rpm_col;
            if (objects.rpm_level_arc) lv_obj_set_style_arc_color(objects.rpm_level_arc, lv_color_hex(rpm_col), LV_PART_INDICATOR);
        }

        if (objects.speed_value) lv_label_set_text_fmt(objects.speed_value, "%d", active_pkt.speed_kmh_x10 / 10);

        if (objects.gas_pedal_arc) lv_arc_set_value(objects.gas_pedal_arc, active_pkt.throttle_pct);
        if (objects.gas_pedal_value) lv_label_set_text_fmt(objects.gas_pedal_value, "%d%%", active_pkt.throttle_pct);
        if (thr_col != last_thr_col) {
            last_thr_col = thr_col;
            if (objects.gas_pedal_arc) lv_obj_set_style_arc_color(objects.gas_pedal_arc, lv_color_hex(thr_col), LV_PART_INDICATOR);
            if (objects.gas_pedal_value) lv_obj_set_style_text_color(objects.gas_pedal_value, lv_color_hex(thr_col), LV_PART_MAIN);
        }

        if (objects.brake_pressure_arc) lv_arc_set_value(objects.brake_pressure_arc, active_pkt.brake_pct);
        if (objects.brake_pressure_value) lv_label_set_text_fmt(objects.brake_pressure_value, "%d%%", active_pkt.brake_pct);
        if (brk_col != last_brk_col) {
            last_brk_col = brk_col;
            if (objects.brake_pressure_arc) lv_obj_set_style_arc_color(objects.brake_pressure_arc, lv_color_hex(brk_col), LV_PART_INDICATOR);
            if (objects.brake_pressure_value) lv_obj_set_style_text_color(objects.brake_pressure_value, lv_color_hex(brk_col), LV_PART_MAIN);
        }

        if (objects.battery_voltage_value) {
            uint16_t mv = active_pkt.battery_mv;
            if (mv > 0) {
                lv_label_set_text_fmt(objects.battery_voltage_value, "%d.%dV", mv / 1000, (mv % 1000) / 100);
            } else {
                lv_label_set_text(objects.battery_voltage_value, "--.-V");
            }
        }
        if (objects.coolant_temp_value) {
            float c = active_pkt.water_temp_x10 / 10.0f;
            lv_label_set_text_fmt(objects.coolant_temp_value, "%d°C", (int)c);
            lv_obj_set_style_text_color(objects.coolant_temp_value, lv_color_hex(c > 105.0f ? 0xff1744 : 0x8c9eb5), LV_PART_MAIN);
        }
        if (objects.fuel_level_value) {
            uint8_t f = active_pkt.fuel_consumption_x10;
            lv_label_set_text_fmt(objects.fuel_level_value, "%d.%dL", f / 10, f % 10);
            lv_obj_set_style_text_color(objects.fuel_level_value, lv_color_hex(get_civic_efficiency_color(f / 10.0f)), LV_PART_MAIN);
        }
        if (objects.gear_value) lv_label_set_text(objects.gear_value, get_gear_str(active_pkt.gear));

        lv_timer_handler();
    } else {
        // Screen 2: Current Production UI (render_gauge_ui - 100% unchanged)
        render_gauge_ui(active_pkt, link);
    }

    delay(20); // ~50 FPS target
}
