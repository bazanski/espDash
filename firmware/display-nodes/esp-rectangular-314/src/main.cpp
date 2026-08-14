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
#include "canlog.h"
#include "bsp/sdcard_bsp.h"

#define LINK_TIMEOUT_MS 1500

// BOOT button. GPIO0 doubles as the ST7701's 3-wire SPI CS during panel init;
// it only becomes usable as an input after release_st7701_spi_pins().
#define REC_BUTTON_PIN     0
#define REC_DEBOUNCE_MS    50

// External function defined in ST7701 display driver
extern "C" void release_st7701_spi_pins(void);

// ESP-NOW State Supervision
static uint32_t last_pkt_rx_time = 0;
static uint32_t pkt_count = 0;
static uint32_t pkt_gaps = 0;
static uint16_t last_seq = 0xFFFF;
static bool     ever_linked = false;
static EspDashTelemetry current_pkt = {0};
// Retained so ESPDASH_HAS() can distinguish "gateway sent 0" from "old
// gateway never sent this field" - it was previously parsed and discarded,
// which matters once logged data is being analysed.
static uint16_t current_payload_len = 0;

enum LinkState { LINK_SEARCHING, LINK_LIVE, LINK_LOST };

// ESP-NOW Receive Callback
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
    // Raw CAN log batches share this callback. canlog_on_packet() ignores
    // anything that is not a canlog message and never blocks, so ordering
    // here is not sensitive.
    canlog_on_packet(incomingData, len);

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
    current_payload_len = payload_len;
}

// =========================================================================
// RECORD BUTTON + STATUS LABEL
// =========================================================================
static lv_obj_t *rec_label = NULL;

static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t node_cmd_seq = 0;

// Tell the gateway whether we want raw CAN streamed to us. Repeated rather
// than edge-triggered: ESP-NOW is fire-and-forget, so a single lost "start"
// would mean recording an empty file. See EspDashProto.h for the full
// rationale - the gateway forgets us if these stop arriving, which is what
// makes the link self-healing in both directions.
static void send_node_cmd(bool want) {
    uint8_t pkt[sizeof(EspDashHeader) + sizeof(EspDashNodeCmd)];
    EspDashHeader *h = (EspDashHeader *)pkt;
    h->magic       = ESPDASH_MAGIC;
    h->msg_type    = ESPDASH_MSG_NODE_CMD;
    h->proto_major = ESPDASH_PROTO_MAJOR;
    h->proto_minor = ESPDASH_PROTO_MINOR;
    h->payload_len = sizeof(EspDashNodeCmd);
    h->seq         = node_cmd_seq++;

    EspDashNodeCmd *c = (EspDashNodeCmd *)(pkt + sizeof(EspDashHeader));
    c->want_canlog = want ? 1 : 0;
    c->node_id     = 1;          // esp-rectangular-314
    c->reserved    = 0;

    esp_now_send(broadcast_mac, pkt, sizeof(pkt));
}

// Re-advertise while recording; also send a few explicit stops on release so
// the gateway reacts immediately instead of waiting out the timeout.
static void node_cmd_tick(uint32_t now) {
    static uint32_t last_send = 0;
    static uint8_t  stop_bursts = 0;
    static bool     was_recording = false;

    bool recording = (canlog_state() == CANLOG_RECORDING);

    if (was_recording && !recording) stop_bursts = 3;
    was_recording = recording;

    if (now - last_send < ESPDASH_NODE_CMD_INTERVAL_MS) return;
    last_send = now;

    if (recording) {
        send_node_cmd(true);
    } else if (stop_bursts) {
        stop_bursts--;
        send_node_cmd(false);
    }
}

static void rec_button_init(void) {
    pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
}

