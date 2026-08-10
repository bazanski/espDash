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
    spr.fillSprite(0x0863); // Dark background

    for (const auto& w : activeWidgets) {
        int val = 0;
        if (w.binding == "rpm") val = liveTelemetry.rpm;
        else if (w.binding == "speed_kmh") val = liveTelemetry.speed_kmh_x10 / 10;
        else if (w.binding == "gear") val = liveTelemetry.gear;
        else if (w.binding == "fuel_pct") val = liveTelemetry.fuel_pct;
        else if (w.binding == "water_temp") val = liveTelemetry.water_temp_x10 / 10;

        if (w.type == "card-box") {
            spr.fillRoundRect(w.x, w.y, w.w, w.h, 8, w.bgColor);
            spr.drawRoundRect(w.x, w.y, w.w, w.h, 8, w.color);
        } else if (w.type == "smooth-arc") {
            int mappedA = map(val, 0, 9000, w.startAngle, w.endAngle);
            spr.drawSmoothArc(w.x, w.y, w.radius, w.radius - w.thickness, w.startAngle, w.endAngle, w.trackColor, 0x0863, true);
            spr.drawSmoothArc(w.x, w.y, w.radius, w.radius - w.thickness, w.startAngle, mappedA, w.color, 0x0863, true);
        } else if (w.type == "digital-value") {
            spr.setTextColor(w.color, 0x0863);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(String(val).c_str(), w.x, w.y, w.fontSize > 30 ? 4 : 2);
        } else if (w.type == "text-label") {
            spr.setTextColor(w.color, 0x0863);
            spr.setTextDatum(MC_DATUM);
            spr.drawString(w.text.c_str(), w.x, w.y, 2);
        } else if (w.type == "bar-slider") {
            int fillW = map(val, 0, 100, 0, w.w);
            spr.fillRect(w.x, w.y, w.w, w.h, w.bgColor);
            spr.fillRect(w.x, w.y, fillW, w.h, w.color);
        }
    }

    spr.pushSprite(0, 0);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        DynamicJsonDocument doc(4096);
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
                liveTelemetry.fuel_pct = data["fuel_pct"] | liveTelemetry.fuel_pct;
                liveTelemetry.water_temp_x10 = (data["water_temp"] | 0) * 10;
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
    spr.createSprite(480, 272); // ESP32-S3-LCD-3.14 default size

    // Display booting splash
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_CYAN);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("espDash Preview Agent", 240, 120, 4);
    spr.drawString("ESP32-S3-LCD-3.14 Ready", 240, 160, 2);
    spr.pushSprite(0, 0);

    wifiMulti.addAP("Complex_parking", "");
    wifiMulti.addAP("Bazanski_ph", "");
    wifiMulti.addAP("Bazanski_IS", "");
    wifiMulti.addAP("IOT-monday", "");

    if (wifiMulti.run() == WL_CONNECTED) {
        MDNS.begin("esp32-s3-lcd-314");
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        ArduinoOTA.begin();
    }
}

void loop() {
    webSocket.loop();
    ArduinoOTA.handle();
}
