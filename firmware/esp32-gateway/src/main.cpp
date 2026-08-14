// =========================================================================
// espDash CENTRAL CAN GATEWAY - 2014 Honda Civic 9th Gen
// =========================================================================
// SAFETY: the TWAI controller is installed in TWAI_MODE_LISTEN_ONLY and this
// firmware never transmits a CAN frame. Do not change that.
//
// CONCURRENCY
//   core 1  canRxTask   (prio 10) - twai_receive + decode + ring_push. No I/O.
//   core 1  publishTask (prio 5)  - ALL output: raw drain, JSON, ESP-NOW, and
//                                   Serial (+ WebSocket/telnet when enabled).
//   core 1  loopTask    (prio 1)  - Wi-Fi/OTA maintenance when enabled, else
//                                   just TWAI recovery checks.
//   core 0                        - left to the Wi-Fi/lwIP and TinyUSB tasks.
//
// Priority, not core affinity, is what protects CAN reception here: canRxTask
// preempts publishTask, so a blocking write cannot delay a frame.
//
// Output used to happen inside the TWAI receive loop, so a TCP retransmit or
// a slow USB host stalled CAN reception outright. At the measured 1399
// frames/s the driver's queue is only ~183 ms deep, so those stalls silently
// lost frames. The ring buffer decouples the two; overflow now drops the
// oldest frame and increments a counter reported as "dropped" in the JSON.
//
// WIRELESS: gated behind ESPDASH_GATEWAY_WIFI (platformio.ini), OFF by
// default. An on-car test showed the gateway's own Wi-Fi station -
// scanning/associating/roaming - degrading the ESP-NOW link to the display
// nodes even with power-save disabled and a runtime abandon-and-relock
// fallback in place. Rather than patch that further, the default build never
// attempts a Wi-Fi connection at all: no association, no scanning, no
// roaming, nothing for a fallback to abandon. USB serial + ESP-NOW only. The
// code for the old Wi-Fi-connected behavior (mDNS, ArduinoOTA, the WebSocket
// dashboard path, Telnet) is preserved behind the flag, not deleted - see
// ARCHITECTURE.md S5.1/S5.2 and set ESPDASH_GATEWAY_WIFI=1 to restore it.
//
// The WebSockets library is NOT thread-safe: when the flag is on,
// webSocket.loop() and broadcastTXT() must both stay inside publishTask.
// =========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "driver/twai.h"

#if ESPDASH_GATEWAY_WIFI
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#endif

#include <EspDashProto.h>
#include "can_decode.h"

// =========================================================================
// CAN PINS (Waveshare ESP32-S3-RS485-CAN)
// =========================================================================
static const gpio_num_t CAN_TX_PIN = GPIO_NUM_15;
static const gpio_num_t CAN_RX_PIN = GPIO_NUM_16;
static bool twai_installed = false;

// =========================================================================
// MODES
// =========================================================================
enum OperationalMode {
    MODE_TELEMETRY = 0,
    MODE_RAW_SNIFFER = 1,
    MODE_DUAL = 2
};

// Independent of current_mode: streaming raw CAN over ESP-NOW to an SD
// recorder node is orthogonal to what the USB/serial side is doing, so a
// capture can run while the dashboard is in any mode (or nothing is attached
// at all, which is the whole point - recording a drive without a laptop).
static volatile bool canlog_streaming = false;

static volatile OperationalMode current_mode = MODE_TELEMETRY;
static volatile bool demo_mode = false;
#if ESPDASH_GATEWAY_WIFI
static volatile bool ota_in_progress = false;
#endif

// =========================================================================
// SHARED STATE  (canRxTask writes, publishTask reads, guarded by spinlock)
// =========================================================================
static CanDecodeState g_state;                       // owned by canRxTask
static CanDecodeState g_snapshot;                    // published copy
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

// =========================================================================
// RAW FRAME RING BUFFER  (canRxTask produces, publishTask consumes)
// =========================================================================
// 2048 slots (~34 KB). Sized for the canlog path: at the measured 1399
// frames/s this is ~1.4 s of buffer, enough to ride out radio backpressure
// when batches contend with the 20 Hz telemetry broadcast. The serial RAW
// path never needed this much, but the memory is cheap on an S3.
#define RAW_RING_SIZE 2048

typedef struct {
    uint32_t ms;
    uint32_t id;
    uint8_t  rtr;
    uint8_t  dlc;
    uint8_t  data[8];
} RawSlot;

static RawSlot raw_ring[RAW_RING_SIZE];
static volatile uint16_t ring_head = 0;   // next write
static volatile uint16_t ring_tail = 0;   // next read
static portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t raw_dropped = 0;
static volatile uint32_t twai_queue_full_events = 0;

static inline void ring_push(const RawSlot *s) {
    portENTER_CRITICAL(&ring_mux);
    uint16_t next = (uint16_t)((ring_head + 1) % RAW_RING_SIZE);
    if (next == ring_tail) {
        // Full: drop the OLDEST frame so live monitoring stays current, and
        // count it. A lossy capture must be visible, never silent.
        ring_tail = (uint16_t)((ring_tail + 1) % RAW_RING_SIZE);
        raw_dropped++;
    }
    raw_ring[ring_head] = *s;
    ring_head = next;
    portEXIT_CRITICAL(&ring_mux);
}

