#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <ArduinoOTA.h>

// Shared telemetry wire protocol
#include <EspDashProto.h>

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);

static WiFiMulti wifiMulti;
static WebSocketsServer webSocket = WebSocketsServer(8888);

// Dynamic Widget Storage for Live Layout Preview
struct WidgetConfig {
    String id;
    String type;
    int x;
    int y;
    int radius;
    int thickness;
    int startAngle;
    int endAngle;
    int w;
    int h;
    int fontSize;
    int ledCount;
    int tickCount;
    String text;
    String binding;
    uint16_t color;
    uint16_t bgColor;
    uint16_t trackColor;
};

static std::vector<WidgetConfig> activeWidgets;
static EspDashTelemetry liveTelemetry = {0};

uint16_t parseHexColor(const char* hexStr, uint16_t defaultColor) {
    if (!hexStr || hexStr[0] != '#') return defaultColor;
    long rgb = strtol(hexStr + 1, NULL, 16);
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    return tft.color565(r, g, b);
}

void renderLiveUI() {
    spr.fillSprite(0x0863); // Dark background (#0b0f19)

    for (const auto& w : activeWidgets) {
        int val = 50;
        if (w.binding == "rpm") val = liveTelemetry.rpm;
        else if (w.binding == "speed_kmh") val = liveTelemetry.speed_kmh_x10 / 10;
        else if (w.binding == "gear") val = liveTelemetry.gear;
        else if (w.binding == "throttle") val = liveTelemetry.throttle_pct;
        else if (w.binding == "brake") val = liveTelemetry.brake_pct;
        else if (w.binding == "fuel_pct") val = liveTelemetry.fuel_consumption_x10 / 10;
        else if (w.binding == "water_temp") val = liveTelemetry.water_temp_x10 / 10;
        else if (w.binding == "battery_v") val = liveTelemetry.battery_mv / 1000;
        else if (w.binding == "steering_deg") val = liveTelemetry.steering_deg;

        if (w.type == "card-box") {
            spr.fillRoundRect(w.x, w.y, w.w, w.h, 8, w.bgColor);
            spr.drawRoundRect(w.x, w.y, w.w, w.h, 8, w.color);
        } else if (w.type == "smooth-arc" || w.type == "boost-gauge") {
            int mappedA = map(val, 0, 8000, w.startAngle, w.endAngle);
            spr.drawSmoothArc(w.x, w.y, w.radius, w.radius - w.thickness, w.startAngle, w.endAngle, w.trackColor, 0x0863, true);
            spr.drawSmoothArc(w.x, w.y, w.radius, w.radius - w.thickness, w.startAngle, mappedA, w.color, 0x0863, true);
        } else if (w.type == "shift-lights") {
            float sStart = w.startAngle * DEG_TO_RAD;
            float sEnd = w.endAngle * DEG_TO_RAD;
            int count = w.ledCount > 0 ? w.ledCount : 12;
            float pct = min(1.0f, (float)val / 7000.0f);
            for (int i = 0; i < count; i++) {
                float angle = sStart + ((float)i / (count - 1)) * (sEnd - sStart);
                int lx = w.x + cos(angle) * w.radius;
                int ly = w.y + sin(angle) * w.radius;
                uint16_t c = ((float)(i + 1) / count <= pct) ? w.color : w.trackColor;
                spr.fillCircle(lx, ly, 4, c);
            }
        } else if (w.type == "rev-strip") {
            int count = w.ledCount > 0 ? w.ledCount : 16;
            float pct = min(1.0f, (float)val / 8000.0f);
            int wLed = (w.w - (count - 1) * 3) / count;
            for (int i = 0; i < count; i++) {
                int lx = w.x + i * (wLed + 3);
                uint16_t c = ((float)(i + 1) / count <= pct) ? w.color : w.trackColor;
                spr.fillRoundRect(lx, w.y, wLed, w.h, 2, c);
            }
        } else if (w.type == "digital-value") {
            spr.setTextColor(w.color, 0x0863);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(String(val).c_str(), w.x, w.y, w.fontSize > 40 ? 6 : 4);
        } else if (w.type == "text-label") {
            spr.setTextColor(w.color, 0x0863);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(w.text.c_str(), w.x, w.y, 2);
        } else if (w.type == "bar-slider") {
            int fillW = map(val, 0, 100, 0, w.w);
            spr.fillRect(w.x, w.y, w.w, w.h, w.bgColor);
            spr.fillRect(w.x, w.y, fillW, w.h, w.color);
        } else if (w.type == "status-badge") {
            spr.fillCircle(w.x, w.y, 16, w.bgColor);
            spr.drawCircle(w.x, w.y, 16, w.color);
            spr.setTextColor(w.color, w.bgColor);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(w.text.c_str(), w.x, w.y, 2);
        } else if (w.type == "speed-sign") {
            spr.fillCircle(w.x, w.y, 22, TFT_WHITE);
            spr.drawCircle(w.x, w.y, 22, TFT_RED);
            spr.setTextColor(TFT_BLACK, TFT_WHITE);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(w.text.c_str(), w.x, w.y, 4);
        } else if (w.type == "nav-turn-arrow") {
            spr.fillRoundRect(w.x - 30, w.y - 30, 60, 60, 8, w.bgColor);
            spr.drawRoundRect(w.x - 30, w.y - 30, 60, 60, 8, w.color);
            spr.setTextColor(w.color, w.bgColor);
            spr.setTextDatum(MC_DATUM);
            spr.drawString("↗", w.x, w.y, 4);
        }
    }

    spr.pushSprite(0, 0);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        DynamicJsonDocument doc(8192);
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            String cmd = doc["cmd"] | "";
            if (cmd == "UPDATE_LAYOUT") {
                activeWidgets.clear();
                JsonArray widgets = doc["widgets"];
                for (JsonObject item : widgets) {
                    WidgetConfig w;
                    w.id = item["id"] | "";
                    w.type = item["type"] | "";
                    w.x = item["x"] | 0;
                    w.y = item["y"] | 0;
                    w.radius = item["radius"] | 50;
                    w.thickness = item["thickness"] | 12;
                    w.startAngle = item["startAngle"] | 135;
                    w.endAngle = item["endAngle"] | 405;
                    w.w = item["w"] | 100;
                    w.h = item["h"] | 20;
                    w.fontSize = item["fontSize"] | 20;
                    w.ledCount = item["ledCount"] | 12;
                    w.tickCount = item["tickCount"] | 9;
                    w.text = item["text"] | "";
                    w.binding = item["binding"] | "";
                    w.color = parseHexColor(item["color"] | "#00f0ff", TFT_CYAN);
                    w.bgColor = parseHexColor(item["bgColor"] | "#141b2d", TFT_DARKGREY);
                    w.trackColor = parseHexColor(item["trackColor"] | "#1e2942", TFT_NAVY);
                    activeWidgets.push_back(w);
                }
                renderLiveUI();
            } else if (cmd == "TELEMETRY_TICK") {
                JsonObject data = doc["data"];
                liveTelemetry.rpm = data["rpm"] | liveTelemetry.rpm;
                liveTelemetry.speed_kmh_x10 = (data["speed_kmh"] | 0) * 10;
                liveTelemetry.gear = data["gear"] | liveTelemetry.gear;
                liveTelemetry.throttle_pct = data["throttle"] | liveTelemetry.throttle_pct;
                liveTelemetry.brake_pct = data["brake"] | liveTelemetry.brake_pct;
                liveTelemetry.fuel_consumption_x10 = (data["fuel_pct"] | 0) * 10;
                liveTelemetry.water_temp_x10 = (data["water_temp"] | 0) * 10;
                liveTelemetry.battery_mv = (data["battery_v"] | 13.8) * 1000;
                renderLiveUI();
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
    spr.setColorDepth(16);
    spr.createSprite(240, 240); // Standard round display size

    // Display booting splash
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_CYAN);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("espDash Preview Agent", 120, 100, 4);
    spr.drawString("Live WebSockets Ready", 120, 140, 2);
    spr.pushSprite(0, 0);

    wifiMulti.addAP("Complex_parking", "");
    wifiMulti.addAP("Bazanski_ph", "");
    wifiMulti.addAP("Bazanski_IS", "");
    wifiMulti.addAP("IOT-monday", "");

    if (wifiMulti.run() == WL_CONNECTED) {
        MDNS.begin("esp32-preview-gauge");
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        ArduinoOTA.begin();
    }
}

void loop() {
    webSocket.loop();
    ArduinoOTA.handle();
}
