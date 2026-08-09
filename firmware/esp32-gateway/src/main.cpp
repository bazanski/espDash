// =========================================================================
// espDash CENTRAL CAN GATEWAY - 2014 Honda Civic 9th Gen
// =========================================================================
// SAFETY: the TWAI controller is installed in TWAI_MODE_LISTEN_ONLY and this
// firmware never transmits a CAN frame. Do not change that.
//
// CONCURRENCY
//   core 1  canRxTask  (prio 10) - twai_receive + decode + ring_push. No I/O.
//   core 0  publishTask (prio 5) - ALL output: raw drain, JSON, ESP-NOW, and
//                                  every WebSocket/telnet/Serial call.
//   core 1  loopTask   (prio 1)  - ArduinoOTA + Wi-Fi maintenance only.
//
// Output used to happen inside the TWAI receive loop, so a TCP retransmit or
// a slow USB host stalled CAN reception outright. At the measured 1399
// frames/s the driver's queue is only ~183 ms deep, so those stalls silently
// lost frames. The ring buffer decouples the two; overflow now drops the
// oldest frame and increments a counter reported as "dropped" in the JSON.
//
// The WebSockets library is NOT thread-safe: webSocket.loop() and
// broadcastTXT() must both stay inside publishTask.
// =========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include "driver/twai.h"

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

static volatile OperationalMode current_mode = MODE_TELEMETRY;
static volatile bool demo_mode = false;
static volatile bool ota_in_progress = false;

// =========================================================================
// SHARED STATE  (canRxTask writes, publishTask reads, guarded by spinlock)
// =========================================================================
static CanDecodeState g_state;                       // owned by canRxTask
static CanDecodeState g_snapshot;                    // published copy
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

// =========================================================================
// RAW FRAME RING BUFFER  (canRxTask produces, publishTask consumes)
// =========================================================================
#define RAW_RING_SIZE 512

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
static WiFiMulti wifiMulti;
static WebSocketsServer webSocket = WebSocketsServer(8888);
static WiFiServer telnetServer(8889);
static WiFiClient telnetClient;
static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint16_t espnow_seq = 0;
static String last_connected_ip = "";

// All three transports get byte-identical payloads. MUST only be called from
// publishTask (WebSockets library is not thread-safe).
static void broadcast_line(const char *str) {
    size_t len = strlen(str);
    Serial.write((const uint8_t *)str, len);
    if (telnetClient && telnetClient.connected()) {
        telnetClient.write((const uint8_t *)str, len);
    }
    webSocket.broadcastTXT((uint8_t *)str, len);
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
    } else if (cmd == "STATS") {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "[STATS] decoded:%lu cksum_rejects:%lu ring_dropped:%lu twai_qfull:%lu\n",
                 (unsigned long)g_snapshot.frames_decoded,
                 (unsigned long)g_snapshot.checksum_rejects,
                 (unsigned long)raw_dropped,
                 (unsigned long)twai_queue_full_events);
        broadcast_line(buf);
    }
}

static void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    if (type == WStype_TEXT) {
        String cmd = String((char *)payload, length);
        process_cmd_string(cmd);
    } else if (type == WStype_CONNECTED) {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WebSocket] Client #%u connected from %s\n", num, ip.toString().c_str());
    }
}

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

        // ---- 3. Drain the raw ring --------------------------------------
        // Budget per pass keeps JSON/ESP-NOW cadence from being starved by a
        // burst; 64 frames/ms is ~45x the measured mean rate.
        int budget = 64;
        RawSlot s;
        while (budget-- > 0 && ring_pop(&s)) {
            int p = snprintf(raw_buf, sizeof(raw_buf), "RAW,%lu,0x%03X,%u,%u",
                             (unsigned long)s.ms, (unsigned int)s.id, s.rtr, s.dlc);
            for (int i = 0; i < s.dlc && i < 8; i++) {
                p += snprintf(raw_buf + p, sizeof(raw_buf) - p, ",%02X", s.data[i]);
            }
            snprintf(raw_buf + p, sizeof(raw_buf) - p, "\n");
            broadcast_line(raw_buf);
        }

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

            esp_now_send(broadcast_mac, pkt, sizeof(pkt));
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
        Serial.println("[Wi-Fi TIMEOUT] Not connected. Locking to channel 1 for ESP-NOW.");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    }

    init_esp_now();

    // canRxTask on core 1 at high priority: it preempts loopTask so decoding
    // is never delayed by OTA or Wi-Fi bookkeeping. publishTask on core 0
    // sits alongside the Wi-Fi/lwIP stack that its socket writes feed.
    xTaskCreatePinnedToCore(canRxTask,   "canRx",   4096, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(publishTask, "publish", 8192, NULL, 5,  NULL, 0);

    Serial.println("[SYSTEM] Gateway ready. canRxTask@core1 prio10, publishTask@core0 prio5.");
}

// =========================================================================
// LOOP (core 1, prio 1) - OTA and Wi-Fi maintenance only
// =========================================================================
void loop() {
    static uint32_t last_wifi_check = 0, last_diag = 0;

    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }

    uint32_t now = millis();

    if (now - last_wifi_check >= 5000) {
        last_wifi_check = now;
        if (WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            if (ip != last_connected_ip) {
                last_connected_ip = ip;
                Serial.printf("[Wi-Fi] Connected IP: %s (SSID: %s)\n",
                              ip.c_str(), WiFi.SSID().c_str());
            }
        } else {
            last_connected_ip = "";
        }
    }

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
