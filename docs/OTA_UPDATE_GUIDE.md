# 📡 ArduinoOTA Wireless Firmware Deployment Guide

All nodes in **espDash** feature background **ArduinoOTA / WebOTA** so firmware can be flashed wirelessly over any configured Wi-Fi network (`Complex_parking`, `Bazanski_ph`, `Bazanski_IS`, or `IOT-monday`) without disassembling dashboard trims or unplugging USB cables.

---

## 📶 Configured Wi-Fi Networks & Passwords

| SSID | Password | Priority / Use Case |
| :--- | :--- | :--- |
| **`Complex_parking`** | `12345678` | Primary Garage / Parking Lot Network |
| **`Bazanski_ph`** | `52288488` | Mobile Phone Hotspot |
| **`Bazanski_IS`** | `52288488` | Home Network |
| **`IOT-monday`** | `fsdL2Dp*KBU0y#9F&c!Zbq853axj` | Office / Fallback Network |

---

## 🌐 mDNS Hostname & Device Map

| Device Node | Hardware Board | Target mDNS Hostname | Role |
| :--- | :--- | :--- | :--- |
| **Central Gateway** | Waveshare ESP32-S3-RS485-CAN | `esp32-gateway.local` | CAN Transceiver Gateway |
| **Display Node 1** | Waveshare ESP32-S3-LCD-3.16 | `esp32-gauge-3inch.local` | Universal Telemetry Gauge |
| **Display Node 2** | XIAO ESP32-S3 + JXL Round | `esp32-gauge-round.local` | Universal Telemetry Gauge |
| **Display Node 3** | Waveshare ESP32-S3-Touch-AMOLED-1.32 | `esp32-gauge-amoled.local` | Universal Touch Gauge |
| **Display Node 4** | Waveshare ESP32-C6-LCD-1.47 | `esp32-gauge-147.local` | Universal Telemetry Gauge |
| **Display Node 5** | Generic ESP32-S3 + OLED | `esp32-shiftlight.local` | Universal Gauge / Shift Light |
| **Relay Node 6** | Heltec V3/V4 Meshtastic ESP32-S3 | `esp32-heltec-relay.local` | Home Assistant LoRa Bridge |

---

## ⚡ Flashing Firmware Over-The-Air (OTA)

### 1. Flash Central Gateway Wirelessly:
```bash
~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gateway.local -d firmware/esp32-gateway
```

### 2. Flash Display Gauge Node Wirelessly:
```bash
~/.platformio/penv/bin/pio run -t upload --upload-port esp32-gauge-round.local -d firmware/display-nodes/xiao-round-gauge
```
