#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_now.h>
#include <ArduinoOTA.h>
#include <WebSocketsServer.h>
#include "driver/twai.h"

// =========================================================================
// 32-BYTE TELEMETRY PACKET PAYLOAD FOR ESP-NOW MULTICAST BROADCAST
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
    { GPIO_NUM_20, GPIO_NUM_19, "GPIO 20(TX) / GPIO 19(RX) [Default Waveshare]" },
    { GPIO_NUM_15, GPIO_NUM_16, "GPIO 15(TX) / GPIO 16(RX) [Waveshare Rev B]" },
    { GPIO_NUM_17, GPIO_NUM_18, "GPIO 17(TX) / GPIO 18(RX) [Waveshare Rev C]" },
    { GPIO_NUM_4,  GPIO_NUM_5,  "GPIO 4(TX) / GPIO 5(RX)" },
    { GPIO_NUM_1,  GPIO_NUM_2,  "GPIO 1(TX) / GPIO 2(RX)" }
};

static int active_pin_idx = 0;
static bool twai_installed = false;

// =========================================================================
// OPERATIONAL MODES & NETWORK SERVERS
// =========================================================================
enum OperationalMode {
    MODE_TELEMETRY = 0,  // Decoded telemetry stream (JSON / PLOT)
    MODE_RAW_SNIFFER = 1 // Raw CAN frame stream
};

static OperationalMode current_mode = MODE_TELEMETRY;

// Additional telemetry parameters
static uint8_t live_throttle_pct = 0;
static uint8_t live_engine_load_pct = 0;
static uint8_t live_brake_bar = 0;
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

void process_cmd_string(String cmd) {
    cmd.trim();
    cmd.toUpperCase();
    if (cmd == "MODE:PLOT" || cmd == "MODE_PLOT") {
        current_mode = MODE_TELEMETRY;
        broadcast_line("[MODE] Switched to TELEMETRY_PLOT\n");
    } else if (cmd == "MODE:RAW" || cmd == "MODE_RAW") {
        current_mode = MODE_RAW_SNIFFER;
        broadcast_line("[MODE] Switched to RAW_SNIFFER\n");
    } else if (cmd == "MODE:GET") {
        char buf[64];
        snprintf(buf, sizeof(buf), "[MODE] CURRENT:%s\n", (current_mode == MODE_TELEMETRY ? "TELEMETRY" : "RAW_SNIFFER"));
        broadcast_line(buf);
    }
}

void handle_incoming_telnet() {
    while (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        process_cmd_string(cmd);
    }
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

    // 2. Wi-Fi Multi & ArduinoOTA Setup
    wifiMulti.addAP("Complex_parking", "12345678");
    wifiMulti.addAP("Bazanski_ph", "52288488");
    wifiMulti.addAP("Bazanski_IS", "52288488");
    wifiMulti.addAP("IOT-monday", "fsdL2Dp*KBU0y#9F&c!Zbq853axj");

    Serial.println("[Wi-Fi] Connecting to Wi-Fi networks...");
    WiFi.mode(WIFI_AP_STA);
    wifiMulti.run();

    ArduinoOTA.setHostname("esp32-gateway");
    ArduinoOTA.begin();

    // 3. WebSockets & Telnet Servers
    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);

    telnetServer.begin();
    telnetServer.setNoDelay(true);

    // 4. ESP-NOW Initialization
    init_esp_now();

    Serial.println("[SYSTEM] Gateway Ready! WebSockets on Port 8888, Telnet on Port 8889.");
}

