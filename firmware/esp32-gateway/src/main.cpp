#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include "driver/twai.h"

// =========================================================================
// 34-BYTE TELEMETRY PACKET PAYLOAD FOR ESP-NOW MULTICAST BROADCAST
// =========================================================================
typedef struct __attribute__((packed)) {
    uint16_t rpm;           // 0 - 9000 RPM (1 RPM resolution)
    uint16_t speed_kmh_x10; // 0 - 300.0 km/h (0.1 km/h resolution)
    int16_t  water_temp_x10;// -40.0 to +150.0 °C (0.1 °C resolution)
    int16_t  oil_temp_x10;  // -40.0 to +150.0 °C (0.1 °C resolution)
    uint16_t battery_mv;    // 0 - 20,000 mV (e.g., 13800 = 13.8V)
    uint8_t  gear;          // 0=P, 1=R, 2=N, 3=D, 4=S, 5=1st, 6=2nd, etc.
    uint8_t  fuel_pct;      // 0 - 100 %
    int16_t  steering_deg;  // -720 to +720 degrees
    int8_t   ambient_temp;  // -40 to +80 °C
    uint8_t  flags;         // Bit 0: Engine Running, Bit 1: Shift Warning, Bit 2: Overheat
    uint8_t  throttle_pct;  // 0 - 100 % throttle pedal position
    uint8_t  brake_bar;     // 0 - 255 bar brake fluid pressure
    uint32_t timestamp_ms;  // Gateway Uptime in ms
} TelemetryPacket;

static TelemetryPacket current_telemetry = {0};

// =========================================================================
// WAVESHARE ESP32-S3-RS485-CAN PIN PAIRS TO SCAN
// =========================================================================
struct CANPinPair {
    gpio_num_t tx;
    gpio_num_t rx;
    const char* name;
};

static CANPinPair pin_candidates[] = {
    { GPIO_NUM_15, GPIO_NUM_16, "GPIO 15(TX) / GPIO 16(RX) [Waveshare Rev B/C Standard]" },
    { GPIO_NUM_17, GPIO_NUM_18, "GPIO 17(TX) / GPIO 18(RX) [Waveshare Rev C Alternative]" },
    { GPIO_NUM_4,  GPIO_NUM_5,  "GPIO 4(TX) / GPIO 5(RX)" },
    { GPIO_NUM_1,  GPIO_NUM_2,  "GPIO 1(TX) / GPIO 2(RX)" }
};

static int active_pin_idx = 0;
static bool twai_installed = false;

// =========================================================================
// OPERATIONAL MODES & NETWORK SERVERS
// =========================================================================
enum OperationalMode {
    MODE_TELEMETRY = 0,   // Decoded telemetry stream (JSON / PLOT)
    MODE_RAW_SNIFFER = 1, // Raw CAN frame stream
    MODE_DUAL = 2         // BOTH Raw CAN frames + Decoded JSON Telemetry simultaneously
};

static OperationalMode current_mode = MODE_TELEMETRY;

// Global Demo Generator Toggle (Default: false, toggleable via DEMO:ON / DEMO:OFF / DEMO:TOGGLE)
static bool demo_mode = false;

// Additional telemetry parameters
static uint8_t live_throttle_pct = 0;
static uint8_t live_engine_load_pct = 0;
static uint8_t live_brake_bar = 0;
static uint16_t live_wheel_fl_x10 = 0;
static uint16_t live_wheel_fr_x10 = 0;
static uint16_t live_wheel_rl_x10 = 0;
static uint16_t live_wheel_rr_x10 = 0;
static uint32_t total_can_frames_received = 0;

// Timing counters
static uint32_t last_plot_time = 0;
static uint32_t last_esp_now_time = 0;
static uint32_t last_wifi_check = 0;
static uint32_t last_diag_time = 0;
static uint32_t last_pin_scan_time = 0;
static String last_connected_ip = "";

// Multi-Wi-Fi, WebSockets (Port 8888) & Raw Telnet (Port 8889)
static WiFiMulti wifiMulti;
static WebSocketsServer webSocket = WebSocketsServer(8888);
static WiFiServer telnetServer(8889);
static WiFiClient telnetClient;
// ESP-NOW Broadcast MAC Address
static uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// =========================================================================
// HELPER BROADCAST FUNCTION (Serial + Telnet + WebSockets)
// =========================================================================
void broadcast_line(const char* str) {
    size_t len = strlen(str);
    Serial.write((uint8_t*)str, len);

    if (telnetClient && telnetClient.connected()) {
        telnetClient.write((uint8_t*)str, len);
    }
    webSocket.broadcastTXT((uint8_t*)str, len);
}

