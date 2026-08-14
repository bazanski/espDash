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
#define REC_DEBOUNCE_MS    250
// TEMPORARY diagnostic: set to 0 once the button pin is confirmed.
#define REC_BUTTON_DEBUG   1

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
static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t node_cmd_seq = 0;
#if REC_BUTTON_DEBUG
static volatile uint32_t dbg_cmd_sent = 0, dbg_cmd_fail = 0;
static volatile int      dbg_cmd_err = 0;
#endif

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

    esp_err_t e = esp_now_send(broadcast_mac, pkt, sizeof(pkt));
#if REC_BUTTON_DEBUG
    if (e == ESP_OK) dbg_cmd_sent++;
    else { dbg_cmd_fail++; dbg_cmd_err = e; }
#else
    (void)e;
#endif
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

#if REC_BUTTON_DEBUG
static volatile uint32_t dbg_presses = 0, dbg_toggles = 0;
static volatile uint32_t dbg_start_ok = 0, dbg_start_fail = 0;
static volatile uint32_t dbg_loop_avg = 0, dbg_loop_max = 0;
static volatile uint32_t dbg_t_btn = 0, dbg_t_tick = 0, dbg_t_ui = 0, dbg_t_lock = 0;
#else
#define dbg_presses (*(volatile uint32_t *)0)  // never referenced when debug off
#endif

// The button is handled by a GPIO interrupt, not by polling.
//
// Polling from loop() cannot work on this board: the LVGL port task runs at
// priority 5 on core 1 while the Arduino loopTask runs at priority 1, and
// rendering the 820x320 RGB panel starves the loop badly enough that
// delay(20) actually returns after ~150 ms (measured: every section of
// loop() timed 0 ms, yet the loop period was 131-158 ms). A press and its
// release both fit inside one such gap, so the level was frequently sampled
// as HIGH both before and after - the press simply never existed as far as
// the debounce state machine was concerned. That is why 3 presses produced
// only 2 debounced edges and 1 toggle.
//
// An interrupt fires regardless of task scheduling, so it is immune to that
// entirely. Debouncing happens in the ISR by ignoring anything within
// REC_DEBOUNCE_MS of the last accepted press.
static volatile uint32_t isr_last_press_ms = 0;
static volatile bool     isr_press_pending = false;

static void IRAM_ATTR rec_button_isr(void) {
    // millis() is backed by esp_timer_get_time(), which is ISR-safe.
    uint32_t t = millis();
    if (t - isr_last_press_ms < REC_DEBOUNCE_MS) return;   // bounce
    isr_last_press_ms = t;
    isr_press_pending = true;
#if REC_BUTTON_DEBUG
    dbg_presses++;
#endif
}

static void rec_button_init(void) {
    pinMode(REC_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(REC_BUTTON_PIN), rec_button_isr, FALLING);
}

// Consumes at most one press per call. Safe to call at whatever rate loop()
// manages - the ISR does not lose presses in between.
static void rec_button_poll(uint32_t now) {
    (void)now;
    if (!isr_press_pending) return;
    isr_press_pending = false;
#if REC_BUTTON_DEBUG
    dbg_toggles++;
#endif
    if (canlog_state() == CANLOG_RECORDING) {
        canlog_stop();
    } else {
#if REC_BUTTON_DEBUG
        if (canlog_start()) dbg_start_ok++; else dbg_start_fail++;
#else
        canlog_start();
#endif
    }
}