static inline bool ring_pop(RawSlot *out) {
    bool got = false;
    portENTER_CRITICAL(&ring_mux);
    if (ring_tail != ring_head) {
        *out = raw_ring[ring_tail];
        ring_tail = (uint16_t)((ring_tail + 1) % RAW_RING_SIZE);
        got = true;
    }
    portEXIT_CRITICAL(&ring_mux);
    return got;
}

// =========================================================================
// NETWORK
// =========================================================================
#if ESPDASH_GATEWAY_WIFI
static WiFiMulti wifiMulti;
static WebSocketsServer webSocket = WebSocketsServer(8888);
static WiFiServer telnetServer(8889);
static WiFiClient telnetClient;
static String last_connected_ip = "";

// Once true, Wi-Fi has been deliberately abandoned for the rest of this boot
// so ESP-NOW can sit on a fixed, stable channel. See the fallback logic in
// loop() for why this exists. Only meaningful when a connection was ever
// attempted in the first place.
static volatile bool wifi_fallback_engaged = false;
#endif

static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t espnow_seq = 0;
static volatile uint32_t espnow_send_fail = 0;
static volatile uint32_t tx_truncated = 0;

// canlog counters, reported by STATS so a lossy capture is visible
static volatile uint32_t canlog_batches_sent = 0;
static volatile uint32_t canlog_frames_sent = 0;
static volatile uint32_t canlog_send_fail = 0;

// USB CDC and TCP both accept short writes: write() returns how many bytes it
// actually took, which is less than requested once the peer stops draining.
// Ignoring that return value corrupts the stream - observed as JSON lines
// missing whole 64-byte USB packets from the middle, because the tail of one
// line and the head of the next were both discarded. Loop until the whole
// buffer is gone.
//
// Blocking here is safe by design: canRxTask is independent, so a slow reader
// backs up into the ring buffer, which drops oldest and counts it. The guard
// bounds the wait so a peer that has stopped reading entirely cannot wedge
// publishTask - we give up and count a truncation instead.
static void write_all(Print &out, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    int stalls = 0;
    while (sent < len && stalls < 50) {
        size_t n = out.write(buf + sent, len - sent);
        if (n == 0) { stalls++; vTaskDelay(1); }
        else { sent += n; stalls = 0; }
    }
    if (sent < len) tx_truncated++;
}

// Every enabled transport gets a byte-identical payload. MUST only be called
// from publishTask (WebSockets library is not thread-safe).
static void broadcast_line(const char *str) {
    size_t len = strlen(str);
    write_all(Serial, (const uint8_t *)str, len);
#if ESPDASH_GATEWAY_WIFI
    if (telnetClient && telnetClient.connected()) {
        write_all(telnetClient, (const uint8_t *)str, len);
    }
    webSocket.broadcastTXT((uint8_t *)str, len);
#endif
}

// =========================================================================
// DEMO GENERATOR
// =========================================================================
// Applied to the published snapshot rather than to the CAN state, so it no
// longer depends on CAN frames arriving. Previously brake and throttle were
// promoted inside the RX loop, so on a quiet bench bus the demo's braking
// never reached the dashboard at all.
static void apply_demo(CanDecodeState *s, float sim_t) {
    float cycle = fmodf(sim_t, 12.0f) / 12.0f;
    float rpm_val = 800.0f, spd_val = 0.0f;
    uint8_t gear_idx = 3;

    if (cycle < 0.25f) {
        gear_idx = 1;
        rpm_val = 900.0f + (cycle / 0.25f) * 5600.0f;
        spd_val = (rpm_val / 6500.0f) * 35.0f;
    } else if (cycle < 0.55f) {
        gear_idx = 2;
        float p = (cycle - 0.25f) / 0.30f;
        rpm_val = 3000.0f + p * 4000.0f;
        spd_val = 35.0f + p * 35.0f;
    } else if (cycle < 0.85f) {
        gear_idx = 3;
        float p = (cycle - 0.55f) / 0.30f;
        rpm_val = 3500.0f + p * 3500.0f;
        spd_val = 70.0f + p * 45.0f;
    } else {
        gear_idx = 4;
        float p = (cycle - 0.85f) / 0.15f;
        rpm_val = 3500.0f - p * 1200.0f;
        spd_val = 115.0f - p * 25.0f;
    }

    uint8_t throt = (uint8_t)constrain((int)(max(0.0f, sinf(sim_t * 1.2f)) * 100.0f), 0, 100);
    int16_t steer = (int16_t)(160.0f * sinf(sim_t * 0.3f));

    s->rpm             = (uint16_t)rpm_val;
    s->speed_kmh_x10   = (uint16_t)(spd_val * 10.0f);
    s->water_temp_x10  = (int16_t)((87.0f + 3.0f * sinf(sim_t * 0.1f)) * 10.0f);
    s->oil_temp_x10    = (int16_t)((92.0f + 4.0f * sinf(sim_t * 0.08f)) * 10.0f);
    s->battery_mv      = (uint16_t)((13.8f + 0.3f * sinf(sim_t * 0.5f)) * 1000.0f);
    s->gear            = gear_idx;
    s->fuel_pct        = 78;
    s->throttle_pct    = throt;
    s->steering_deg    = steer;
    s->ambient_temp    = 23;

    float corner = (steer / 160.0f) * 3.5f;
    s->wheel_fl_x10 = (uint16_t)(max(0.0f, spd_val - corner) * 10.0f);
    s->wheel_fr_x10 = (uint16_t)(max(0.0f, spd_val + corner) * 10.0f);
    s->wheel_rl_x10 = (uint16_t)(max(0.0f, spd_val - corner * 0.7f) * 10.0f);
    s->wheel_rr_x10 = (uint16_t)(max(0.0f, spd_val + corner * 0.7f) * 10.0f);

    s->brake_switch = (throt < 10);
    s->brake_pct    = (throt < 10) ? 25 : 0;
}