// Synthetic Telemetry Generator for Desk/Bench Testing (Applies ONLY to Telemetry JSON & ESP-NOW)
void update_demo_telemetry() {
    static float sim_t = 0.0f;
    sim_t += 0.1f;

    float cycle = fmod(sim_t, 12.0f) / 12.0f; // 12-second acceleration cycle
    float rpm_val = 800.0f;
    uint8_t gear_idx = 3;
    float spd_val = 0.0f;

    if (cycle < 0.25f) {
        gear_idx = 1;
        rpm_val = 900.0f + (cycle / 0.25f) * 5600.0f; // 900 - 6500
        spd_val = (rpm_val / 6500.0f) * 35.0f;
    } else if (cycle < 0.55f) {
        gear_idx = 2;
        float progress = (cycle - 0.25f) / 0.30f;
        rpm_val = 3000.0f + progress * 4000.0f; // 3000 - 7000 (Shift warning at 6800+)
        spd_val = 35.0f + progress * 35.0f;
    } else if (cycle < 0.85f) {
        gear_idx = 3;
        float progress = (cycle - 0.55f) / 0.30f;
        rpm_val = 3500.0f + progress * 3500.0f; // 3500 - 7000
        spd_val = 70.0f + progress * 45.0f;
    } else {
        gear_idx = 4;
        float progress = (cycle - 0.85f) / 0.15f;
        rpm_val = 3500.0f - progress * 1200.0f; // 3500 - 2300
        spd_val = 115.0f - progress * 25.0f;
    }

    uint8_t throt = (uint8_t)constrain((int)(max(0.0f, sinf(sim_t * 1.2f)) * 100.0f), 0, 100);
    float water_temp = 87.0f + 3.0f * sinf(sim_t * 0.1f);
    float oil_temp = 92.0f + 4.0f * sinf(sim_t * 0.08f);
    float batt_v = 13.8f + 0.3f * sinf(sim_t * 0.5f);
    int16_t steer = (int16_t)(160.0f * sinf(sim_t * 0.3f));

    current_telemetry.rpm = (uint16_t)rpm_val;
    current_telemetry.speed_kmh_x10 = (uint16_t)(spd_val * 10.0f);
    current_telemetry.water_temp_x10 = (int16_t)(water_temp * 10.0f);
    current_telemetry.oil_temp_x10 = (int16_t)(oil_temp * 10.0f);
    current_telemetry.battery_mv = (uint16_t)(batt_v * 1000.0f);
    current_telemetry.gear = gear_idx;
    current_telemetry.fuel_pct = 78;
    current_telemetry.throttle_pct = throt;
    current_telemetry.steering_deg = steer;
    current_telemetry.ambient_temp = 23;

    float corner = (steer / 160.0f) * 3.5f;
    live_wheel_fl_x10 = (uint16_t)(max(0.0f, spd_val - corner) * 10.0f);
    live_wheel_fr_x10 = (uint16_t)(max(0.0f, spd_val + corner) * 10.0f);
    live_wheel_rl_x10 = (uint16_t)(max(0.0f, spd_val - corner * 0.7f) * 10.0f);
    live_wheel_rr_x10 = (uint16_t)(max(0.0f, spd_val + corner * 0.7f) * 10.0f);

    current_telemetry.flags = 0x01; // Engine Running
    if (current_telemetry.rpm > 6800) current_telemetry.flags |= 0x02; // Shift Warning
    if (throt < 10) {
        current_telemetry.flags |= 0x20; // Brake switch
        live_brake_bar = 25;
    } else {
        live_brake_bar = 0;
    }
}

void process_cmd_string(String cmd) {
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
        snprintf(buf, sizeof(buf), "[DEMO] Demo Telemetry is now %s\n", demo_mode ? "ENABLED" : "DISABLED");
        broadcast_line(buf);
    } else if (cmd == "MODE:GET") {
        char buf[100];
        const char* mode_name = (current_mode == MODE_TELEMETRY ? "TELEMETRY" : (current_mode == MODE_RAW_SNIFFER ? "RAW_SNIFFER" : "DUAL"));
        snprintf(buf, sizeof(buf), "[MODE] CURRENT:%s | DEMO:%s\n", mode_name, demo_mode ? "ON" : "OFF");
        broadcast_line(buf);
    }
}

