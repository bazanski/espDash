# 🏎️ espDash - Distributed Automotive Telemetry System

**espDash** is an ultra-low-latency, car-safe, distributed automotive telemetry and multi-gauge display network built for the **2014 Honda Civic (9th Gen)**.

---

## 🏛️ System Architecture

```text
🚗 Honda Civic F-CAN Bus (500 kbps)
               │
               ▼
⚡ Central Gateway (Waveshare ESP32-S3-RS485-CAN) [firmware/esp32-gateway]
  ├── 100% Passive TWAI Listen-Only Receiver (Zero ECU Fault Risk)
  ├── INA228 I2C 12V Battery Monitor (GPIO 1 SDA / GPIO 2 SCL, addr 0x40)
  ├── 2-Tier Light/Deep Sleep Auto-Power Off (~15µA core, GPIO 16 CAN wake)
  ├── ESP-NOW v2.4 Multicast (46B telemetry @ 20Hz + raw CAN log streaming)
  └── USB WebSerial / JSON Telemetry Channel (Optional Wi-Fi/WS Port 8888)
               │
               ├───► 💻 Laptop / Tablet Station (web-dashboard / tools/log_collector.py)
               ├───► 🏎️ esp-rectangular-314: Waveshare 3.16" RGB LCD + SD Raw CAN Recorder
               ├───► 🏎️ xiao-round-gauge: Seeed XIAO ESP32-S3 + 240x240 Round TFT
               ├───► 🏎️ xiao-dual-round: Seeed XIAO ESP32-S3 + Dual 240x240 Round TFTs
               ├───► 🏎️ esp-round-amoled-touch: Waveshare 1.32" 466x466 AMOLED Touch
               ├───► 🏎️ esp-touch-350: LCD Wiki ES3C35P 3.5" Touch LCD + MPU-6500 IMU + PFD
               ├───► 📄 crowpanel-epaper-370: Elecrow CrowPanel 3.7" E-Paper (Slow Data & Parked Card)
               └───► 🏠 Planned: ESP32-C6 1.47", OLED Shift Light, Heltec LoRa Relay
```

> **Note:** Nodes share the single-source-of-truth wire protocol (`EspDashProto.h` v2.4) and 30-signal catalog (`EspDashSignals.h`). By default, both Gateway (`ESPDASH_GATEWAY_WIFI=0`) and Display Nodes (`ESPDASH_NODE_WIFI=0`) keep Wi-Fi STA disabled and lock onto fixed channel `ESPDASH_ESPNOW_CHANNEL` (`1`) for rock-solid ESP-NOW delivery.

---

## 📁 Repository Structure

```text
espDash/
├── README.md                    # Project Overview
├── AGENTS.md                    # Agentsync turn-completion rules
├── CLAUDE.md                    # Project guidelines & build/flash shortcuts
├── docs/                        # Project Documentation
│   ├── ARCHITECTURE.md          # Multi-Node Architecture, Proto v2.4 & Power/Wi-Fi Notes
│   ├── CAN_PROTOCOL_MAP.md      # Reverse-Engineered 9th Gen Honda Civic F-CAN Bus Map
│   ├── CAPTURE_ABS_TC.md        # Guide for capturing unmapped ABS/TC signals
│   ├── OTA_UPDATE_GUIDE.md      # ArduinoOTA Wireless Firmware Deployment Guide
│   ├── SD_CAN_RECORDER.md       # Wireless ESP-NOW -> SD Card Raw CAN Logger Spec
│   └── IMPLEMENTATION_PLAN.md   # System Roadmap & Phase Status
├── firmware/
│   ├── shared/EspDashProto/     # Shared Wire Protocol v2.4 & 30-Signal Catalog
│   ├── esp32-gateway/           # Central ESP32-S3 Gateway + Native CAN Decode Tests
│   └── display-nodes/
│       ├── esp-rectangular-314/ # Waveshare 3.16" ST7701 LCD + SD CAN Recorder
│       ├── xiao-round-gauge/    # XIAO / S3-Zero 240x240 GC9A01 Round Gauge
│       ├── xiao-dual-round/     # Dual 240x240 GC9A01 Virtual 240x480 Gauge
│       ├── esp-round-amoled-touch/ # Waveshare 1.32" 466x466 AMOLED Eco Coach
│       ├── esp-touch-350/       # ES3C35P 3.5" 480x320 Touch + IMU + PFD Node
│       └── crowpanel-epaper-370/# Elecrow CrowPanel 3.7" E-Paper Slow-Data Dash
├── tests/                       # Python test suite (log collector)
├── tools/                       # Host CAN decoder, calibrator, & log collector scripts
└── web-dashboard/               # HTML5/JS Web Telemetry Dashboard App
```