// Drives the two EEZ labels on the Main screen:
//   status_label  (115,38) - recording state, always populated
//   warning_label (115,60) - only speaks up when something is wrong
//
// Colour is applied here with an explicit style, not via EEZ and not via
// LVGL's "#rrggbb ...#" recolor markup. EEZ generated no styles at all for
// this project (styles.c is empty), and the build runs the LIGHT theme
// (LV_THEME_DEFAULT_DARK 0) over a near-black background (0x0f111a) - so an
// unstyled label renders dark-on-dark and is effectively invisible. Setting
// the colour per state in code is both correct and survives a UI re-export.
static void rec_label_update(void) {
    char buf[64];
    CanLogState st = canlog_state();

#if REC_BUTTON_DEBUG
    static uint32_t dbg_next = 0;
    if (millis() >= dbg_next) {
        dbg_next = millis() + 3000;
        Serial.printf("[UI] status_label=%p warning_label=%p state=%d\n",
                      (void *)objects.status_label, (void *)objects.warning_label, (int)st);
    }
#endif

    // ---- status: what the recorder is doing right now --------------------
    if (objects.status_label) {
        uint32_t colour;
        if (st == CANLOG_RECORDING) {
            uint32_t sec = canlog_elapsed_ms() / 1000;
            colour = 0xff4040;
            // File number is shown throughout so a recording can be tied back
            // to what you were doing at the time.
            snprintf(buf, sizeof(buf), LV_SYMBOL_STOP " REC %04u  %lu:%02lu  %luMB",
                     (unsigned)canlog_file_index(),
                     (unsigned long)(sec / 60), (unsigned long)(sec % 60),
                     (unsigned long)(canlog_bytes_written() / (1024 * 1024)));
        } else if (st == CANLOG_ERROR) {
            colour = 0xffa000;
            snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " %s", sdcard_status_str());
        } else if (canlog_safe_to_remove()) {
            // Green is an explicit "every byte is on the card, you can pull it
            // or cut power" signal, not merely an idle state.
            colour = 0x40c070;
            snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " %luMB  SAFE",
                     (unsigned long)sdcard_free_mb());
        } else {
            colour = 0x8892a4;
            snprintf(buf, sizeof(buf), LV_SYMBOL_SD_CARD " %luMB",
                     (unsigned long)sdcard_free_mb());
        }
        lv_obj_set_style_text_color(objects.status_label, lv_color_hex(colour), LV_PART_MAIN);
        lv_label_set_text(objects.status_label, buf);
    }

    // ---- warning: silent unless there is something to say ----------------
    // Kept empty in the normal case on purpose. A warning line that always
    // shows something trains you to ignore it, which defeats the point.
    if (objects.warning_label) {
        uint32_t drops = canlog_dropped() + canlog_gw_dropped();
        uint16_t gaps  = canlog_seq_gaps();

        if (st == CANLOG_ERROR) {
            // Name the actual cause: this is read in a car with no serial
            // monitor, where a bare "SD ERROR" leaves you guessing between a
            // missing card, an exFAT card needing a reformat, and a full one -
            // each with a completely different fix.
            lv_obj_set_style_text_color(objects.warning_label, lv_color_hex(0xff4040), LV_PART_MAIN);
            snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " %s", sdcard_status_str());
            lv_label_set_text(objects.warning_label, buf);
        } else if (st == CANLOG_RECORDING && (drops || gaps)) {
            // Frames lost somewhere in the chain - the capture will have holes.
            lv_obj_set_style_text_color(objects.warning_label, lv_color_hex(0xffa000), LV_PART_MAIN);
            snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " lost %lu  gaps %u",
                     (unsigned long)drops, (unsigned)gaps);
            lv_label_set_text(objects.warning_label, buf);
        } else {
            lv_label_set_text(objects.warning_label, "");
        }
    }
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
        // Recording status now lives in the EEZ-authored status_label /
        // warning_label (montserrat_20, far more legible at a glance than the
        // 14px corner label this replaces), so nothing is created here.

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
#if REC_BUTTON_DEBUG
    {
        static uint32_t prev = 0, acc = 0, n = 0;
        if (prev) {
            uint32_t d = now - prev;
            acc += d; n++;
            if (d > dbg_loop_max) dbg_loop_max = d;
            if (n >= 20) { dbg_loop_avg = acc / n; acc = 0; n = 0; }
        }
        prev = now;
    }
#endif

    // Record button: polled every pass (not just on the 20 Hz UI tick) so a
    // short press is never missed.
    uint32_t _t0 = millis();
    rec_button_poll(now);
    uint32_t _t1 = millis();
    canlog_tick(now);     // auto-closes the file if the gateway goes away
    node_cmd_tick(now);   // keeps the gateway armed while recording
    uint32_t _t2 = millis();
    dbg_t_btn  = _t1 - _t0;
    dbg_t_tick = _t2 - _t1;
#if REC_BUTTON_DEBUG
    {
        static uint32_t hb = 0;
        if (now >= hb) {
            hb = now + 2000;
            Serial.printf("[BTN] presses=%lu toggles=%lu ok=%lu fail=%lu "
                          "state=%d file=%04u sd=%s tlm=%lu clog=%lu tx=%lu txfail=%lu(%d) loop=%lums\n",
                          (unsigned long)dbg_presses, (unsigned long)dbg_toggles,
                          (unsigned long)dbg_start_ok, (unsigned long)dbg_start_fail,
                          (int)canlog_state(), (unsigned)canlog_file_index(),
                          sdcard_status_str(),
                          (unsigned long)pkt_count, (unsigned long)canlog_packets_rx(),
                          (unsigned long)dbg_cmd_sent, (unsigned long)dbg_cmd_fail,
                          dbg_cmd_err, (unsigned long)dbg_loop_avg);
            dbg_loop_max = 0;
        }
    }
#endif

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
        uint32_t _u0 = millis();

        uint32_t _l0 = millis();
        if (lvgl_port_lock(10)) {
            dbg_t_lock = millis() - _l0;
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
        dbg_t_ui = millis() - _u0;
    }

    delay(20); // Let LVGL task run uncontested between updates
}