// =========================================================================
// COMMANDS  (called from publishTask only)
// =========================================================================
static void process_cmd_string(String cmd) {
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "MODE:PLOT" || cmd == "MODE_PLOT" || cmd == "MODE:TELEMETRY") {
        current_mode = MODE_TELEMETRY;
        broadcast_line("[MODE] Switched to TELEMETRY_PLOT\n");
    } else if (cmd == "MODE:RAW" || cmd == "MODE_RAW" || cmd == "MODE:SNIFFER") {
        current_mode = MODE_RAW_SNIFFER;
        broadcast_line("[MODE] Switched to RAW_SNIFFER\n");
    } else if (cmd == "MODE:DUAL" || cmd == "MODE_DUAL" || cmd == "MODE:BOTH") {
        current_mode = MODE_DUAL;
        broadcast_line("[MODE] Switched to DUAL (RAW CAN + TELEMETRY JSON)\n");
    } else if (cmd == "DEMO:ON" || cmd == "DEMO_ON" || cmd == "DEMO:1") {
        demo_mode = true;
        broadcast_line("[DEMO] Demo Telemetry Generator ENABLED\n");
    } else if (cmd == "DEMO:OFF" || cmd == "DEMO_OFF" || cmd == "DEMO:0") {
        demo_mode = false;
        broadcast_line("[DEMO] Demo Telemetry Generator DISABLED (Real CAN active)\n");
    } else if (cmd == "DEMO:TOGGLE") {
        demo_mode = !demo_mode;
        char buf[64];
        snprintf(buf, sizeof(buf), "[DEMO] Demo Telemetry is now %s\n",
                 demo_mode ? "ENABLED" : "DISABLED");
        broadcast_line(buf);
    } else if (cmd == "MODE:GET") {
        char buf[100];
        const char *m = (current_mode == MODE_TELEMETRY) ? "TELEMETRY"
                      : (current_mode == MODE_RAW_SNIFFER) ? "RAW_SNIFFER" : "DUAL";
        snprintf(buf, sizeof(buf), "[MODE] CURRENT:%s | DEMO:%s\n", m, demo_mode ? "ON" : "OFF");
        broadcast_line(buf);
    } else if (cmd == "LOG:ON" || cmd == "LOG_ON" || cmd == "LOG:1") {
        canlog_streaming = true;
        broadcast_line("[LOG] CAN log streaming ENABLED (ESP-NOW -> SD recorder)\n");
    } else if (cmd == "LOG:OFF" || cmd == "LOG_OFF" || cmd == "LOG:0") {
        canlog_streaming = false;
        broadcast_line("[LOG] CAN log streaming DISABLED\n");
    } else if (cmd == "STATS") {
        char buf[220];
#if ESPDASH_GATEWAY_WIFI
        snprintf(buf, sizeof(buf),
                 "[STATS] decoded:%lu cksum_rejects:%lu ring_dropped:%lu twai_qfull:%lu "
                 "tx_trunc:%lu espnow_fail:%lu wifi_fallback:%s "
                 "log:%s batches:%lu logframes:%lu logfail:%lu\n",
                 (unsigned long)g_snapshot.frames_decoded,
                 (unsigned long)g_snapshot.checksum_rejects,
                 (unsigned long)raw_dropped,
                 (unsigned long)twai_queue_full_events,
                 (unsigned long)tx_truncated,
                 (unsigned long)espnow_send_fail,
                 wifi_fallback_engaged ? "yes" : "no",
                 canlog_streaming ? "on" : "off",
                 (unsigned long)canlog_batches_sent,
                 (unsigned long)canlog_frames_sent,
                 (unsigned long)canlog_send_fail);
#else
        // No wifi_fallback field: with no Wi-Fi station ever attempted,
        // there's no connection to fall back from. espnow_send_fail is still
        // meaningful - esp_now_send() can fail for reasons unrelated to
        // Wi-Fi, such as an internal queue full.
        snprintf(buf, sizeof(buf),
                 "[STATS] decoded:%lu cksum_rejects:%lu ring_dropped:%lu twai_qfull:%lu "
                 "tx_trunc:%lu espnow_fail:%lu "
                 "log:%s batches:%lu logframes:%lu logfail:%lu\n",
                 (unsigned long)g_snapshot.frames_decoded,
                 (unsigned long)g_snapshot.checksum_rejects,
                 (unsigned long)raw_dropped,
                 (unsigned long)twai_queue_full_events,
                 (unsigned long)tx_truncated,
                 (unsigned long)espnow_send_fail,
                 canlog_streaming ? "on" : "off",
                 (unsigned long)canlog_batches_sent,
                 (unsigned long)canlog_frames_sent,
                 (unsigned long)canlog_send_fail);
#endif
        broadcast_line(buf);
    }
}