// Single short press toggles recording. There is deliberately no long-press
// gesture: the first version had one for "unmount before removing the card",
// and it was both buggy (the release still ran the toggle, silently
// remounting and restarting the recording it had just stopped) and
// unnecessary. Once a recording is stopped the file is closed and fsync'd,
// so nothing is in flight and the card can simply be pulled - see
// canlog_stop() and the "safe to remove" state in rec_label_update().
static void rec_button_poll(uint32_t now) {
    static bool     last_raw = true;    // pulled up = released
    static bool     stable = true;
    static uint32_t last_change = 0;

    bool raw = digitalRead(REC_BUTTON_PIN);
    if (raw != last_raw) {
        last_raw = raw;
        last_change = now;
        return;
    }
    if (now - last_change < REC_DEBOUNCE_MS || raw == stable) return;

    stable = raw;
    if (stable) {                        // released (active low, so this is the edge)
        if (canlog_state() == CANLOG_RECORDING) canlog_stop();
        else                                    canlog_start();
    }
}

// Red REC + elapsed + free space while recording; dim when idle; amber on
// error. Created here rather than in EEZ so the generated UI can be
// regenerated without losing it.
// Colour is applied with an explicit style rather than LVGL's "#rrggbb ...#"
// recolor markup. Recolor only tints the span between the markers and leaves
// the rest at the theme default - and this build runs the LIGHT theme
// (LV_THEME_DEFAULT_DARK 0) on a near-black screen background (0x0f111a), so
// any un-tinted text would be dark-on-dark and effectively invisible. There
// are no other labels in the EEZ UI, so nothing had exercised that default
// before. Styling the whole label per state is both correct and simpler.
static void rec_label_update(void) {
    if (!rec_label) return;
    char buf[64];
    CanLogState st = canlog_state();
    uint32_t colour;

    if (st == CANLOG_RECORDING) {
        uint32_t sec = canlog_elapsed_ms() / 1000;
        uint32_t drops = canlog_dropped() + canlog_gw_dropped();
        colour = drops ? 0xffa000 : 0xff4040;   // amber if anything was lost
        // File number is shown throughout so you can note which recording
        // corresponds to what you were doing at the time.
        if (drops) {
            snprintf(buf, sizeof(buf), LV_SYMBOL_STOP " REC %04u  %lu:%02lu  !%lu",
                     (unsigned)canlog_file_index(),
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60),
                     (unsigned long)drops);
        } else {
            snprintf(buf, sizeof(buf), LV_SYMBOL_STOP " REC %04u  %lu:%02lu  %luMB",
                     (unsigned)canlog_file_index(),
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60),
                     (unsigned long)(canlog_bytes_written() / (1024 * 1024)));
        }
    } else if (st == CANLOG_ERROR) {
        // Name the actual cause. This gets read in a car with no serial
        // monitor attached, where "SD ERROR" alone leaves you guessing
        // between a missing card, an exFAT card needing a reformat, and a
        // full one - each with a completely different fix.
        colour = 0xffa000;
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", sdcard_status_str());
    } else if (canlog_safe_to_remove()) {
        // Green rather than grey: an explicit "everything is on the card, you
        // can pull it or cut power" signal, not just an idle state.
        colour = 0x40c070;
        snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " %luMB OK",
                 (unsigned long)sdcard_free_mb());
    } else {
        colour = 0x8892a4;   // readable grey, not the theme's dark default
        snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " %luMB",
                 (unsigned long)sdcard_free_mb());
    }

    lv_obj_set_style_text_color(rec_label, lv_color_hex(colour), LV_PART_MAIN);
    lv_label_set_text(rec_label, buf);
}