// =========================================================================
// MAIN LOOP
// =========================================================================
void loop() {
    ArduinoOTA.handle();
    webSocket.loop();
    handle_incoming_telnet();

    uint32_t now = millis();

    // 1. Wi-Fi Connection Maintenance
    if (now - last_wifi_check >= 2500) {
        last_wifi_check = now;
        if (wifiMulti.run() == WL_CONNECTED) {
            String cur_ip = WiFi.localIP().toString();
            if (cur_ip != last_connected_ip) {
                last_connected_ip = cur_ip;
                Serial.printf("[Wi-Fi SUCCESS] IP: %s (SSID: %s, mDNS: esp32-gateway.local)\n", 
                    cur_ip.c_str(), WiFi.SSID().c_str());
            }
        } else {
            last_connected_ip = "";
        }
    }

    // 2. TWAI Diagnostics & Auto-Pin Switcher
    if (now - last_diag_time >= 3000) {
        last_diag_time = now;
        twai_status_info_t status;
        if (twai_get_status_info(&status) == ESP_OK) {
            if (total_can_frames_received == 0 && (now - last_pin_scan_time >= 6000)) {
                last_pin_scan_time = now;
                active_pin_idx = (active_pin_idx + 1) % (sizeof(pin_candidates) / sizeof(pin_candidates[0]));
                char switch_msg[128];
                snprintf(switch_msg, sizeof(switch_msg), "[TWAI SCAN] Trying pin pair: %s\n", pin_candidates[active_pin_idx].name);
                broadcast_line(switch_msg);
                try_install_twai(pin_candidates[active_pin_idx].tx, pin_candidates[active_pin_idx].rx);
            }

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

        // A. 0x17C - Engine Speed RPM (100Hz)
        if (id == 0x17C && dlc >= 4) {
            uint16_t raw_rpm = ((uint16_t)rx_msg.data[2] << 8) | rx_msg.data[3];
            if (raw_rpm < 9000) {
                current_telemetry.rpm = raw_rpm;
            }
        }

        // B. 0x156 - Vehicle Speed (km/h) & Gear Selector Position (50Hz)
        if (id == 0x156 && dlc >= 5) {
            uint16_t spd_x10 = (uint16_t)((rx_msg.data[1] / 2.0f) * 10.0f);
            current_telemetry.speed_kmh_x10 = spd_x10;
            current_telemetry.gear = rx_msg.data[4]; // Direct raw CAN gear selector byte (0=P, 1=P, 2=R, 3=N, 4=D, 5=S)
        }

        // C. 0x1A4 - Throttle Position (50Hz)
        if (id == 0x1A4 && dlc >= 2) {
            live_throttle_pct = (uint8_t)((rx_msg.data[1] / 255.0f) * 100.0f);
        }

        // D. 0x091 - Engine Load (50Hz)
        if (id == 0x091 && dlc >= 1) {
            live_engine_load_pct = (uint8_t)((rx_msg.data[0] / 255.0f) * 100.0f);
        }

        // E. 0x1D0 / 0x158 / 0x1D6 - Coolant & Oil Temp (10Hz)
        if ((id == 0x1D0 || id == 0x158 || id == 0x1D6) && dlc >= 1) {
            if (rx_msg.data[0] > 0) {
                current_telemetry.water_temp_x10 = (int16_t)(((float)rx_msg.data[0] - 40.0f) * 10.0f);
            }
            if (dlc >= 5 && rx_msg.data[4] > 0) {
                current_telemetry.oil_temp_x10 = (int16_t)(((float)rx_msg.data[4] - 40.0f) * 10.0f);
            }
        }

        // F. 0x1AA - Steering Wheel Angle (50Hz)
        if (id == 0x1AA && dlc >= 2) {
            current_telemetry.steering_deg = (int16_t)((rx_msg.data[0] << 8) | rx_msg.data[1]);
        }

        // G. 0x1B0 - Brake Pedal Pressure (50Hz)
        if (id == 0x1B0 && dlc >= 2) {
            live_brake_bar = rx_msg.data[1];
        }

        // H. 0x13C - Fuel Tank Level % (10Hz)
        if (id == 0x13C && dlc >= 2) {
            current_telemetry.fuel_pct = rx_msg.data[1];
        }

        // I. 0x305 - Ambient Outdoor Air Temp (5Hz)
        if (id == 0x305 && dlc >= 1) {
            current_telemetry.ambient_temp = (int8_t)(rx_msg.data[0] - 40);
        }

        // J. 0x309 - 12V Battery Voltage (5Hz)
        if (id == 0x309 && dlc >= 2) {
            current_telemetry.battery_mv = (uint16_t)(rx_msg.data[1] * 100); // e.g. 138 * 100 = 13800 mV
        }

        // K. OBD-II 29-bit Response Decoding (0x18DAF10E / 0x7E8)
        if ((id == 0x18DAF10E || id == 0x7E8) && dlc >= 4 && rx_msg.data[1] == 0x41) {
            uint8_t pid = rx_msg.data[2];
            if (pid == 0x0C && dlc >= 5) {
                current_telemetry.rpm = (uint16_t)((((uint16_t)rx_msg.data[3] << 8) | rx_msg.data[4]) / 4.0f);
            } else if (pid == 0x05 && dlc >= 4) {
                current_telemetry.water_temp_x10 = (int16_t)(((float)rx_msg.data[3] - 40.0f) * 10.0f);
            } else if (pid == 0x5C && dlc >= 4) {
                current_telemetry.oil_temp_x10 = (int16_t)(((float)rx_msg.data[3] - 40.0f) * 10.0f);
            }
        }

        // Update Flags
        current_telemetry.flags = 0;
        if (current_telemetry.rpm > 400) current_telemetry.flags |= 0x01; // Engine Running
        if (current_telemetry.rpm > 6800) current_telemetry.flags |= 0x02; // Shift Warning
        if (current_telemetry.water_temp_x10 > 1050) current_telemetry.flags |= 0x04; // Overheat
        current_telemetry.timestamp_ms = now;

        // RAW SNIFFER STREAM OUTPUT
        if (current_mode == MODE_RAW_SNIFFER) {
            char raw_buf[128];
            int p = snprintf(raw_buf, sizeof(raw_buf), "RAW,%lu,0x%03X,%u,%u", now, (unsigned int)id, rx_msg.rtr, dlc);
            for (int i = 0; i < dlc && i < 8; i++) {
                p += snprintf(raw_buf + p, sizeof(raw_buf) - p, ",%02X", rx_msg.data[i]);
            }
            snprintf(raw_buf + p, sizeof(raw_buf) - p, "\n");
            broadcast_line(raw_buf);
        }
    }

    // 5. ESP-NOW Multicast Broadcast (20Hz / 50ms interval)
    if (now - last_esp_now_time >= 50) {
        last_esp_now_time = now;
        esp_now_send(broadcast_mac, (uint8_t*)&current_telemetry, sizeof(TelemetryPacket));
    }

    // 6. JSON Telemetry Broadcast for Web Dashboard (10Hz / 100ms interval)
    if (current_mode == MODE_TELEMETRY && (now - last_plot_time >= 100)) {
        last_plot_time = now;

        char json_buf[256];
        snprintf(json_buf, sizeof(json_buf), 
            "{\"type\":\"telemetry\",\"rpm\":%u,\"speed\":%.1f,\"water_temp\":%.1f,\"oil_temp\":%.1f,\"battery_v\":%.2f,\"gear\":%u,\"fuel\":%u,\"throttle\":%u,\"steering\":%d,\"brake\":%u,\"ambient\":%d,\"timestamp\":%lu}\n",
            current_telemetry.rpm,
            current_telemetry.speed_kmh_x10 / 10.0f,
            current_telemetry.water_temp_x10 / 10.0f,
            current_telemetry.oil_temp_x10 / 10.0f,
            current_telemetry.battery_mv / 1000.0f,
            current_telemetry.gear,
            current_telemetry.fuel_pct,
            live_throttle_pct,
            current_telemetry.steering_deg,
            live_brake_bar,
            current_telemetry.ambient_temp,
            now
        );
        broadcast_line(json_buf);
    }
}
