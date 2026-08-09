# 🏛️ espDash System Architecture Specification

## Overview

**espDash** is a low-latency, modular automotive telemetry network designed for the **2014 Honda Civic (9th Gen)**.

---

## 1. Gateway Dual Wireless Architecture

The central ESP32-S3 gateway maintains two concurrent output channels:

1. **ESP-NOW 2.4 GHz Peer-to-Peer Multicast Broadcast:**
   * Transmits a compact **32-byte TelemetryPacket** at 20Hz–50Hz to in-car display nodes (~1-3ms latency, zero connection handshake wait).
2. **WebSocket / WebSerial Debug Channel (Port 8888):**
   * Serves live telemetry JSON and raw CAN frames to the web dashboard on laptop/tablet over Wi-Fi (`10.0.0.43` / `esp32-gateway.local`) or USB WebSerial (`navigator.serial`).

---

## 2. TelemetryPacket Payload (32 Bytes)

```cpp
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
```

---

## 4. Future Expansion & Sensor Roadmap

### 📍 GY-BNO08X (BNO080 / BNO085) 9-DOF IMU Integration (Investigate Phase)
* **Objective:** Connect a GY-BNO08X 9-axis Motion Sensor (I2C / SPI) to the Central ESP32-S3 Gateway.
* **Capabilities:**
  * **3-Axis Acceleration & G-Force Values:** Real-time lateral/longitudinal G-force monitoring for performance cornering and acceleration.
  * **Vehicle Pitch & Roll Tilt Angle:** Track incline, slope, and body roll dynamics.
  * **Parking Hit & Shock Detection:** Ultra-low-power accelerometer wake-up mode to detect parking bumps, impacts, or tampering while the vehicle is parked.
  * **Telemetry Payload Integration:** Extend `TelemetryPacket` with `int16_t g_force_x_mg`, `int16_t g_force_y_mg`, `int16_t pitch_deg_x10`, `int16_t roll_deg_x10`.

---

## 5. Wireless Protocol Co-existence (ESP-NOW + BLE + Wi-Fi)

* **ESP-NOW & BLE Simultaneous Operation:** ESP32-S3 features hardware Wi-Fi/Bluetooth Coexistence (`esp_coex`). ESP-NOW and BLE can operate concurrently.
* **ESP-NOW Performance Optimization:** Disabling standard Wi-Fi router STA connection when in pure track mode eliminates Wi-Fi beacon listening overhead and reduces ESP-NOW packet latency to `< 0.5 ms`.

### 5.1 Two Wi-Fi/ESP-NOW defects found and fixed (2026-08-09)

Both were invisible in bench testing (both boards near a stable, in-range network) and only showed
up once the gateway and node were run somewhere without one, i.e. the actual use case.

**Wi-Fi power-save silently drops most ESP-NOW packets.** With both boards associated to an AP,
STA modem sleep parks the radio between DTIM beacons and it sleeps through most ESP-NOW broadcasts.
Measured on the bench with the gateway sending at 20 Hz:

  | | rate received | delivery |
  |---|---|---|
  | before `esp_wifi_set_ps(WIFI_PS_NONE)` | 3.4–8.8 Hz | ~30% |
  | after | 19.7–20.2 Hz | 100% (244/244, 0 gaps) |

Fixed by calling `esp_wifi_set_ps(WIFI_PS_NONE)` on both the gateway and every display node, right
before `esp_now_init()`. Both devices are vehicle/mains powered, so the extra draw is an acceptable
trade for a dependable link — this does not apply to a battery node design.

**A Wi-Fi connection that drops mid-drive can silently kill ESP-NOW until the gateway is rebooted.**
The gateway's boot sequence only ever handled "no known network found within 15 s" — in that case it
locks the radio to a fixed channel and stays there for the rest of the boot, which is stable.
It never handled "connected fine at boot, then drove out of range." In that case
`WiFi.setAutoReconnect(true)` leaves the radio scanning/roaming indefinitely, and the ESP-NOW peer
is configured with `channel = 0` ("follow the station's current channel") — which is only
well-defined while genuinely associated. Mid-reconnect, that channel is a moving target, so
`esp_now_send()` can fail outright (previously unchecked) or succeed while broadcasting on a channel
no display node is listening to. Symptom observed on-car: the round gauge searched continuously and
never found the gateway, and was only fixed by power-cycling the *gateway* (not the node) — the
reboot re-runs the 15 s boot-time timeout and lands back on the stable fixed-channel path.

Fixed in `firmware/esp32-gateway/src/main.cpp`'s `loop()`: if Wi-Fi has been down for 20 s
(long enough to not react to a momentary blip, short enough not to leave the car undriveable-by-gauge
for long), the gateway deliberately abandons the reconnect attempt — `setAutoReconnect(false)`,
disconnects, and locks to channel 1, mirroring the boot-time fallback. ESP-NOW telemetry is the
higher-priority function while driving; regaining the dashboard/OTA link over Wi-Fi can wait for the
next reboot. `esp_now_send()` failures are now counted (`espnow_send_fail`) and the fallback state
is reported (`wifi_fallback:yes/no`), both visible via the gateway's `STATS` serial command — so this
class of failure is diagnosable next time instead of requiring another blind reboot-and-guess.

> **Universal Gauge Design:** All display nodes run a universal, configurable telemetry display engine capable of rendering gauges, digital readouts, bar graphs, and warnings. Display nodes are currently general-purpose telemetry gauges; specific dedicated functions will be assigned in future iterations.

* **Gateway:** `esp32-gateway.local` (Waveshare ESP32-S3-RS485-CAN)
* **Node 1:** `esp32-gauge-3inch.local` (Waveshare ESP32-S3-LCD-3.16 — Universal Telemetry Display)
* **Node 2:** `esp32-gauge-round.local` (Seeed XIAO ESP32-S3 + JXL Round — Universal Circular Gauge)
* **Node 3:** `esp32-gauge-amoled.local` (Waveshare ESP32-S3-Touch-AMOLED-1.32 — Universal Touch Gauge)
* **Node 4:** `esp32-gauge-147.local` (Waveshare ESP32-C6-LCD-1.47 — Universal Telemetry Display, Touch or Non-Touch TBD)
* **Node 5:** `esp32-oled.local` (Generic ESP32-S3 + I2C OLED — Universal Gauge / Shift Light)
* **Node 6:** `esp32-heltec-relay.local` (Heltec Meshtastic LoRa Bridge to Home Assistant)