void handle_incoming_serial() {
    while (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        process_cmd_string(cmd);
    }
}

void handle_incoming_telnet() {
    if (telnetClient && telnetClient.connected() && telnetClient.available()) {
        String cmd = telnetClient.readStringUntil('\n');
        process_cmd_string(cmd);
    }
}

// WebSocket Event Handler
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        String cmd = String((char*)payload);
        process_cmd_string(cmd);
    } else if (type == WStype_CONNECTED) {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[WebSocket] Client #%u connected from %s\n", num, ip.toString().c_str());
    }
}

// =========================================================================
// ESP-NOW INITIALIZATION
// =========================================================================
void init_esp_now() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Initialization failed!");
        return;
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcast_mac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] Failed to add broadcast peer!");
    } else {
        Serial.println("[ESP-NOW] Multicast peer active ({FF:FF:FF:FF:FF:FF})");
    }
}

// =========================================================================
// TWAI DRIVER INSTALLATION & HARDWARE SCANNER
// =========================================================================
bool try_install_twai(gpio_num_t tx_pin, gpio_num_t rx_pin) {
    if (twai_installed) {
        twai_stop();
        twai_driver_uninstall();
        twai_installed = false;
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(tx_pin, rx_pin, TWAI_MODE_LISTEN_ONLY);
    g_config.rx_queue_len = 256;

    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err == ESP_OK) {
        err = twai_start();
        if (err == ESP_OK) {
            twai_installed = true;
            return true;
        }
    }
    return false;
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=================================================================");
    Serial.println(" 🏎️ espDash CENTRAL CAN GATEWAY (2014 HONDA CIVIC 9TH GEN)");
    Serial.println(" BAUD  : 500 kbps (100% TWAI_MODE_LISTEN_ONLY Passive)");
    Serial.println("=================================================================");

    // 1. Install TWAI Driver
    try_install_twai(pin_candidates[active_pin_idx].tx, pin_candidates[active_pin_idx].rx);
    Serial.printf("[TWAI] Active Pin Candidate: %s\n", pin_candidates[active_pin_idx].name);

    // 2. Configure Multi-Wi-Fi Networks
    wifiMulti.addAP("Comlex_parking", "12345678");
    wifiMulti.addAP("Complex_parking", "12345678");
    wifiMulti.addAP("Bazanski_ph", "52288488");
    wifiMulti.addAP("Bazanski_IS", "52288488");
    wifiMulti.addAP("IOT-monday", "fsdL2Dp*KBU0y#9F&c!Zbq853axj");

    WiFi.mode(WIFI_AP_STA);

    // Wi-Fi Connection attempt with strict 15-second total timeout
    Serial.println("[Wi-Fi] Connecting to network (max 15s timeout)...");
    uint32_t wifi_start = millis();
    bool wifi_connected = false;
    while (millis() - wifi_start < 15000) {
        if (wifiMulti.run() == WL_CONNECTED) {
            wifi_connected = true;
            break;
        }
        delay(150);
    }

    if (wifi_connected) {
        WiFi.setAutoReconnect(true);
        Serial.printf("[Wi-Fi SUCCESS] IP: %s (SSID: %s, MAC: %s, mDNS: esp32-gateway.local)\n", 
            WiFi.localIP().toString().c_str(), WiFi.SSID().c_str(), WiFi.macAddress().c_str());
        if (MDNS.begin("esp32-gateway")) {
            MDNS.addService("espdash", "tcp", 8889);
            MDNS.addServiceTxt("espdash", "tcp", "mac", WiFi.macAddress());
            MDNS.addServiceTxt("espdash", "tcp", "device", "gateway");
        }
        ArduinoOTA.begin();
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        telnetServer.begin();
        telnetServer.setNoDelay(true);
    } else {
        Serial.println("[Wi-Fi TIMEOUT] Wi-Fi not connected after 15s. Disabling STA scan to prevent ESP-NOW lag.");
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Lock to Channel 1 for pure fast ESP-NOW
    }

    // 4. ESP-NOW Initialization
    init_esp_now();

    Serial.println("[SYSTEM] Gateway Ready! ESP-NOW Active at 20Hz.");
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    // ALWAYS process incoming USB Serial commands regardless of Wi-Fi connection state
    handle_incoming_serial();

    // Only handle network services if Wi-Fi is actively connected
    if (WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
        webSocket.loop();
        handle_incoming_telnet();
    }

    uint32_t now = millis();

    // 1. Wi-Fi Connection Maintenance & IP Logger
    if (now - last_wifi_check >= 5000) {
        last_wifi_check = now;
        if (WiFi.status() == WL_CONNECTED) {
            String cur_ip = WiFi.localIP().toString();
            if (cur_ip != last_connected_ip) {
                last_connected_ip = cur_ip;
                Serial.printf("[Wi-Fi SUCCESS] Connected IP: %s (SSID: %s, mDNS: esp32-gateway.local)\n", 
                    cur_ip.c_str(), WiFi.SSID().c_str());
            }
        } else {
            last_connected_ip = "";
        }
    }

    // 2. TWAI Diagnostics & Auto-Recovery
    if (now - last_diag_time >= 5000) {
        last_diag_time = now;
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (status.state == TWAI_STATE_BUS_OFF) {
                twai_initiate_recovery();
            } else if (status.state == TWAI_STATE_STOPPED) {
                twai_start();
            }
        }
    }

    // 3. Telnet Client Connection Management
    if (telnetServer.hasClient()) {
        WiFiClient newClient = telnetServer.available();
        if (telnetClient && telnetClient.connected()) {
            telnetClient.stop();
        }
        telnetClient = newClient;
    }

    // 4. TWAI CAN Frame Decoding Loop
    twai_message_t rx_msg;
    while (twai_installed && twai_receive(&rx_msg, 0) == ESP_OK) {
        total_can_frames_received++;
        uint32_t id = rx_msg.identifier;
        uint8_t dlc = rx_msg.data_length_code;

        // -----------------------------------------------------------------
        // HONDA CIVIC 9TH GEN CAN DECODING ENGINE
        // -----------------------------------------------------------------

        // A. 0x1DC (476 dec) - Engine RPM (50Hz) [Verified Correct]
        if (id == 0x1DC && dlc >= 3) {
            current_telemetry.rpm = ((uint16_t)rx_msg.data[1] << 8) | rx_msg.data[2];
        }

        // B. 0x17C (380 dec) - Accelerator Pedal Position / Throttle % (50Hz) [0 to 97 max]
        if (id == 0x17C && dlc >= 1) {
            live_throttle_pct = (uint8_t)min(100, (int)((rx_msg.data[0] / 97.0f) * 100.0f));
        }

        // C1. 0x188 (392 dec) - Honda Shifter Position (50Hz) [Inverted Byte 3 Mapping: P=4, R=1, N=8, D=0, S=2]
        if (id == 0x188 && dlc >= 4) {
            uint8_t b3 = rx_msg.data[3];
            if (b3 == 0x04)      current_telemetry.gear = 0; // P (Park)
            else if (b3 == 0x01) current_telemetry.gear = 1; // R (Reverse)
            else if (b3 == 0x08) current_telemetry.gear = 2; // N (Neutral)
            else if (b3 == 0x00) current_telemetry.gear = 3; // D (Drive)
            else if (b3 == 0x02) current_telemetry.gear = 4; // S (Sport)
        }

        // C2. 0x1D0 (464 dec) - 4-Wheel Speeds & Low-Speed Speedometer (50Hz) [0.01 km/h resolution]
        if (id == 0x1D0 && dlc >= 8) {
            live_wheel_fl_x10 = (uint16_t)((((uint16_t)rx_msg.data[0] << 8) | rx_msg.data[1]) / 10);
            live_wheel_fr_x10 = (uint16_t)((((uint16_t)rx_msg.data[2] << 8) | rx_msg.data[3]) / 10);
            live_wheel_rl_x10 = (uint16_t)((((uint16_t)rx_msg.data[4] << 8) | rx_msg.data[5]) / 10);
            live_wheel_rr_x10 = (uint16_t)((((uint16_t)rx_msg.data[6] << 8) | rx_msg.data[7]) / 10);

            // Vehicle Speedometer (reads crawling speeds 1-6 km/h instantly!)
            current_telemetry.speed_kmh_x10 = live_wheel_fl_x10;
        }

        // D. 0x1A4 (420 dec) - Instant Brake Light Switch & Master Cylinder Fluid Pressure (50Hz)
        if (id == 0x1A4 && dlc >= 2) {
            if (rx_msg.data[0] > 0) {
                current_telemetry.flags |= 0x20; // Brake Switch ON
            } else {
                current_telemetry.flags &= ~0x20; // Brake Switch OFF
            }

            uint8_t raw_b = rx_msg.data[1];
            if (raw_b > 30) {
                live_brake_bar = (uint8_t)min(100, (int)(((raw_b - 30) / 180.0f) * 100.0f));
            } else {
                live_brake_bar = 0;
            }
        }

        // E. 0x156 (342 dec) - Steering Wheel Angle (50Hz) [Signed 16-bit, 0 deg Center, -540 deg Left, +540 deg Right]
        if (id == 0x156 && dlc >= 2) {
            int16_t raw_steer = (int16_t)(((uint16_t)rx_msg.data[0] << 8) | rx_msg.data[1]);
            current_telemetry.steering_deg = (int16_t)(raw_steer / 9.0f);
        }

        // F. 0x324 (804 dec) - Coolant Temp & Fuel Tank Level (10Hz)
        if (id == 0x324 && dlc >= 2) {
            // Byte 0: Coolant Temp (0x7D = 125 -> 85.0 °C) [Verified Correct]
            if (rx_msg.data[0] > 0) {
                float t_val = (float)rx_msg.data[0] - 40.0f;
                if (t_val > 0.0f && t_val < 140.0f) {
                    current_telemetry.water_temp_x10 = (int16_t)(t_val * 10.0f);
                }
            }
            // Byte 1: Fuel Tank Level (0x64 = 100 -> 100 / 2.0 = 50.0%) [Verified Correct]
            uint8_t raw_f = rx_msg.data[1];
            if (raw_f <= 200) {
                current_telemetry.fuel_pct = (uint8_t)min(100, (int)(raw_f / 2.0f));
            }
        }

        // G. 0x1D0 (464 dec) - 4-Wheel Speeds (FL, FR, RL, RR) (50Hz) [0.01 km/h resolution]
        if (id == 0x1D0 && dlc >= 8) {
            live_wheel_fl_x10 = (uint16_t)((((uint16_t)rx_msg.data[0] << 8) | rx_msg.data[1]) / 10);
            live_wheel_fr_x10 = (uint16_t)((((uint16_t)rx_msg.data[2] << 8) | rx_msg.data[3]) / 10);
            live_wheel_rl_x10 = (uint16_t)((((uint16_t)rx_msg.data[4] << 8) | rx_msg.data[5]) / 10);
            live_wheel_rr_x10 = (uint16_t)((((uint16_t)rx_msg.data[6] << 8) | rx_msg.data[7]) / 10);
        }

        // H. 0x158 / 0x1D6 - Coolant & Oil Temp (10Hz)
        if ((id == 0x158 || id == 0x1D6) && dlc >= 1) {
            if (rx_msg.data[0] > 0) {
                float t_val = (float)rx_msg.data[0] - 40.0f;
                if (t_val > 0.0f && t_val < 140.0f) {
                    current_telemetry.water_temp_x10 = (int16_t)(t_val * 10.0f);
                }
            }
        }

        // I. 0x21E / 0x372 (542 / 882 dec) - Outdoor Ambient Air Temp (5Hz) [Direct °C (0x20 = 32°C)]
        if (id == 0x21E && dlc >= 5) {
            current_telemetry.ambient_temp = (int8_t)rx_msg.data[4];
        } else if (id == 0x372 && dlc >= 1) {
            current_telemetry.ambient_temp = (int8_t)rx_msg.data[0];
        }

        // J. 0x305 (773 dec) - 12V Battery Voltage (5Hz) [Byte 0 * 100 mV (142 -> 14.2V)]
        if (id == 0x305 && dlc >= 1) {
            uint8_t raw_v = rx_msg.data[0];
            if (raw_v >= 100 && raw_v <= 160) {
                current_telemetry.battery_mv = (uint16_t)(raw_v * 100);
            }
        }

        // K. 0x1A0 - VSA / ABS & Traction Control Flags & Warnings (50Hz)
        if (id == 0x1A0 && dlc >= 2) {
            if (rx_msg.data[1] & 0x08) current_telemetry.flags |= 0x08; // ABS Active
            else current_telemetry.flags &= ~0x08;
            if (rx_msg.data[0] & 0x02) current_telemetry.flags |= 0x10; // TC Active
            else current_telemetry.flags &= ~0x10;
            if (rx_msg.data[0] & 0x04) current_telemetry.flags |= 0x80; // VSA Warning
            else current_telemetry.flags &= ~0x80;
        }

        // Update Flags (preserve Bit 3 ABS, Bit 4 TC, Bit 5 Brake Switch)
        current_telemetry.flags &= 0x38; // Keep bits 3, 4, 5
        if (current_telemetry.rpm > 400) current_telemetry.flags |= 0x01; // Engine Running
        if (current_telemetry.rpm > 6800) current_telemetry.flags |= 0x02; // Shift Warning
        if (current_telemetry.water_temp_x10 > 1050) current_telemetry.flags |= 0x04; // Overheat
        current_telemetry.throttle_pct = live_throttle_pct;
        current_telemetry.brake_bar = live_brake_bar;
        current_telemetry.timestamp_ms = now;

        // RAW SNIFFER STREAM OUTPUT
        if (current_mode == MODE_RAW_SNIFFER || current_mode == MODE_DUAL) {
            char raw_buf[128];
            int p = snprintf(raw_buf, sizeof(raw_buf), "RAW,%lu,0x%03X,%u,%u", now, (unsigned int)id, rx_msg.rtr, dlc);
            for (int i = 0; i < dlc && i < 8; i++) {
                p += snprintf(raw_buf + p, sizeof(raw_buf) - p, ",%02X", rx_msg.data[i]);
            }
            snprintf(raw_buf + p, sizeof(raw_buf) - p, "\n");
            broadcast_line(raw_buf);
        }
    }

    // 5. ESP-NOW Multicast Broadcast (20Hz / 50ms interval for wireless round gauges)
    if (now - last_esp_now_time >= 50) {
        last_esp_now_time = now;
        if (demo_mode) {
            update_demo_telemetry();
        }
        current_telemetry.timestamp_ms = now;
        esp_now_send(broadcast_mac, (uint8_t*)&current_telemetry, sizeof(TelemetryPacket));
    }

    // 6. JSON Telemetry Broadcast for Web Dashboard & Serial (10Hz / 100ms interval)
    if ((current_mode == MODE_TELEMETRY || current_mode == MODE_DUAL) && (now - last_plot_time >= 100)) {
        last_plot_time = now;

        if (demo_mode) {
            update_demo_telemetry();
        }

        char json_buf[450];
        snprintf(json_buf, sizeof(json_buf), 
            "{\"type\":\"telemetry\",\"mac\":\"%s\",\"rpm\":%u,\"speed\":%.1f,\"water_temp\":%.1f,\"oil_temp\":%.1f,\"battery_v\":%.2f,\"gear\":%u,\"fuel\":%u,\"throttle\":%u,\"steering\":%d,\"brake\":%u,\"abs\":%s,\"tc\":%s,\"brake_sw\":%s,\"cel\":%s,\"vsa_warn\":%s,\"w_fl\":%.1f,\"w_fr\":%.1f,\"w_rl\":%.1f,\"w_rr\":%.1f,\"ambient\":%d,\"timestamp\":%lu}\n",
            WiFi.macAddress().c_str(),
            current_telemetry.rpm,
            current_telemetry.speed_kmh_x10 / 10.0f,
            current_telemetry.water_temp_x10 / 10.0f,
            current_telemetry.oil_temp_x10 / 10.0f,
            current_telemetry.battery_mv / 1000.0f,
            current_telemetry.gear,
            current_telemetry.fuel_pct,
            current_telemetry.throttle_pct,
            current_telemetry.steering_deg,
            current_telemetry.brake_bar,
            (current_telemetry.flags & 0x08) ? "true" : "false",
            (current_telemetry.flags & 0x10) ? "true" : "false",
            (current_telemetry.flags & 0x20) ? "true" : "false",
            (current_telemetry.flags & 0x40) ? "true" : "false",
            (current_telemetry.flags & 0x80) ? "true" : "false",
            live_wheel_fl_x10 / 10.0f,
            live_wheel_fr_x10 / 10.0f,
            live_wheel_rl_x10 / 10.0f,
            live_wheel_rr_x10 / 10.0f,
            current_telemetry.ambient_temp,
            now
        );
        broadcast_line(json_buf);
    }
}