#if ESPDASH_GATEWAY_WIFI
static void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String cmd = String((char *)payload, length);
        process_cmd_string(cmd);
    } else if (type == WStype_CONNECTED) {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WebSocket] Client #%u connected from %s\n", num, ip.toString().c_str());
    }
}
#endif

// =========================================================================
// TWAI
// =========================================================================
static bool install_twai() {
    if (twai_installed) {
        twai_stop();
        twai_driver_uninstall();
        twai_installed = false;
    }

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
    g.rx_queue_len = 512;  // ~366 ms at the measured 1399 frames/s

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    if (twai_start() != ESP_OK) return false;

    // Surface driver-level overruns instead of losing frames invisibly.
    twai_reconfigure_alerts(TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_OFF |
                            TWAI_ALERT_ERR_PASS, NULL);
    twai_installed = true;
    return true;
}

// =========================================================================
// CORE 1: CAN RX TASK - decode only, never blocks on I/O
// =========================================================================
static void canRxTask(void *arg) {
    (void)arg;
    twai_message_t rx;
    CanFrame f;

    for (;;) {
        if (!twai_installed) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Blocking receive with a short timeout: sleeps when the bus is quiet,
        // wakes immediately on a frame.
        if (twai_receive(&rx, pdMS_TO_TICKS(10)) != ESP_OK) continue;

        uint32_t now = millis();

        f.id  = rx.identifier;
        f.dlc = rx.data_length_code > 8 ? 8 : rx.data_length_code;
        memcpy(f.data, rx.data, f.dlc);

        can_decode_frame(&g_state, &f, now);

        // Publish a consistent copy for publishTask.
        portENTER_CRITICAL(&state_mux);
        g_snapshot = g_state;
        portEXIT_CRITICAL(&state_mux);

        if (current_mode == MODE_RAW_SNIFFER || current_mode == MODE_DUAL) {
            RawSlot s;
            s.ms  = now;
            s.id  = rx.identifier;
            s.rtr = rx.rtr;
            s.dlc = f.dlc;
            memcpy(s.data, f.data, 8);
            ring_push(&s);
        }
    }
}

// =========================================================================
// CANLOG BATCHER - packs raw frames for the ESP-NOW SD recorder
// =========================================================================
// Called only from publishTask, so no locking is needed on the batch state.
//
// The ID table maps 11-bit CAN ids to a 1-byte index. This car has 45 unique
// ids, so a byte is ample; anything beyond the table (a new id appearing
// mid-drive) falls back to an escape marker plus the raw 16-bit id rather
// than being dropped - an unexpected id is exactly the kind of thing worth
// capturing.
#define CANLOG_ID_TABLE_MAX 64
#define CANLOG_BATCH_MAX_BYTES 240   // ESP-NOW hard limit is 250

static uint16_t canlog_id_table[CANLOG_ID_TABLE_MAX];
static uint8_t  canlog_id_count = 0;
static uint32_t last_canlog_ids_send = 0;

static uint8_t  canlog_buf[CANLOG_BATCH_MAX_BYTES];
static uint16_t canlog_len = 0;       // bytes used in canlog_buf
static uint8_t  canlog_count = 0;     // frames packed
static uint32_t canlog_base_ms = 0;
static uint32_t canlog_first_push_ms = 0;

static uint8_t canlog_id_index(uint32_t id) {
    for (uint8_t i = 0; i < canlog_id_count; i++) {
        if (canlog_id_table[i] == (uint16_t)id) return i;
    }
    if (canlog_id_count < CANLOG_ID_TABLE_MAX) {
        canlog_id_table[canlog_id_count] = (uint16_t)id;
        return canlog_id_count++;
    }
    return ESPDASH_CANLOG_ID_ESCAPE;
}

static void canlog_send_batch() {
    if (canlog_count == 0) return;

    uint8_t pkt[sizeof(EspDashHeader) + sizeof(EspDashCanLogHdr) + CANLOG_BATCH_MAX_BYTES];
    EspDashHeader *h = (EspDashHeader *)pkt;
    h->magic       = ESPDASH_MAGIC;
    h->msg_type    = ESPDASH_MSG_CANLOG;
    h->proto_major = ESPDASH_PROTO_MAJOR;
    h->proto_minor = ESPDASH_PROTO_MINOR;
    h->payload_len = (uint16_t)(sizeof(EspDashCanLogHdr) + canlog_len);
    h->seq         = espnow_seq++;

    EspDashCanLogHdr *ch = (EspDashCanLogHdr *)(pkt + sizeof(EspDashHeader));
    ch->base_ms    = canlog_base_ms;
    ch->count      = canlog_count;
    ch->flags      = 0;
    // Carry the gateway-side drop count so loss is recorded in the log file
    // itself. A lossy capture must be visible, never silent.
    ch->gw_dropped = (uint16_t)raw_dropped;

    memcpy(pkt + sizeof(EspDashHeader) + sizeof(EspDashCanLogHdr), canlog_buf, canlog_len);

    size_t total = sizeof(EspDashHeader) + sizeof(EspDashCanLogHdr) + canlog_len;
    if (esp_now_send(broadcast_mac, pkt, total) == ESP_OK) {
        canlog_batches_sent++;
        canlog_frames_sent += canlog_count;
    } else {
        canlog_send_fail++;
    }

    canlog_len = 0;
    canlog_count = 0;
}