// Global LVGL Chart Series Handles
static lv_chart_series_t *ser_throttle = NULL;
static lv_chart_series_t *ser_brake    = NULL;
static lv_chart_series_t *ser_speed    = NULL;
static uint16_t current_speed_ymax     = 60;

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=================================================================");
    Serial.println(" 🏎️ espDash Waveshare ESP32-S3-LCD-3.16 (Racelab Telemetry Display)");
    Serial.println(" DISPLAY: ST7701 RGB Parallel 820x320 LCD (Landscape)");
    Serial.println(" NODE: esp-rectangular-314");
    Serial.println("=================================================================");

    // Initialize ST7701 RGB parallel display driver & FreeRTOS LVGL task
    lvgl_port_init();

    // Turn on LCD backlight to full brightness
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);

    // Release 3-wire SPI pins after panel initialization. This frees GPIO0
    // (button) and GPIO1/GPIO2, which the SD card needs - so both the button
    // and the SD mount below MUST come after this call.
    release_st7701_spi_pins();

    rec_button_init();
    canlog_init();   // mounts SD, allocates PSRAM ring, starts writer task

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
            lv_chart_set_range(objects.history_chart, LV_CHART_AXIS_SECONDARY_Y, 0, 60);
            lv_chart_set_point_count(objects.history_chart, 200); // 200 samples @ 20Hz = 10 sec rolling window

            // Remove default circular points on line nodes for a sleek Racelab look
            lv_obj_set_style_size(objects.history_chart, 0, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(objects.history_chart, lv_color_hex(0x141824), LV_PART_MAIN);
            lv_obj_set_style_border_color(objects.history_chart, lv_color_hex(0x2a2f45), LV_PART_MAIN);
            lv_obj_set_style_line_width(objects.history_chart, 2, LV_PART_ITEMS);

            // Add 3 Time-Synchronized Series
            ser_throttle = lv_chart_add_series(objects.history_chart, lv_color_hex(0x00FF00), LV_CHART_AXIS_PRIMARY_Y);   // Neon Green (Throttle %)
            ser_brake    = lv_chart_add_series(objects.history_chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);   // Bright Red (Brake %)
            ser_speed    = lv_chart_add_series(objects.history_chart, lv_color_hex(0xFFFF00), LV_CHART_AXIS_SECONDARY_Y); // Bright Yellow (Speed KM/H)

            // Pre-fill chart with zero values
            for (int i = 0; i < 200; i++) {
                lv_chart_set_next_value(objects.history_chart, ser_throttle, 0);
                lv_chart_set_next_value(objects.history_chart, ser_brake, 0);
                lv_chart_set_next_value(objects.history_chart, ser_speed, 0);
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

        // Recording status, top-right. Built here rather than in EEZ Studio
        // so regenerating the UI cannot silently drop it.
        // Top strip (y 0..31) is free: the chart starts at y=32 and the bars
        // at x=28/55/82 also start at y=32, so a right-aligned label here
        // cannot overlap anything. (left_tiers sits at y=428, off a 320-tall
        // screen, so it is not a factor.)
        if (objects.main) {
            rec_label = lv_label_create(objects.main);
            lv_obj_align(rec_label, LV_ALIGN_TOP_RIGHT, -8, 6);
            lv_obj_set_style_text_color(rec_label, lv_color_hex(0x8892a4), LV_PART_MAIN);
            lv_label_set_text(rec_label, LV_SYMBOL_SD_CARD " --");
        }

        lvgl_port_unlock();
    }

    // Register ESP-NOW Receiver Callback
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(OnDataRecv);

        // This node also TRANSMITS now (arming the gateway's log stream), so
        // it needs a peer - receiving alone never required one.
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, broadcast_mac, 6);
        peer.channel = 0;    // follow the current radio channel
        peer.encrypt = false;
        if (esp_now_add_peer(&peer) != ESP_OK) {
            Serial.println("[ESP-NOW] Failed to add broadcast peer - cannot arm gateway!");
        }
    }
}

