#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <SPI.h>

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

// ---- ESP-NOW channel discovery -----------------------------------------
// The gateway transmits on whatever channel its Wi-Fi AP uses. A node pinned
// to channel 1 simply never hears a gateway on channel 6 - a silent, total
// failure that gets more likely with every gauge added. So sweep the channels
// until a valid packet arrives, then lock on.
static uint8_t  espnow_channel = 1;
static bool     channel_locked = false;
static uint32_t last_channel_hop = 0;
#define CHANNEL_HOP_MS   200    // dwell per channel while searching
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
// MULTI-WIFI & WEB OTA SERVER
// =========================================================================
static WiFiMulti wifiMulti;
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
    float brRatio = min(1.0f, (float)pkt.brake_pct / 100.0f);
    uint32_t brStartDeg = 300 - (uint32_t)(brRatio * 60.0f);

    spr.drawSmoothArc(cx, cy, 82, 76, 240, 300, COLOR_DARK_GRAY, COLOR_BG, false);
    if (brRatio > 0.01f) {
        spr.drawSmoothArc(cx, cy, 82, 76, brStartDeg, 300, COLOR_BLUE, COLOR_BG, true);
    }

    // Readout labels on sides (positioned right up against inside of throttle/brake slider arcs)
    spr.setTextColor(COLOR_GREEN, COLOR_BG);
    spr.setTextDatum(ML_DATUM);
    spr.drawString(String(pkt.throttle_pct) + "%", cx - 70, cy);

    spr.setTextColor(COLOR_BLUE, COLOR_BG);
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
    uint8_t fuel = pkt.fuel_pct;
    bool isLowFuel = fuel <= 15;
    spr.setTextColor(isLowFuel ? COLOR_RED : COLOR_GREEN, COLOR_BG);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("F:" + String(fuel) + "%", cx, cy + 62);

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

    // While searching, show which channel is being probed so a mismatch is
    // diagnosable from the gauge itself.
    if (link == LINK_SEARCHING && !channel_locked) {
        spr.setTextColor(COLOR_TEXT_MUT, COLOR_BG);
        spr.drawString("ch " + String(espnow_channel), cx, 30);
    }

    // Push Sprite to Screen
    spr.pushSprite(0, 0);
}

// =========================================================================
// SETUP
// =========================================================================
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

    // Configure Multi-Wi-Fi Networks
    wifiMulti.addAP("Complex_parking", "12345678");
    wifiMulti.addAP("Bazanski_ph", "52288488");
    wifiMulti.addAP("Bazanski_IS", "52288488");
    wifiMulti.addAP("IOT-monday", "fsdL2Dp*KBU0y#9F&c!Zbq853axj");

    // Render 3-Second Startup Splash Screen with IP Address or Standalone ESP-NOW
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString("espDash Telemetry", 120, 65, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("XIAO Round Gauge Ready", 120, 110, 2);

    // Wi-Fi connection attempt with strict 15-second timeout
    uint32_t start_connect = millis();
    bool wifi_ok = false;
    while (millis() - start_connect < 15000) {
        if (wifiMulti.run() == WL_CONNECTED) {
            wifi_ok = true;
            break;
        }
        delay(150);
        String dots = "Connecting";
        int cnt = ((millis() - start_connect) / 400) % 4;
        for (int i = 0; i < cnt; i++) dots += ".";
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawCentreString(dots + "   ", 120, 150, 2);
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
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Lock channel for lag-free ESP-NOW
    }

    delay(2000);

    spr.setColorDepth(16);
    spr.createSprite(240, 240);

    // ESP-NOW Setup
    // Wi-Fi STA defaults to modem sleep, waking only on DTIM beacons - so an
    // associated node sleeps through most ESP-NOW broadcasts. Measured on the
    // bench: 3.4-8.8 Hz received against the gateway's 20 Hz. Disabling power
    // save is what makes the link actually deliver 20 Hz.
    esp_wifi_set_ps(WIFI_PS_NONE);

    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
    }
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    // Only process OTA & HTTP requests if Wi-Fi is actively connected
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
        webServer.handleClient();
    }

    uint32_t now = millis();

    // WiFi Maintenance & Auto-Reconnect
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setAutoReconnect(true);
    }

    // ---- ESP-NOW link supervision & channel discovery -------------------
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);

    // Only sweep when standalone: with Wi-Fi up the radio is already parked on
    // the AP's channel and moving it would drop the connection and OTA.
    if (!live && WiFi.status() != WL_CONNECTED) {
        if (channel_locked) {
            // We had a link and lost it - the gateway may have moved networks.
            channel_locked = false;
            Serial.println("[ESP-NOW] Link lost, resuming channel scan");
        }
        if (now - last_channel_hop >= CHANNEL_HOP_MS) {
            last_channel_hop = now;
            espnow_channel = (espnow_channel % 13) + 1;
            esp_wifi_set_channel(espnow_channel, WIFI_SECOND_CHAN_NONE);
        }
    }

    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);

    // Fall back to the bench sweep only when no gateway has ever been seen, so
    // a real dropout is never disguised as live data.
    bool is_demo = (link == LINK_SEARCHING);

    EspDashTelemetry active_pkt = {0};   // fields the demo does not set read 0
    if (is_demo) {
        float phase = now * 0.002f;
        active_pkt.rpm = 3000 + sin(phase) * 2800 + sin(phase * 3.0f) * 500;
        active_pkt.speed_kmh_x10 = (uint16_t)((90 + sin(phase * 0.8f) * 40) * 10);
        active_pkt.water_temp_x10 = (int16_t)((92 + sin(phase * 0.2f) * 6) * 10);
        active_pkt.steering_deg = (int16_t)(sin(phase * 1.2f) * 180);
        active_pkt.throttle_pct = (uint8_t)(50 + sin(phase * 1.5f) * 45);
        active_pkt.brake_pct = (uint8_t)(max(0.0f, -sin(phase * 1.5f) * 80.0f));
        active_pkt.gear = 4; // D
    } else {
        active_pkt = current_pkt;
    }

    // Periodic link health, so "it looks connected" can be checked rather than
    // eyeballed: packet rate should sit at the gateway's 20 Hz and gaps at 0.
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

    render_gauge_ui(active_pkt, link);
    delay(20); // ~50 FPS target
}