static void canlog_add_frame(const RawSlot *s) {
    uint8_t dlc = s->dlc > 8 ? 8 : s->dlc;
    uint8_t idx = canlog_id_index(s->id);
    uint16_t need = 4 + dlc + (idx == ESPDASH_CANLOG_ID_ESCAPE ? 2 : 0);

    if (canlog_count == 0) {
        canlog_base_ms = s->ms;
        canlog_first_push_ms = millis();
    }
    // Flush before overflowing either the byte budget or the frame count.
    if (canlog_len + need > CANLOG_BATCH_MAX_BYTES ||
        canlog_count >= ESPDASH_CANLOG_MAX_FRAMES) {
        canlog_send_batch();
        canlog_base_ms = s->ms;
        canlog_first_push_ms = millis();
    }

    uint32_t delta = s->ms - canlog_base_ms;
    if (delta > 0xFFFF) delta = 0xFFFF;   // clamp; batches span far less than 65 s

    uint8_t *p = canlog_buf + canlog_len;
    *p++ = idx;
    *p++ = dlc;
    *p++ = (uint8_t)(delta & 0xFF);
    *p++ = (uint8_t)(delta >> 8);
    if (idx == ESPDASH_CANLOG_ID_ESCAPE) {
        *p++ = (uint8_t)(s->id & 0xFF);
        *p++ = (uint8_t)((s->id >> 8) & 0xFF);
    }
    memcpy(p, s->data, dlc);
    canlog_len += need;
    canlog_count++;
}

// Send the id table periodically so a recording that starts mid-stream is
// still decodable on its own.
static void canlog_send_ids(uint32_t now) {
    if (canlog_id_count == 0) return;
    uint8_t pkt[sizeof(EspDashHeader) + sizeof(EspDashCanLogIdsHdr) + CANLOG_ID_TABLE_MAX * 2];
    EspDashHeader *h = (EspDashHeader *)pkt;
    h->magic       = ESPDASH_MAGIC;
    h->msg_type    = ESPDASH_MSG_CANLOG_IDS;
    h->proto_major = ESPDASH_PROTO_MAJOR;
    h->proto_minor = ESPDASH_PROTO_MINOR;
    h->payload_len = (uint16_t)(sizeof(EspDashCanLogIdsHdr) + canlog_id_count * 2);
    h->seq         = espnow_seq++;

    EspDashCanLogIdsHdr *ih = (EspDashCanLogIdsHdr *)(pkt + sizeof(EspDashHeader));
    ih->count = canlog_id_count;
    ih->reserved = 0;
    uint8_t *ids = pkt + sizeof(EspDashHeader) + sizeof(EspDashCanLogIdsHdr);
    for (uint8_t i = 0; i < canlog_id_count; i++) {
        ids[i*2]     = (uint8_t)(canlog_id_table[i] & 0xFF);
        ids[i*2 + 1] = (uint8_t)(canlog_id_table[i] >> 8);
    }
    esp_now_send(broadcast_mac, pkt,
                 sizeof(EspDashHeader) + sizeof(EspDashCanLogIdsHdr) + canlog_id_count * 2);
    last_canlog_ids_send = now;
}

// Flush a partial batch that has been sitting too long (quiet bus), and
// refresh the id table every 2 s.
static void canlog_flush_if_due(uint32_t now) {
    if (canlog_count > 0 && (now - canlog_first_push_ms) >= 20) {
        canlog_send_batch();
    }
    if (now - last_canlog_ids_send >= 2000) {
        canlog_send_ids(now);
    }
}

// =========================================================================
// CORE 0: PUBLISH TASK - owns every byte of output
// =========================================================================

// Dynamic signals must not sit frozen when the bus goes quiet: showing 5000
// rpm on a switched-off car is worse than showing zero. Slow-moving signals
// (coolant, fuel, battery, ambient, gear) legitimately persist and are left
// at their last known value.
static void apply_staleness(CanDecodeState *s, uint32_t now) {
    const uint32_t T = 2000;
    if (can_decode_is_stale(s, SIG_RPM, now, T))      s->rpm = 0;
    if (can_decode_is_stale(s, SIG_SPEED, now, T))    s->speed_kmh_x10 = 0;
    if (can_decode_is_stale(s, SIG_WHEELS, now, T)) {
        s->wheel_fl_x10 = s->wheel_fr_x10 = s->wheel_rl_x10 = s->wheel_rr_x10 = 0;
    }
    if (can_decode_is_stale(s, SIG_THROTTLE, now, T)) s->throttle_pct = 0;
    if (can_decode_is_stale(s, SIG_BRAKE, now, T))    s->brake_pct = 0;
    if (can_decode_is_stale(s, SIG_STEER, now, T))    s->steering_deg = 0;
}