void loop() {
    uint32_t now = millis();

    // Record button: polled every pass (not just on the 20 Hz UI tick) so a
    // short press is never missed.
    rec_button_poll(now);
    canlog_tick(now);     // auto-closes the file if the gateway goes away
    node_cmd_tick(now);   // keeps the gateway armed while recording

    // Link Supervision & Demo Mode Generation
    bool live = ever_linked && (now - last_pkt_rx_time <= LINK_TIMEOUT_MS);
    LinkState link = live ? LINK_LIVE : (ever_linked ? LINK_LOST : LINK_SEARCHING);
    bool is_demo = (link == LINK_SEARCHING);

    EspDashTelemetry active_pkt = {0};
    if (is_demo) {
        // Dynamic multi-frequency sinusoidal telemetry sweep curves
        float phase = now * 0.0015f;
        float thr_val = 50.0f + 42.0f * sin(phase * 1.3f) + 14.0f * sin(phase * 2.9f);
        float brk_val = 45.0f + 42.0f * sin(phase * 1.3f + 2.4f) + 12.0f * cos(phase * 3.3f);

        active_pkt.throttle_pct = (uint8_t)constrain((int)thr_val, 0, 100);
        active_pkt.brake_pct    = (uint8_t)constrain((int)brk_val, 0, 100);
        active_pkt.rpm          = (uint16_t)constrain((int)(1200 + (active_pkt.throttle_pct / 100.0f) * 5200 + sin(phase * 4.2f) * 350), 800, 7000);
        active_pkt.speed_kmh_x10 = (uint16_t)((35 + (active_pkt.throttle_pct / 100.0f) * 65 + sin(phase * 0.8f) * 20) * 10); // 15 - 120 km/h sweep
        active_pkt.water_temp_x10 = 920;
        active_pkt.fuel_pct = 82;
        active_pkt.battery_mv = 13800;
        active_pkt.gear = (uint8_t)(1 + ((int)(now * 0.0004f) % 6));
    } else {
        active_pkt = current_pkt;
    }

    // Batch all LVGL updates at 20 Hz (50ms) to avoid excessive mutex contention
    // and partial-frame redraws that cause visible twitching
    static uint32_t last_ui_update = 0;
    static uint8_t prev_throttle = 0xFF, prev_brake = 0xFF;
    static uint16_t prev_rpm = 0xFFFF;

#define SPEED_WINDOW_POINTS 200
static uint16_t speed_history_buf[SPEED_WINDOW_POINTS] = {0};
static uint16_t speed_buf_idx = 0;

    if (now - last_ui_update >= 50) {
        last_ui_update = now;

        if (lvgl_port_lock(10)) {
            // Chart feed with dynamic speed autoscale (visible 10s window max + 10 km/h padding, baseline 60 km/h)
            if (objects.history_chart && ser_throttle && ser_brake && ser_speed) {
                uint16_t spd_kmh = active_pkt.speed_kmh_x10 / 10;

                // Store in circular buffer matching the 200 visible chart points (10 sec)
                speed_history_buf[speed_buf_idx] = spd_kmh;
                speed_buf_idx = (speed_buf_idx + 1) % SPEED_WINDOW_POINTS;

                // Find peak speed within currently visible 10-second chart window
                uint16_t visible_max_speed = 0;
                for (int i = 0; i < SPEED_WINDOW_POINTS; i++) {
                    if (speed_history_buf[i] > visible_max_speed) {
                        visible_max_speed = speed_history_buf[i];
                    }
                }

                // Baseline Y-max is 60 km/h. When speed exceeds 50 km/h, scale to (visible_max + 10 km/h)
                // so the yellow speed line never touches the top border of the chart.
                // Reverts back to 60 km/h automatically once high speed points roll off the screen.
                uint16_t target_ymax = (visible_max_speed > 50) ? (visible_max_speed + 10) : 60;

                if (target_ymax != current_speed_ymax) {
                    current_speed_ymax = target_ymax;
                    lv_chart_set_range(objects.history_chart, LV_CHART_AXIS_SECONDARY_Y, 0, current_speed_ymax);
                }

                lv_chart_set_next_value(objects.history_chart, ser_throttle, active_pkt.throttle_pct);
                lv_chart_set_next_value(objects.history_chart, ser_brake, active_pkt.brake_pct);
                lv_chart_set_next_value(objects.history_chart, ser_speed, spd_kmh);
            }

            // Only update bars when value actually changed (avoids needless redraws)
            if (objects.throttle_bar && active_pkt.throttle_pct != prev_throttle) {
                lv_bar_set_value(objects.throttle_bar, active_pkt.throttle_pct, LV_ANIM_OFF);
                prev_throttle = active_pkt.throttle_pct;
            }
            if (objects.brake_bar && active_pkt.brake_pct != prev_brake) {
                lv_bar_set_value(objects.brake_bar, active_pkt.brake_pct, LV_ANIM_OFF);
                prev_brake = active_pkt.brake_pct;
            }
            uint16_t rpm_clamped = min((uint16_t)7000, active_pkt.rpm);
            if (objects.rpm_bar && rpm_clamped != prev_rpm) {
                lv_bar_set_value(objects.rpm_bar, rpm_clamped, LV_ANIM_OFF);
                prev_rpm = rpm_clamped;
            }

            rec_label_update();

            ui_tick();
            lvgl_port_unlock();
        }
    }

    delay(20); // Let LVGL task run uncontested between updates
}
