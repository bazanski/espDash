#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "lvgl.h"
#include "bsp/lvgl_port.h"
#include "bsp/lcd_bl_pwm_bsp.h"
#include "bsp/user_config.h"
#include "EspDashProto.h"

#include "ui/ui.h"
#include "ui/screens.h"

#define LINK_TIMEOUT_MS 1500

// External function defined in ST7701 display driver
extern "C" void release_st7701_spi_pins(void);

// ESP-NOW State Supervision
static uint32_t last_pkt_rx_time = 0;
static uint32_t pkt_count = 0;
static uint32_t pkt_gaps = 0;
static uint16_t last_seq = 0xFFFF;
static bool     ever_linked = false;
static EspDashTelemetry current_pkt = {0};

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
        pkt_gaps++;
    }

    last_seq = seq;
    last_pkt_rx_time = millis();
    pkt_count++;
    ever_linked = true;
    current_pkt = *pkt;
}

// Global LVGL Chart Series Handles
static lv_chart_series_t *ser_throttle = NULL;
static lv_chart_series_t *ser_brake    = NULL;

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=================================================================");
    Serial.println(" 🏎️ espDash Waveshare ESP32-S3-LCD-3.16 (Racelab Telemetry Display)");
    Serial.println(" DISPLAY: ST7701 RGB Parallel 820x320 LCD (Landscape)");
    Serial.println("=================================================================");

    // Initialize ST7701 RGB parallel display driver & FreeRTOS LVGL task
    lvgl_port_init();

    // Turn on LCD backlight to full brightness
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

    // Release 3-wire SPI pins after panel initialization
    release_st7701_spi_pins();

    // Setup WiFi Radio for ESP-NOW (No WiFi association overhead)
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Lock LVGL mutex to build EEZ Studio UI & configure chart
    if (lvgl_port_lock(500)) {
        ui_init();

        // Set dark background matching Racelab telemetry aesthetic
        if (objects.main) {
            lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x0f111a), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(objects.main, LV_OPA_COVER, LV_PART_MAIN);
        }

        // Configure Racelab-Style 10-Second Rolling Telemetry Chart
        if (objects.history_chart) {
            lv_chart_set_type(objects.history_chart, LV_CHART_TYPE_LINE);
            lv_chart_set_update_mode(objects.history_chart, LV_CHART_UPDATE_MODE_SHIFT);
            lv_chart_set_range(objects.history_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
            lv_chart_set_point_count(objects.history_chart, 100); // 100 samples @ 10Hz = 10 sec rolling window

            // Remove default circular points on line nodes for a sleek Racelab look
            lv_obj_set_style_size(objects.history_chart, 0, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(objects.history_chart, lv_color_hex(0x141824), LV_PART_MAIN);
            lv_obj_set_style_border_color(objects.history_chart, lv_color_hex(0x2a2f45), LV_PART_MAIN);
            lv_obj_set_style_line_width(objects.history_chart, 2, LV_PART_ITEMS);

            // Add 2 Time-Synchronized Series
            ser_throttle = lv_chart_add_series(objects.history_chart, lv_color_hex(0x00FF00), LV_CHART_AXIS_PRIMARY_Y); // Neon Green
            ser_brake    = lv_chart_add_series(objects.history_chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y); // Bright Red

            // Pre-fill chart with zero values
            for (int i = 0; i < 100; i++) {
                lv_chart_set_next_value(objects.history_chart, ser_throttle, 0);
                lv_chart_set_next_value(objects.history_chart, ser_brake, 0);
            }
        }

        // Configure Bar Ranges & Colors
        if (objects.throttle_bar) {
            lv_bar_set_range(objects.throttle_bar, 0, 100);
            lv_obj_set_style_bg_color(objects.throttle_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
        }
        if (objects.brake_bar) {
            lv_bar_set_range(objects.brake_bar, 0, 100);
            lv_obj_set_style_bg_color(objects.brake_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        }
        if (objects.rpm_bar) {
            lv_bar_set_range(objects.rpm_bar, 0, 7000);
            lv_obj_set_style_bg_color(objects.rpm_bar, lv_color_hex(0x00BFFF), LV_PART_INDICATOR);
        }

        lvgl_port_unlock();
    }

    // Register ESP-NOW Receiver Callback
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);
    }
}

void loop() {
    uint32_t now = millis();

    // Link Supervision & Demo Mode Generation
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);
    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

    EspDashTelemetry active_pkt = {0};
    if (is_demo) {
        float phase = now * 0.002f;
        active_pkt.rpm = (uint16_t)(3500 + sin(phase) * 3200);
        active_pkt.speed_kmh_x10 = (uint16_t)((110 + sin(phase * 0.8f) * 50) * 10);
        active_pkt.throttle_pct = (uint8_t)(max(0.0f, sin(phase * 1.5f) * 100.0f));
        active_pkt.brake_pct = (uint8_t)(max(0.0f, -sin(phase * 1.5f) * 90.0f));
        active_pkt.water_temp_x10 = 920;
        active_pkt.fuel_pct = 82;
        active_pkt.battery_mv = 13800;
        active_pkt.gear = (uint8_t)(1 + ((int)(now * 0.0004f) % 6));
    } else {
        active_pkt = current_pkt;
    }

    // Thread-safe update of UI elements (FreeRTOS mutex guard)
    if (lvgl_port_lock(20)) {
        // 10 Hz (100ms) Chart Feed for 10-Second Rolling Window
        static uint32_t last_chart_update = 0;
        if (now - last_chart_update >= 100) {
            last_chart_update = now;
            if (objects.history_chart && ser_throttle && ser_brake) {
                lv_chart_set_next_value(objects.history_chart, ser_throttle, active_pkt.throttle_pct);
                lv_chart_set_next_value(objects.history_chart, ser_brake, active_pkt.brake_pct);
            }
        }

        // Update Telemetry Bars (0-100% Throttle, 0-100% Brake, 0-7000 RPM)
        if (objects.throttle_bar) lv_bar_set_value(objects.throttle_bar, active_pkt.throttle_pct, LV_ANIM_OFF);
        if (objects.brake_bar) lv_bar_set_value(objects.brake_bar, active_pkt.brake_pct, LV_ANIM_OFF);
        if (objects.rpm_bar) lv_bar_set_value(objects.rpm_bar, min((uint16_t)7000, active_pkt.rpm), LV_ANIM_OFF);

        ui_tick();
        lvgl_port_unlock();
    }

    delay(20); // ~50 FPS target
}