static void publishTask(void *arg) {
    (void)arg;
    uint32_t last_json = 0, last_espnow = 0;
    float sim_t = 0.0f;
    char raw_buf[128];
    char json_buf[512];

    for (;;) {
        uint32_t now = millis();

        // ---- 1. Serial commands -----------------------------------------
        while (Serial.available()) {
            process_cmd_string(Serial.readStringUntil('\n'));
        }

#if ESPDASH_GATEWAY_WIFI
        // ---- 2. Network servicing (all WebSocket calls live here) --------
        if (WiFi.status() == WL_CONNECTED) {
            webSocket.loop();

            if (telnetServer.hasClient()) {
                WiFiClient nc = telnetServer.available();
                if (telnetClient && telnetClient.connected()) telnetClient.stop();
                telnetClient = nc;
            }
            if (telnetClient && telnetClient.connected() && telnetClient.available()) {
                process_cmd_string(telnetClient.readStringUntil('\n'));
            }
        }

        // Suspend streaming during OTA so the flash has the radio to itself.
        if (ota_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
#endif

        // ---- 3. Drain the raw ring --------------------------------------
        // Budget per pass keeps JSON/ESP-NOW cadence from being starved by a
        // burst; 64 frames/ms is ~45x the measured mean rate.
        //
        // Each popped frame goes to whichever sinks are active: the serial
        // RAW text stream (when in RAW/DUAL mode) and/or the ESP-NOW canlog
        // batcher. A frame is popped exactly once and fanned out, so enabling
        // the recorder never steals frames from a USB capture running at the
        // same time.
        int budget = 64;
        RawSlot s;
        bool serial_raw = (current_mode == MODE_RAW_SNIFFER || current_mode == MODE_DUAL);
        while (budget-- > 0 && ring_pop(&s)) {
            if (serial_raw) {
                int p = snprintf(raw_buf, sizeof(raw_buf), "RAW,%lu,0x%03X,%u,%u",
                                 (unsigned long)s.ms, (unsigned int)s.id, s.rtr, s.dlc);
                for (int i = 0; i < s.dlc && i < 8; i++) {
                    p += snprintf(raw_buf + p, sizeof(raw_buf) - p, ",%02X", s.data[i]);
                }
                snprintf(raw_buf + p, sizeof(raw_buf) - p, "\n");
                broadcast_line(raw_buf);
            }
            if (canlog_streaming) {
                canlog_add_frame(&s);
            }
        }
        // Flush a partial batch if it has been waiting too long, so the tail
        // of a capture is never stranded when the bus goes quiet.
        if (canlog_streaming) canlog_flush_if_due(now);

        // ---- 4. Snapshot -------------------------------------------------
        CanDecodeState snap;
        portENTER_CRITICAL(&state_mux);
        snap = g_snapshot;
        portEXIT_CRITICAL(&state_mux);

        if (demo_mode) {
            sim_t += 0.05f;
            apply_demo(&snap, sim_t);
        } else {
            apply_staleness(&snap, now);
        }

        // ---- 5. ESP-NOW @20Hz -------------------------------------------
        if (now - last_espnow >= 50) {
            last_espnow = now;

            uint8_t pkt[ESPDASH_PACKET_SIZE];
            EspDashHeader *h = (EspDashHeader *)pkt;
            h->magic       = ESPDASH_MAGIC;
            h->msg_type    = ESPDASH_MSG_TELEMETRY;
            h->proto_major = ESPDASH_PROTO_MAJOR;
            h->proto_minor = ESPDASH_PROTO_MINOR;
            h->payload_len = sizeof(EspDashTelemetry);
            h->seq         = espnow_seq++;

            EspDashTelemetry *t = (EspDashTelemetry *)(pkt + sizeof(EspDashHeader));
            t->rpm             = snap.rpm;
            t->speed_kmh_x10   = snap.speed_kmh_x10;
            t->water_temp_x10  = snap.water_temp_x10;
            t->oil_temp_x10    = snap.oil_temp_x10;
            t->battery_mv      = snap.battery_mv;
            t->gear            = (snap.gear == 0xFF) ? 0 : snap.gear;
            t->fuel_pct        = snap.fuel_pct;
            t->steering_deg    = snap.steering_deg;
            t->ambient_temp    = snap.ambient_temp;
            t->flags           = can_decode_flags(&snap);
            t->throttle_pct    = snap.throttle_pct;
            t->brake_pct       = snap.brake_pct;
            t->timestamp_ms    = now;
            t->wheel_fl_x10    = snap.wheel_fl_x10;
            t->wheel_fr_x10    = snap.wheel_fr_x10;
            t->wheel_rl_x10    = snap.wheel_rl_x10;
            t->wheel_rr_x10    = snap.wheel_rr_x10;

            // peer.channel = 0 makes ESP-NOW follow the station's current
            // channel. With ESPDASH_GATEWAY_WIFI=0 (the default) that channel
            // is fixed at boot and never changes, so this essentially cannot
            // fail for channel reasons. With the flag on, a connection lost
            // mid-run leaves the channel transiently undefined - the failure
            // mode the loop() fallback exists to recover from - and this
            // counter is what makes that failure visible instead of silent.
            if (esp_now_send(broadcast_mac, pkt, sizeof(pkt)) != ESP_OK) {
                espnow_send_fail++;
            }
        }

        // ---- 6. JSON telemetry @10Hz ------------------------------------
        // Key names and units are frozen: the web dashboard, log_collector.py
        // and tests/test_log_collector.py all bind to this exact schema.
        if ((current_mode == MODE_TELEMETRY || current_mode == MODE_DUAL) &&
            (now - last_json >= 100)) {
            last_json = now;

            uint8_t flags = can_decode_flags(&snap);
            snprintf(json_buf, sizeof(json_buf),
                "{\"type\":\"telemetry\",\"mac\":\"%s\",\"rpm\":%u,\"speed\":%.1f,"
                "\"water_temp\":%.1f,\"oil_temp\":%.1f,\"battery_v\":%.2f,\"gear\":%u,"
                "\"fuel\":%u,\"throttle\":%u,\"steering\":%d,\"brake\":%u,\"abs\":%s,"
                "\"tc\":%s,\"brake_sw\":%s,\"cel\":%s,\"vsa_warn\":%s,\"w_fl\":%.1f,"
                "\"w_fr\":%.1f,\"w_rl\":%.1f,\"w_rr\":%.1f,\"ambient\":%d,"
                "\"dropped\":%lu,\"timestamp\":%lu}\n",
                WiFi.macAddress().c_str(),
                snap.rpm,
                snap.speed_kmh_x10 / 10.0f,
                snap.water_temp_x10 / 10.0f,
                snap.oil_temp_x10 / 10.0f,
                snap.battery_mv / 1000.0f,
                (snap.gear == 0xFF) ? 0 : snap.gear,
                snap.fuel_pct,
                snap.throttle_pct,
                snap.steering_deg,
                snap.brake_pct,
                (flags & ESPDASH_FLAG_ABS_ACTIVE)   ? "true" : "false",
                (flags & ESPDASH_FLAG_TC_ACTIVE)    ? "true" : "false",
                (flags & ESPDASH_FLAG_BRAKE_SWITCH) ? "true" : "false",
                (flags & ESPDASH_FLAG_CEL)          ? "true" : "false",
                (flags & ESPDASH_FLAG_VSA_WARNING)  ? "true" : "false",
                snap.wheel_fl_x10 / 10.0f,
                snap.wheel_fr_x10 / 10.0f,
                snap.wheel_rl_x10 / 10.0f,
                snap.wheel_rr_x10 / 10.0f,
                snap.ambient_temp,
                (unsigned long)raw_dropped,
                (unsigned long)now);
            broadcast_line(json_buf);
        }

        vTaskDelay(1);  // 1 tick; yields the core to Wi-Fi between passes
    }
}

// =========================================================================
// ESP-NOW
// =========================================================================
static void init_esp_now() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Initialization failed!");
        return;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast_mac, 6);
    peer.channel = 0;   // follow the station channel
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[ESP-NOW] Failed to add broadcast peer!");
    } else {
        Serial.println("[ESP-NOW] Multicast peer active (FF:FF:FF:FF:FF:FF)");
    }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    can_decode_init(&g_state);
    can_decode_init(&g_snapshot);

    Serial.println("\n=================================================================");
    Serial.println(" espDash CENTRAL CAN GATEWAY (2014 HONDA CIVIC 9TH GEN)");
    Serial.println(" BAUD  : 500 kbps (TWAI_MODE_LISTEN_ONLY - never transmits)");
    Serial.printf(" PROTO : ESP-NOW v%d.%d, %u byte packet\n",
                  ESPDASH_PROTO_MAJOR, ESPDASH_PROTO_MINOR, (unsigned)ESPDASH_PACKET_SIZE);
    Serial.println("=================================================================");

    if (install_twai()) {
        Serial.println("[TWAI] Installed on GPIO15(TX)/GPIO16(RX), rx_queue_len=512");
    } else {
        Serial.println("[TWAI] INSTALL FAILED");
    }

