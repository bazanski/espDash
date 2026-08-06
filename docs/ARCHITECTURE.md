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

## 3. Node Hardware & Hostnames

> **Universal Gauge Design:** All display nodes run a universal, configurable telemetry display engine capable of rendering gauges, digital readouts, bar graphs, and warnings. Display nodes are currently general-purpose telemetry gauges; specific dedicated functions will be assigned in future iterations.

* **Gateway:** `esp32-gateway.local` (Waveshare ESP32-S3-RS485-CAN)
* **Node 1:** `esp32-gauge-3inch.local` (Waveshare ESP32-S3-LCD-3.16 — Universal Telemetry Display)
* **Node 2:** `esp32-gauge-round.local` (Seeed XIAO ESP32-S3 + JXL Round — Universal Circular Gauge)
* **Node 3:** `esp32-gauge-amoled.local` (Waveshare ESP32-S3-Touch-AMOLED-1.32 — Universal Touch Gauge)
* **Node 4:** `esp32-gauge-147.local` (Waveshare ESP32-C6-LCD-1.47 — Universal Telemetry Display, Touch or Non-Touch TBD)
* **Node 5:** `esp32-oled.local` (Generic ESP32-S3 + I2C OLED — Universal Gauge / Shift Light)
* **Node 6:** `esp32-heltec-relay.local` (Heltec Meshtastic LoRa Bridge to Home Assistant)
