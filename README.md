# 🏎️ espDash - Distributed Automotive Telemetry System

**espDash** is an ultra-low-latency, car-safe, distributed automotive telemetry and multi-gauge display network built for the **2014 Honda Civic (9th Gen)**.

---

## 🏛️ System Architecture

```text
🚗 Honda Civic F-CAN Bus (500 kbps)
               │
               ▼
⚡ Central Gateway (Waveshare ESP32-S3-RS485-CAN) [esp32-gateway.local]
  ├── 100% Passive TWAI Listen-Only Receiver (Zero ECU Fault Risk)
  ├── 15µA Deep Sleep Auto-Power Off (Zero Battery Drain)
  ├── ESP-NOW Wireless Multicast (1-3ms Latency to Display Gauges)
  └── WebSocket / WebSerial Debug Channel (Port 8888)
               │
               ├───► 💻 Laptop / Tablet Debug Station (Web Dashboard)
               ├───► 🏎️ Node 1: Waveshare ESP32-S3-LCD-3.16 (Universal Telemetry Gauge)
               ├───► 🏎️ Node 2: Seeed XIAO ESP32-S3 + JXL Round (Universal Telemetry Gauge)
               ├───► 🏎️ Node 3: Waveshare ESP32-S3-Touch-AMOLED-1.32 (Universal Touch Gauge)
               ├───► 🏎️ Node 4: Waveshare ESP32-C6-LCD-1.47 (Universal Telemetry Gauge)
               ├───► 🏎️ Node 5: Generic ESP32-S3 + OLED (Universal Gauge / Shift Light)
               └───► 🏠 Node 6: Heltec V3/V4 Meshtastic Relay ──► Home Assistant
```

> **Note:** All display nodes run a universal configurable gauge firmware engine for now. Specialized dedicated task modes will be assigned per display in future releases.

---

## 📡 Wireless ArduinoOTA Firmware Updates

Every device in the network features background **ArduinoOTA / WebOTA** for zero-disassembly wireless firmware upgrades:

* `esp32-gateway.local` — Central CAN Gateway
* `esp32-gauge-3inch.local` — Waveshare 3.16" LCD Universal Gauge
* `esp32-gauge-round.local` — XIAO Round Universal Gauge
* `esp32-gauge-amoled.local` — 1.32" Round AMOLED Universal Touch Gauge
* `esp32-gauge-147.local` — Waveshare ESP32-C6 1.47" LCD Universal Gauge
* `esp32-shiftlight.local` — OLED Universal Gauge / Shift Light
* `esp32-heltec-relay.local` — Heltec Meshtastic LoRa Bridge

---

## 📁 Repository Structure

```text
espDash/
├── README.md                    # Project Overview
├── AGENTS.md                    # Agentsync turn-completion rules
├── CLAUDE.md                    # Project guidelines & shortcuts
├── docs/                        # Project Documentation
│   ├── ARCHITECTURE.md          # Complete Multi-Node Architecture & Wireless Protocols
│   ├── CAN_PROTOCOL_MAP.md      # Reverse-Engineered 9th Gen Honda Civic F-CAN Bus Map
│   └── OTA_UPDATE_GUIDE.md      # ArduinoOTA Wireless Firmware Deployment Guide
├── firmware/
│   └── esp32-gateway/           # Central ESP32-S3 Gateway PlatformIO Project
└── web-dashboard/               # HTML5/JS Web Telemetry Dashboard App
```