#if ESPDASH_GATEWAY_WIFI
    wifiMulti.addAP("Comlex_parking", "12345678");
    wifiMulti.addAP("Complex_parking", "12345678");
    wifiMulti.addAP("Bazanski_ph", "52288488");
    wifiMulti.addAP("Bazanski_IS", "52288488");
    wifiMulti.addAP("IOT-monday", "fsdL2Dp*KBU0y#9F&c!Zbq853axj");

    WiFi.mode(WIFI_AP_STA);

    Serial.println("[Wi-Fi] Connecting (max 15s)...");
    uint32_t t0 = millis();
    bool connected = false;
    while (millis() - t0 < 15000) {
        if (wifiMulti.run() == WL_CONNECTED) { connected = true; break; }
        delay(150);
    }

    if (connected) {
        WiFi.setAutoReconnect(true);
        Serial.printf("[Wi-Fi SUCCESS] IP: %s (SSID: %s, MAC: %s, mDNS: esp32-gateway.local)\n",
                      WiFi.localIP().toString().c_str(), WiFi.SSID().c_str(),
                      WiFi.macAddress().c_str());
        if (MDNS.begin("esp32-gateway")) {
            MDNS.addService("espdash", "tcp", 8889);
            MDNS.addServiceTxt("espdash", "tcp", "mac", WiFi.macAddress());
            MDNS.addServiceTxt("espdash", "tcp", "device", "gateway");
        }
        ArduinoOTA.onStart([]() { ota_in_progress = true; });
        ArduinoOTA.onEnd([]() { ota_in_progress = false; });
        ArduinoOTA.onError([](ota_error_t) { ota_in_progress = false; });
        ArduinoOTA.begin();
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        telnetServer.begin();
        telnetServer.setNoDelay(true);
    } else {
        Serial.println("[Wi-Fi TIMEOUT] Not connected. Locking to fixed channel for ESP-NOW.");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    }
#else
    // No Wi-Fi station is ever attempted: no association, no scanning, no
    // roaming, and therefore nothing for a runtime fallback to abandon. This
    // is what actually fixed the on-car ESP-NOW instability - see the banner
    // comment at the top of this file. Set ESPDASH_GATEWAY_WIFI=1 in
    // platformio.ini and rebuild to restore mDNS/OTA/WebSocket/Telnet.
    Serial.println("[Wi-Fi] Disabled at build time (ESPDASH_GATEWAY_WIFI=0). USB + ESP-NOW only.");
    // Order matters: disconnect(true, true)'s second argument stops the Wi-Fi
    // radio outright (esp_wifi_stop()). WiFi.mode(WIFI_STA) must come AFTER
    // it, since that's what restarts the radio into STA mode - reversing
    // this order leaves the interface down and esp_now_send() failing with
    // ESP_ERR_ESPNOW_IF on every call, which is exactly what happened here
    // the first time this was written with the calls the other way round.
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
#endif

    // Wi-Fi STA modem sleep makes the radio doze between DTIM beacons, which
    // costs ESP-NOW packets on both ends of the link. The gateway is mains/
    // vehicle powered, so trade the power for a dependable 20 Hz.
    esp_wifi_set_ps(WIFI_PS_NONE);

    init_esp_now();

    // Both application tasks live on core 1; core 0 is left to the Wi-Fi/lwIP
    // stack AND the TinyUSB device task, which is what actually drains the USB
    // CDC FIFO. Running publishTask on core 0 starves that task and silently
    // drops CDC bytes - measured as ~10% byte loss shredding 58% of telemetry
    // lines into fragments, against 0 fragments with both tasks on core 1.
    //
    // Nothing is lost by this placement: canRxTask at priority 10 preempts
    // publishTask at 5, so a blocking write still cannot delay CAN reception,
    // which is the entire point of the split.
    xTaskCreatePinnedToCore(canRxTask,   "canRx",   4096, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(publishTask, "publish", 8192, NULL, 5,  NULL, 1);

    Serial.println("[SYSTEM] Gateway ready. canRxTask@core1 prio10, publishTask@core1 prio5.");
}

// =========================================================================
// LOOP (core 1, prio 1) - Wi-Fi/OTA maintenance when enabled, TWAI recovery
// always
// =========================================================================
void loop() {
    static uint32_t last_diag = 0;
    uint32_t now = millis();

#if ESPDASH_GATEWAY_WIFI
    static uint32_t last_wifi_check = 0;
    static uint32_t wifi_lost_since = 0;

    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }

    if (now - last_wifi_check >= 5000) {
        last_wifi_check = now;
        if (WiFi.status() == WL_CONNECTED) {
            wifi_lost_since = 0;
            String ip = WiFi.localIP().toString();
            if (ip != last_connected_ip) {
                last_connected_ip = ip;
                Serial.printf("[Wi-Fi] Connected IP: %s (SSID: %s)\n",
                              ip.c_str(), WiFi.SSID().c_str());
            }
        } else {
            last_connected_ip = "";

            // Wi-Fi that connected at boot and later drops (car drives out of
            // AP range) is NOT the same case as "no AP found within 15s at
            // boot" - only the latter was ever handled, by locking to a fixed
            // channel once in setup(). At runtime, WiFi.setAutoReconnect(true)
            // instead leaves the radio scanning/roaming indefinitely, which
            // means the ESP-NOW peer's channel=0 ("follow the station") is
            // tracking a channel that keeps moving or is transiently
            // undefined mid-reconnect. esp_now_send() can then fail silently
            // - nothing checked its return value before espnow_send_fail was
            // added - and the display node's own channel sweep has no fixed
            // target to lock onto, so it searches indefinitely. A full
            // gateway reboot "fixes" it only because it re-runs the boot-time
            // timeout path and lands back on a stable, fixed channel.
            //
            // This mitigation is what ESPDASH_GATEWAY_WIFI=0 (the default)
            // supersedes: with no Wi-Fi ever attempted, there's nothing to
            // lose and nothing to abandon. Kept here, gated, for when Wi-Fi
            // is deliberately re-enabled.
            if (!wifi_fallback_engaged) {
                if (wifi_lost_since == 0) {
                    wifi_lost_since = now;
                } else if (now - wifi_lost_since >= 20000) {
                    wifi_fallback_engaged = true;
                    Serial.println("[Wi-Fi] Lost for 20s - abandoning reconnect, "
                                   "locking ESP-NOW to a fixed channel for stability");
                    WiFi.setAutoReconnect(false);
                    WiFi.disconnect(true, true);
                    WiFi.mode(WIFI_STA);
                    esp_wifi_set_channel(ESPDASH_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
                }
            }
        }
    }
#endif

    if (now - last_diag >= 5000) {
        last_diag = now;

        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) == ESP_OK) {
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL) twai_queue_full_events++;
        }

        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            if (st.state == TWAI_STATE_BUS_OFF)      twai_initiate_recovery();
            else if (st.state == TWAI_STATE_STOPPED) twai_start();
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
